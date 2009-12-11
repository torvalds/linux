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

#define UNSET (-1U)

#define DM1105_BOARD_NOAUTO		UNSET
#define DM1105_BOARD_UNKNOWN		0
#define DM1105_BOARD_DVBWORLD_2002	1
#define DM1105_BOARD_DVBWORLD_2004	2
#define DM1105_BOARD_AXESS_DM05		3

/* ----------------------------------------------- */
/*
 * PCI ID's
 */
#ifndef PCI_VENDOR_ID_TRIGEM
#define PCI_VENDOR_ID_TRIGEM	0x109f
#endif
#ifndef PCI_VENDOR_ID_AXESS
#define PCI_VENDOR_ID_AXESS	0x195d
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
#ifndef PCI_DEVICE_ID_DM05
#define PCI_DEVICE_ID_DM05	0x1105
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
#define DM1105_LNB_OFF				0x00020000
#define DM1105_LNB_13V				0x00010100
#define DM1105_LNB_18V				0x00000100

/* GPIO's for LNB power control for Axess DM05 */
#define DM05_LNB_MASK				0x00000000
#define DM05_LNB_OFF				0x00020000/* actually 13v */
#define DM05_LNB_13V				0x00020000
#define DM05_LNB_18V				0x00030000

static unsigned int card[]  = {[0 ... 3] = UNSET };
module_param_array(card,  int, NULL, 0444);
MODULE_PARM_DESC(card, "card type");

static int ir_debug;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug, "enable debugging information for IR decoding");

static unsigned int dm1105_devcount;

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct dm1105_board {
	char                    *name;
};

struct dm1105_subid {
	u16     subvendor;
	u16     subdevice;
	u32     card;
};

static const struct dm1105_board dm1105_boards[] = {
	[DM1105_BOARD_UNKNOWN] = {
		.name		= "UNKNOWN/GENERIC",
	},
	[DM1105_BOARD_DVBWORLD_2002] = {
		.name		= "DVBWorld PCI 2002",
	},
	[DM1105_BOARD_DVBWORLD_2004] = {
		.name		= "DVBWorld PCI 2004",
	},
	[DM1105_BOARD_AXESS_DM05] = {
		.name		= "Axess/EasyTv DM05",
	},
};

static const struct dm1105_subid dm1105_subids[] = {
	{
		.subvendor = 0x0000,
		.subdevice = 0x2002,
		.card      = DM1105_BOARD_DVBWORLD_2002,
	}, {
		.subvendor = 0x0001,
		.subdevice = 0x2002,
		.card      = DM1105_BOARD_DVBWORLD_2002,
	}, {
		.subvendor = 0x0000,
		.subdevice = 0x2004,
		.card      = DM1105_BOARD_DVBWORLD_2004,
	}, {
		.subvendor = 0x0001,
		.subdevice = 0x2004,
		.card      = DM1105_BOARD_DVBWORLD_2004,
	}, {
		.subvendor = 0x195d,
		.subdevice = 0x1105,
		.card      = DM1105_BOARD_AXESS_DM05,
	},
};

static void dm1105_card_list(struct pci_dev *pci)
{
	int i;

	if (0 == pci->subsystem_vendor &&
			0 == pci->subsystem_device) {
		printk(KERN_ERR
			"dm1105: Your board has no valid PCI Subsystem ID\n"
			"dm1105: and thus can't be autodetected\n"
			"dm1105: Please pass card=<n> insmod option to\n"
			"dm1105: workaround that.  Redirect complaints to\n"
			"dm1105: the vendor of the TV card.  Best regards,\n"
			"dm1105: -- tux\n");
	} else {
		printk(KERN_ERR
			"dm1105: Your board isn't known (yet) to the driver.\n"
			"dm1105: You can try to pick one of the existing\n"
			"dm1105: card configs via card=<n> insmod option.\n"
			"dm1105: Updating to the latest version might help\n"
			"dm1105: as well.\n");
	}
	printk(KERN_ERR "Here is a list of valid choices for the card=<n> "
		   "insmod option:\n");
	for (i = 0; i < ARRAY_SIZE(dm1105_boards); i++)
		printk(KERN_ERR "dm1105:    card=%d -> %s\n",
				i, dm1105_boards[i].name);
}

/* infrared remote control */
struct infrared {
	struct input_dev	*input_dev;
	struct ir_input_state	ir;
	char			input_phys[32];
	struct work_struct	work;
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
	unsigned int boardnr;
	int nr;

	/* i2c */
	struct i2c_adapter i2c_adap;

	/* irq */
	struct work_struct work;
	struct workqueue_struct *wq;
	char wqn[16];

	/* dma */
	dma_addr_t dma_addr;
	unsigned char *ts_buf;
	u32 wrp;
	u32 nextwrp;
	u32 buffer_size;
	unsigned int	PacketErrorCount;
	unsigned int dmarst;
	spinlock_t lock;
};

#define dm_io_mem(reg)	((unsigned long)(&dm1105dvb->io_mem[reg]))

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
	u32 lnb_mask, lnb_13v, lnb_18v, lnb_off;

	switch (dm1105dvb->boardnr) {
	case DM1105_BOARD_AXESS_DM05:
		lnb_mask = DM05_LNB_MASK;
		lnb_off = DM05_LNB_OFF;
		lnb_13v = DM05_LNB_13V;
		lnb_18v = DM05_LNB_18V;
		break;
	case DM1105_BOARD_DVBWORLD_2002:
	case DM1105_BOARD_DVBWORLD_2004:
	default:
		lnb_mask = DM1105_LNB_MASK;
		lnb_off = DM1105_LNB_OFF;
		lnb_13v = DM1105_LNB_13V;
		lnb_18v = DM1105_LNB_18V;
	}

	outl(lnb_mask, dm_io_mem(DM1105_GPIOCTR));
	if (voltage == SEC_VOLTAGE_18)
		outl(lnb_18v , dm_io_mem(DM1105_GPIOVAL));
	else if (voltage == SEC_VOLTAGE_13)
		outl(lnb_13v, dm_io_mem(DM1105_GPIOVAL));
	else
		outl(lnb_off, dm_io_mem(DM1105_GPIOVAL));

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

/* ir work handler */
static void dm1105_emit_key(struct work_struct *work)
{
	struct infrared *ir = container_of(work, struct infrared, work);
	u32 ircom = ir->ir_command;
	u8 data;

	if (ir_debug)
		printk(KERN_INFO "%s: received byte 0x%04x\n", __func__, ircom);

	data = (ircom >> 8) & 0x7f;

	ir_input_keydown(ir->input_dev, &ir->ir, data);
	ir_input_nokey(ir->input_dev, &ir->ir);
}

/* work handler */
static void dm1105_dmx_buffer(struct work_struct *work)
{
	struct dm1105dvb *dm1105dvb =
				container_of(work, struct dm1105dvb, work);
	unsigned int nbpackets;
	u32 oldwrp = dm1105dvb->wrp;
	u32 nextwrp = dm1105dvb->nextwrp;

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
			return;
		}
	}

	if (nextwrp < oldwrp) {
		memcpy(dm1105dvb->ts_buf + dm1105dvb->buffer_size,
						dm1105dvb->ts_buf, nextwrp);
		nbpackets = ((dm1105dvb->buffer_size - oldwrp) + nextwrp) / 188;
	} else
		nbpackets = (nextwrp - oldwrp) / 188;

	dm1105dvb->wrp = nextwrp;
	dvb_dmx_swfilter_packets(&dm1105dvb->demux,
					&dm1105dvb->ts_buf[oldwrp], nbpackets);
}

static irqreturn_t dm1105dvb_irq(int irq, void *dev_id)
{
	struct dm1105dvb *dm1105dvb = dev_id;

	/* Read-Write INSTS Ack's Interrupt for DM1105 chip 16.03.2008 */
	unsigned int intsts = inb(dm_io_mem(DM1105_INTSTS));
	outb(intsts, dm_io_mem(DM1105_INTSTS));

	switch (intsts) {
	case INTSTS_TSIRQ:
	case (INTSTS_TSIRQ | INTSTS_IR):
		dm1105dvb->nextwrp = inl(dm_io_mem(DM1105_WRP)) -
					inl(dm_io_mem(DM1105_STADR));
		queue_work(dm1105dvb->wq, &dm1105dvb->work);
		break;
	case INTSTS_IR:
		dm1105dvb->ir.ir_command = inl(dm_io_mem(DM1105_IRCODE));
		schedule_work(&dm1105dvb->ir.work);
		break;
	}

	return IRQ_HANDLED;
}

int __devinit dm1105_ir_init(struct dm1105dvb *dm1105)
{
	struct input_dev *input_dev;
	struct ir_scancode_table *ir_codes = &ir_codes_dm1105_nec_table;
	int ir_type = IR_TYPE_OTHER;
	int err = -ENOMEM;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	dm1105->ir.input_dev = input_dev;
	snprintf(dm1105->ir.input_phys, sizeof(dm1105->ir.input_phys),
		"pci-%s/ir0", pci_name(dm1105->pdev));

	err = ir_input_init(input_dev, &dm1105->ir.ir, ir_type);
	if (err < 0) {
		input_free_device(input_dev);
		return err;
	}

	input_dev->name = "DVB on-card IR receiver";
	input_dev->phys = dm1105->ir.input_phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 1;
	if (dm1105->pdev->subsystem_vendor) {
		input_dev->id.vendor = dm1105->pdev->subsystem_vendor;
		input_dev->id.product = dm1105->pdev->subsystem_device;
	} else {
		input_dev->id.vendor = dm1105->pdev->vendor;
		input_dev->id.product = dm1105->pdev->device;
	}

	input_dev->dev.parent = &dm1105->pdev->dev;

	INIT_WORK(&dm1105->ir.work, dm1105_emit_key);

	err = ir_input_register(input_dev, ir_codes);

	return err;
}

void __devexit dm1105_ir_exit(struct dm1105dvb *dm1105)
{
	ir_input_unregister(dm1105->ir.input_dev);
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

	switch (dm1105dvb->boardnr) {
	case DM1105_BOARD_DVBWORLD_2004:
		dm1105dvb->fe = dvb_attach(
			cx24116_attach, &serit_sp2633_config,
			&dm1105dvb->i2c_adap);
		if (dm1105dvb->fe)
			dm1105dvb->fe->ops.set_voltage = dm1105dvb_set_voltage;

		break;
	case DM1105_BOARD_DVBWORLD_2002:
	case DM1105_BOARD_AXESS_DM05:
	default:
		dm1105dvb->fe = dvb_attach(
			stv0299_attach, &sharp_z0194a_config,
			&dm1105dvb->i2c_adap);
		if (dm1105dvb->fe) {
			dm1105dvb->fe->ops.set_voltage =
							dm1105dvb_set_voltage;
			dvb_attach(dvb_pll_attach, dm1105dvb->fe, 0x60,
					&dm1105dvb->i2c_adap, DVB_PLL_OPERA1);
			break;
		}

		dm1105dvb->fe = dvb_attach(
			stv0288_attach, &earda_config,
			&dm1105dvb->i2c_adap);
		if (dm1105dvb->fe) {
			dm1105dvb->fe->ops.set_voltage =
						dm1105dvb_set_voltage;
			dvb_attach(stb6000_attach, dm1105dvb->fe, 0x61,
					&dm1105dvb->i2c_adap);
			break;
		}

		dm1105dvb->fe = dvb_attach(
			si21xx_attach, &serit_config,
			&dm1105dvb->i2c_adap);
		if (dm1105dvb->fe)
			dm1105dvb->fe->ops.set_voltage =
						dm1105dvb_set_voltage;

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
		{
			.addr = IIC_24C01_addr >> 1,
			.flags = 0,
			.buf = command,
			.len = 1
		}, {
			.addr = IIC_24C01_addr >> 1,
			.flags = I2C_M_RD,
			.buf = mac,
			.len = 6
		},
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
	int i;

	dm1105dvb = kzalloc(sizeof(struct dm1105dvb), GFP_KERNEL);
	if (!dm1105dvb)
		return -ENOMEM;

	/* board config */
	dm1105dvb->nr = dm1105_devcount;
	dm1105dvb->boardnr = UNSET;
	if (card[dm1105dvb->nr] < ARRAY_SIZE(dm1105_boards))
		dm1105dvb->boardnr = card[dm1105dvb->nr];
	for (i = 0; UNSET == dm1105dvb->boardnr &&
				i < ARRAY_SIZE(dm1105_subids); i++)
		if (pdev->subsystem_vendor ==
			dm1105_subids[i].subvendor &&
				pdev->subsystem_device ==
					dm1105_subids[i].subdevice)
			dm1105dvb->boardnr = dm1105_subids[i].card;

	if (UNSET == dm1105dvb->boardnr) {
		dm1105dvb->boardnr = DM1105_BOARD_UNKNOWN;
		dm1105_card_list(pdev);
	}

	dm1105_devcount++;
	dm1105dvb->pdev = pdev;
	dm1105dvb->buffer_size = 5 * DM1105_DMA_BYTES;
	dm1105dvb->PacketErrorCount = 0;
	dm1105dvb->dmarst = 0;

	ret = pci_enable_device(pdev);
	if (ret < 0)
		goto err_kfree;

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
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

	ret = dm1105dvb_hw_init(dm1105dvb);
	if (ret < 0)
		goto err_pci_iounmap;

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

	INIT_WORK(&dm1105dvb->work, dm1105_dmx_buffer);
	sprintf(dm1105dvb->wqn, "%s/%d", dvb_adapter->name, dvb_adapter->num);
	dm1105dvb->wq = create_singlethread_workqueue(dm1105dvb->wqn);
	if (!dm1105dvb->wq)
		goto err_dvb_net;

	ret = request_irq(pdev->irq, dm1105dvb_irq, IRQF_SHARED,
						DRIVER_NAME, dm1105dvb);
	if (ret < 0)
		goto err_workqueue;

	return 0;

err_workqueue:
	destroy_workqueue(dm1105dvb->wq);
err_dvb_net:
	dvb_net_release(&dm1105dvb->dvbnet);
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
err_pci_iounmap:
	pci_iounmap(pdev, dm1105dvb->io_mem);
err_pci_release_regions:
	pci_release_regions(pdev);
err_pci_disable_device:
	pci_disable_device(pdev);
err_kfree:
	pci_set_drvdata(pdev, NULL);
	kfree(dm1105dvb);
	return ret;
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
	dm1105_devcount--;
	kfree(dm1105dvb);
}

static struct pci_device_id dm1105_id_table[] __devinitdata = {
	{
		.vendor = PCI_VENDOR_ID_TRIGEM,
		.device = PCI_DEVICE_ID_DM1105,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	}, {
		.vendor = PCI_VENDOR_ID_AXESS,
		.device = PCI_DEVICE_ID_DM05,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
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
