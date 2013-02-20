/*
 * Freescale CPM1/CPM2 I2C interface.
 * Copyright (c) 1999 Dan Malek (dmalek@jlc.net).
 *
 * moved into proper i2c interface;
 * Brad Parker (brad@heeltoe.com)
 *
 * Parts from dbox2_i2c.c (cvs.tuxbox.org)
 * (C) 2000-2001 Felix Domke (tmbinc@gmx.net), Gillem (htoa@gmx.net)
 *
 * (C) 2007 Montavista Software, Inc.
 * Vitaly Bordug <vitb@kernel.crashing.org>
 *
 * Converted to of_platform_device. Renamed to i2c-cpm.c.
 * (C) 2007,2008 Jochen Friedrich <jochen@scram.de>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_i2c.h>
#include <sysdev/fsl_soc.h>
#include <asm/cpm.h>

/* Try to define this if you have an older CPU (earlier than rev D4) */
/* However, better use a GPIO based bitbang driver in this case :/   */
#undef	I2C_CHIP_ERRATA

#define CPM_MAX_READ    513
#define CPM_MAXBD       4

#define I2C_EB			(0x10) /* Big endian mode */
#define I2C_EB_CPM2		(0x30) /* Big endian mode, memory snoop */

#define DPRAM_BASE		((u8 __iomem __force *)cpm_muram_addr(0))

/* I2C parameter RAM. */
struct i2c_ram {
	ushort  rbase;		/* Rx Buffer descriptor base address */
	ushort  tbase;		/* Tx Buffer descriptor base address */
	u_char  rfcr;		/* Rx function code */
	u_char  tfcr;		/* Tx function code */
	ushort  mrblr;		/* Max receive buffer length */
	uint    rstate;		/* Internal */
	uint    rdp;		/* Internal */
	ushort  rbptr;		/* Rx Buffer descriptor pointer */
	ushort  rbc;		/* Internal */
	uint    rxtmp;		/* Internal */
	uint    tstate;		/* Internal */
	uint    tdp;		/* Internal */
	ushort  tbptr;		/* Tx Buffer descriptor pointer */
	ushort  tbc;		/* Internal */
	uint    txtmp;		/* Internal */
	char    res1[4];	/* Reserved */
	ushort  rpbase;		/* Relocation pointer */
	char    res2[2];	/* Reserved */
};

#define I2COM_START	0x80
#define I2COM_MASTER	0x01
#define I2CER_TXE	0x10
#define I2CER_BUSY	0x04
#define I2CER_TXB	0x02
#define I2CER_RXB	0x01
#define I2MOD_EN	0x01

/* I2C Registers */
struct i2c_reg {
	u8	i2mod;
	u8	res1[3];
	u8	i2add;
	u8	res2[3];
	u8	i2brg;
	u8	res3[3];
	u8	i2com;
	u8	res4[3];
	u8	i2cer;
	u8	res5[3];
	u8	i2cmr;
};

struct cpm_i2c {
	char *base;
	struct platform_device *ofdev;
	struct i2c_adapter adap;
	uint dp_addr;
	int version; /* CPM1=1, CPM2=2 */
	int irq;
	int cp_command;
	int freq;
	struct i2c_reg __iomem *i2c_reg;
	struct i2c_ram __iomem *i2c_ram;
	u16 i2c_addr;
	wait_queue_head_t i2c_wait;
	cbd_t __iomem *tbase;
	cbd_t __iomem *rbase;
	u_char *txbuf[CPM_MAXBD];
	u_char *rxbuf[CPM_MAXBD];
	u32 txdma[CPM_MAXBD];
	u32 rxdma[CPM_MAXBD];
};

static irqreturn_t cpm_i2c_interrupt(int irq, void *dev_id)
{
	struct cpm_i2c *cpm;
	struct i2c_reg __iomem *i2c_reg;
	struct i2c_adapter *adap = dev_id;
	int i;

	cpm = i2c_get_adapdata(dev_id);
	i2c_reg = cpm->i2c_reg;

	/* Clear interrupt. */
	i = in_8(&i2c_reg->i2cer);
	out_8(&i2c_reg->i2cer, i);

	dev_dbg(&adap->dev, "Interrupt: %x\n", i);

	wake_up(&cpm->i2c_wait);

	return i ? IRQ_HANDLED : IRQ_NONE;
}

static void cpm_reset_i2c_params(struct cpm_i2c *cpm)
{
	struct i2c_ram __iomem *i2c_ram = cpm->i2c_ram;

	/* Set up the I2C parameters in the parameter ram. */
	out_be16(&i2c_ram->tbase, (u8 __iomem *)cpm->tbase - DPRAM_BASE);
	out_be16(&i2c_ram->rbase, (u8 __iomem *)cpm->rbase - DPRAM_BASE);

	if (cpm->version == 1) {
		out_8(&i2c_ram->tfcr, I2C_EB);
		out_8(&i2c_ram->rfcr, I2C_EB);
	} else {
		out_8(&i2c_ram->tfcr, I2C_EB_CPM2);
		out_8(&i2c_ram->rfcr, I2C_EB_CPM2);
	}

	out_be16(&i2c_ram->mrblr, CPM_MAX_READ);

	out_be32(&i2c_ram->rstate, 0);
	out_be32(&i2c_ram->rdp, 0);
	out_be16(&i2c_ram->rbptr, 0);
	out_be16(&i2c_ram->rbc, 0);
	out_be32(&i2c_ram->rxtmp, 0);
	out_be32(&i2c_ram->tstate, 0);
	out_be32(&i2c_ram->tdp, 0);
	out_be16(&i2c_ram->tbptr, 0);
	out_be16(&i2c_ram->tbc, 0);
	out_be32(&i2c_ram->txtmp, 0);
}

static void cpm_i2c_force_close(struct i2c_adapter *adap)
{
	struct cpm_i2c *cpm = i2c_get_adapdata(adap);
	struct i2c_reg __iomem *i2c_reg = cpm->i2c_reg;

	dev_dbg(&adap->dev, "cpm_i2c_force_close()\n");

	cpm_command(cpm->cp_command, CPM_CR_CLOSE_RX_BD);

	out_8(&i2c_reg->i2cmr, 0x00);	/* Disable all interrupts */
	out_8(&i2c_reg->i2cer, 0xff);
}

static void cpm_i2c_parse_message(struct i2c_adapter *adap,
	struct i2c_msg *pmsg, int num, int tx, int rx)
{
	cbd_t __iomem *tbdf;
	cbd_t __iomem *rbdf;
	u_char addr;
	u_char *tb;
	u_char *rb;
	struct cpm_i2c *cpm = i2c_get_adapdata(adap);

	tbdf = cpm->tbase + tx;
	rbdf = cpm->rbase + rx;

	addr = pmsg->addr << 1;
	if (pmsg->flags & I2C_M_RD)
		addr |= 1;

	tb = cpm->txbuf[tx];
	rb = cpm->rxbuf[rx];

	/* Align read buffer */
	rb = (u_char *) (((ulong) rb + 1) & ~1);

	tb[0] = addr;		/* Device address byte w/rw flag */

	out_be16(&tbdf->cbd_datlen, pmsg->len + 1);
	out_be16(&tbdf->cbd_sc, 0);

	if (!(pmsg->flags & I2C_M_NOSTART))
		setbits16(&tbdf->cbd_sc, BD_I2C_START);

	if (tx + 1 == num)
		setbits16(&tbdf->cbd_sc, BD_SC_LAST | BD_SC_WRAP);

	if (pmsg->flags & I2C_M_RD) {
		/*
		 * To read, we need an empty buffer of the proper length.
		 * All that is used is the first byte for address, the remainder
		 * is just used for timing (and doesn't really have to exist).
		 */

		dev_dbg(&adap->dev, "cpm_i2c_read(abyte=0x%x)\n", addr);

		out_be16(&rbdf->cbd_datlen, 0);
		out_be16(&rbdf->cbd_sc, BD_SC_EMPTY | BD_SC_INTRPT);

		if (rx + 1 == CPM_MAXBD)
			setbits16(&rbdf->cbd_sc, BD_SC_WRAP);

		eieio();
		setbits16(&tbdf->cbd_sc, BD_SC_READY);
	} else {
		dev_dbg(&adap->dev, "cpm_i2c_write(abyte=0x%x)\n", addr);

		memcpy(tb+1, pmsg->buf, pmsg->len);

		eieio();
		setbits16(&tbdf->cbd_sc, BD_SC_READY | BD_SC_INTRPT);
	}
}

static int cpm_i2c_check_message(struct i2c_adapter *adap,
	struct i2c_msg *pmsg, int tx, int rx)
{
	cbd_t __iomem *tbdf;
	cbd_t __iomem *rbdf;
	u_char *tb;
	u_char *rb;
	struct cpm_i2c *cpm = i2c_get_adapdata(adap);

	tbdf = cpm->tbase + tx;
	rbdf = cpm->rbase + rx;

	tb = cpm->txbuf[tx];
	rb = cpm->rxbuf[rx];

	/* Align read buffer */
	rb = (u_char *) (((uint) rb + 1) & ~1);

	eieio();
	if (pmsg->flags & I2C_M_RD) {
		dev_dbg(&adap->dev, "tx sc 0x%04x, rx sc 0x%04x\n",
			in_be16(&tbdf->cbd_sc), in_be16(&rbdf->cbd_sc));

		if (in_be16(&tbdf->cbd_sc) & BD_SC_NAK) {
			dev_dbg(&adap->dev, "I2C read; No ack\n");
			return -ENXIO;
		}
		if (in_be16(&rbdf->cbd_sc) & BD_SC_EMPTY) {
			dev_err(&adap->dev,
				"I2C read; complete but rbuf empty\n");
			return -EREMOTEIO;
		}
		if (in_be16(&rbdf->cbd_sc) & BD_SC_OV) {
			dev_err(&adap->dev, "I2C read; Overrun\n");
			return -EREMOTEIO;
		}
		memcpy(pmsg->buf, rb, pmsg->len);
	} else {
		dev_dbg(&adap->dev, "tx sc %d 0x%04x\n", tx,
			in_be16(&tbdf->cbd_sc));

		if (in_be16(&tbdf->cbd_sc) & BD_SC_NAK) {
			dev_dbg(&adap->dev, "I2C write; No ack\n");
			return -ENXIO;
		}
		if (in_be16(&tbdf->cbd_sc) & BD_SC_UN) {
			dev_err(&adap->dev, "I2C write; Underrun\n");
			return -EIO;
		}
		if (in_be16(&tbdf->cbd_sc) & BD_SC_CL) {
			dev_err(&adap->dev, "I2C write; Collision\n");
			return -EIO;
		}
	}
	return 0;
}

static int cpm_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct cpm_i2c *cpm = i2c_get_adapdata(adap);
	struct i2c_reg __iomem *i2c_reg = cpm->i2c_reg;
	struct i2c_ram __iomem *i2c_ram = cpm->i2c_ram;
	struct i2c_msg *pmsg;
	int ret, i;
	int tptr;
	int rptr;
	cbd_t __iomem *tbdf;
	cbd_t __iomem *rbdf;

	if (num > CPM_MAXBD)
		return -EINVAL;

	/* Check if we have any oversized READ requests */
	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];
		if (pmsg->len >= CPM_MAX_READ)
			return -EINVAL;
	}

	/* Reset to use first buffer */
	out_be16(&i2c_ram->rbptr, in_be16(&i2c_ram->rbase));
	out_be16(&i2c_ram->tbptr, in_be16(&i2c_ram->tbase));

	tbdf = cpm->tbase;
	rbdf = cpm->rbase;

	tptr = 0;
	rptr = 0;

	while (tptr < num) {
		pmsg = &msgs[tptr];
		dev_dbg(&adap->dev, "R: %d T: %d\n", rptr, tptr);

		cpm_i2c_parse_message(adap, pmsg, num, tptr, rptr);
		if (pmsg->flags & I2C_M_RD)
			rptr++;
		tptr++;
	}
	/* Start transfer now */
	/* Enable RX/TX/Error interupts */
	out_8(&i2c_reg->i2cmr, I2CER_TXE | I2CER_TXB | I2CER_RXB);
	out_8(&i2c_reg->i2cer, 0xff);	/* Clear interrupt status */
	/* Chip bug, set enable here */
	setbits8(&i2c_reg->i2mod, I2MOD_EN);	/* Enable */
	/* Begin transmission */
	setbits8(&i2c_reg->i2com, I2COM_START);

	tptr = 0;
	rptr = 0;

	while (tptr < num) {
		/* Check for outstanding messages */
		dev_dbg(&adap->dev, "test ready.\n");
		pmsg = &msgs[tptr];
		if (pmsg->flags & I2C_M_RD)
			ret = wait_event_timeout(cpm->i2c_wait,
				(in_be16(&tbdf[tptr].cbd_sc) & BD_SC_NAK) ||
				!(in_be16(&rbdf[rptr].cbd_sc) & BD_SC_EMPTY),
				1 * HZ);
		else
			ret = wait_event_timeout(cpm->i2c_wait,
				!(in_be16(&tbdf[tptr].cbd_sc) & BD_SC_READY),
				1 * HZ);
		if (ret == 0) {
			ret = -EREMOTEIO;
			dev_err(&adap->dev, "I2C transfer: timeout\n");
			goto out_err;
		}
		if (ret > 0) {
			dev_dbg(&adap->dev, "ready.\n");
			ret = cpm_i2c_check_message(adap, pmsg, tptr, rptr);
			tptr++;
			if (pmsg->flags & I2C_M_RD)
				rptr++;
			if (ret)
				goto out_err;
		}
	}
#ifdef I2C_CHIP_ERRATA
	/*
	 * Chip errata, clear enable. This is not needed on rev D4 CPUs.
	 * Disabling I2C too early may cause too short stop condition
	 */
	udelay(4);
	clrbits8(&i2c_reg->i2mod, I2MOD_EN);
#endif
	return (num);

out_err:
	cpm_i2c_force_close(adap);
#ifdef I2C_CHIP_ERRATA
	/*
	 * Chip errata, clear enable. This is not needed on rev D4 CPUs.
	 */
	clrbits8(&i2c_reg->i2mod, I2MOD_EN);
#endif
	return ret;
}

static u32 cpm_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK);
}

/* -----exported algorithm data: -------------------------------------	*/

static const struct i2c_algorithm cpm_i2c_algo = {
	.master_xfer = cpm_i2c_xfer,
	.functionality = cpm_i2c_func,
};

static const struct i2c_adapter cpm_ops = {
	.owner		= THIS_MODULE,
	.name		= "i2c-cpm",
	.algo		= &cpm_i2c_algo,
};

static int cpm_i2c_setup(struct cpm_i2c *cpm)
{
	struct platform_device *ofdev = cpm->ofdev;
	const u32 *data;
	int len, ret, i;
	void __iomem *i2c_base;
	cbd_t __iomem *tbdf;
	cbd_t __iomem *rbdf;
	unsigned char brg;

	dev_dbg(&cpm->ofdev->dev, "cpm_i2c_setup()\n");

	init_waitqueue_head(&cpm->i2c_wait);

	cpm->irq = of_irq_to_resource(ofdev->dev.of_node, 0, NULL);
	if (!cpm->irq)
		return -EINVAL;

	/* Install interrupt handler. */
	ret = request_irq(cpm->irq, cpm_i2c_interrupt, 0, "cpm_i2c",
			  &cpm->adap);
	if (ret)
		return ret;

	/* I2C parameter RAM */
	i2c_base = of_iomap(ofdev->dev.of_node, 1);
	if (i2c_base == NULL) {
		ret = -EINVAL;
		goto out_irq;
	}

	if (of_device_is_compatible(ofdev->dev.of_node, "fsl,cpm1-i2c")) {

		/* Check for and use a microcode relocation patch. */
		cpm->i2c_ram = i2c_base;
		cpm->i2c_addr = in_be16(&cpm->i2c_ram->rpbase);

		/*
		 * Maybe should use cpm_muram_alloc instead of hardcoding
		 * this in micropatch.c
		 */
		if (cpm->i2c_addr) {
			cpm->i2c_ram = cpm_muram_addr(cpm->i2c_addr);
			iounmap(i2c_base);
		}

		cpm->version = 1;

	} else if (of_device_is_compatible(ofdev->dev.of_node, "fsl,cpm2-i2c")) {
		cpm->i2c_addr = cpm_muram_alloc(sizeof(struct i2c_ram), 64);
		cpm->i2c_ram = cpm_muram_addr(cpm->i2c_addr);
		out_be16(i2c_base, cpm->i2c_addr);
		iounmap(i2c_base);

		cpm->version = 2;

	} else {
		iounmap(i2c_base);
		ret = -EINVAL;
		goto out_irq;
	}

	/* I2C control/status registers */
	cpm->i2c_reg = of_iomap(ofdev->dev.of_node, 0);
	if (cpm->i2c_reg == NULL) {
		ret = -EINVAL;
		goto out_ram;
	}

	data = of_get_property(ofdev->dev.of_node, "fsl,cpm-command", &len);
	if (!data || len != 4) {
		ret = -EINVAL;
		goto out_reg;
	}
	cpm->cp_command = *data;

	data = of_get_property(ofdev->dev.of_node, "linux,i2c-class", &len);
	if (data && len == 4)
		cpm->adap.class = *data;

	data = of_get_property(ofdev->dev.of_node, "clock-frequency", &len);
	if (data && len == 4)
		cpm->freq = *data;
	else
		cpm->freq = 60000; /* use 60kHz i2c clock by default */

	/*
	 * Allocate space for CPM_MAXBD transmit and receive buffer
	 * descriptors in the DP ram.
	 */
	cpm->dp_addr = cpm_muram_alloc(sizeof(cbd_t) * 2 * CPM_MAXBD, 8);
	if (!cpm->dp_addr) {
		ret = -ENOMEM;
		goto out_reg;
	}

	cpm->tbase = cpm_muram_addr(cpm->dp_addr);
	cpm->rbase = cpm_muram_addr(cpm->dp_addr + sizeof(cbd_t) * CPM_MAXBD);

	/* Allocate TX and RX buffers */

	tbdf = cpm->tbase;
	rbdf = cpm->rbase;

	for (i = 0; i < CPM_MAXBD; i++) {
		cpm->rxbuf[i] = dma_alloc_coherent(&cpm->ofdev->dev,
						   CPM_MAX_READ + 1,
						   &cpm->rxdma[i], GFP_KERNEL);
		if (!cpm->rxbuf[i]) {
			ret = -ENOMEM;
			goto out_muram;
		}
		out_be32(&rbdf[i].cbd_bufaddr, ((cpm->rxdma[i] + 1) & ~1));

		cpm->txbuf[i] = (unsigned char *)dma_alloc_coherent(&cpm->ofdev->dev, CPM_MAX_READ + 1, &cpm->txdma[i], GFP_KERNEL);
		if (!cpm->txbuf[i]) {
			ret = -ENOMEM;
			goto out_muram;
		}
		out_be32(&tbdf[i].cbd_bufaddr, cpm->txdma[i]);
	}

	/* Initialize Tx/Rx parameters. */

	cpm_reset_i2c_params(cpm);

	dev_dbg(&cpm->ofdev->dev, "i2c_ram 0x%p, i2c_addr 0x%04x, freq %d\n",
		cpm->i2c_ram, cpm->i2c_addr, cpm->freq);
	dev_dbg(&cpm->ofdev->dev, "tbase 0x%04x, rbase 0x%04x\n",
		(u8 __iomem *)cpm->tbase - DPRAM_BASE,
		(u8 __iomem *)cpm->rbase - DPRAM_BASE);

	cpm_command(cpm->cp_command, CPM_CR_INIT_TRX);

	/*
	 * Select an invalid address. Just make sure we don't use loopback mode
	 */
	out_8(&cpm->i2c_reg->i2add, 0x7f << 1);

	/*
	 * PDIV is set to 00 in i2mod, so brgclk/32 is used as input to the
	 * i2c baud rate generator. This is divided by 2 x (DIV + 3) to get
	 * the actual i2c bus frequency.
	 */
	brg = get_brgfreq() / (32 * 2 * cpm->freq) - 3;
	out_8(&cpm->i2c_reg->i2brg, brg);

	out_8(&cpm->i2c_reg->i2mod, 0x00);
	out_8(&cpm->i2c_reg->i2com, I2COM_MASTER);	/* Master mode */

	/* Disable interrupts. */
	out_8(&cpm->i2c_reg->i2cmr, 0);
	out_8(&cpm->i2c_reg->i2cer, 0xff);

	return 0;

out_muram:
	for (i = 0; i < CPM_MAXBD; i++) {
		if (cpm->rxbuf[i])
			dma_free_coherent(&cpm->ofdev->dev, CPM_MAX_READ + 1,
				cpm->rxbuf[i], cpm->rxdma[i]);
		if (cpm->txbuf[i])
			dma_free_coherent(&cpm->ofdev->dev, CPM_MAX_READ + 1,
				cpm->txbuf[i], cpm->txdma[i]);
	}
	cpm_muram_free(cpm->dp_addr);
out_reg:
	iounmap(cpm->i2c_reg);
out_ram:
	if ((cpm->version == 1) && (!cpm->i2c_addr))
		iounmap(cpm->i2c_ram);
	if (cpm->version == 2)
		cpm_muram_free(cpm->i2c_addr);
out_irq:
	free_irq(cpm->irq, &cpm->adap);
	return ret;
}

static void cpm_i2c_shutdown(struct cpm_i2c *cpm)
{
	int i;

	/* Shut down I2C. */
	clrbits8(&cpm->i2c_reg->i2mod, I2MOD_EN);

	/* Disable interrupts */
	out_8(&cpm->i2c_reg->i2cmr, 0);
	out_8(&cpm->i2c_reg->i2cer, 0xff);

	free_irq(cpm->irq, &cpm->adap);

	/* Free all memory */
	for (i = 0; i < CPM_MAXBD; i++) {
		dma_free_coherent(&cpm->ofdev->dev, CPM_MAX_READ + 1,
			cpm->rxbuf[i], cpm->rxdma[i]);
		dma_free_coherent(&cpm->ofdev->dev, CPM_MAX_READ + 1,
			cpm->txbuf[i], cpm->txdma[i]);
	}

	cpm_muram_free(cpm->dp_addr);
	iounmap(cpm->i2c_reg);

	if ((cpm->version == 1) && (!cpm->i2c_addr))
		iounmap(cpm->i2c_ram);
	if (cpm->version == 2)
		cpm_muram_free(cpm->i2c_addr);
}

static int cpm_i2c_probe(struct platform_device *ofdev)
{
	int result, len;
	struct cpm_i2c *cpm;
	const u32 *data;

	cpm = kzalloc(sizeof(struct cpm_i2c), GFP_KERNEL);
	if (!cpm)
		return -ENOMEM;

	cpm->ofdev = ofdev;

	dev_set_drvdata(&ofdev->dev, cpm);

	cpm->adap = cpm_ops;
	i2c_set_adapdata(&cpm->adap, cpm);
	cpm->adap.dev.parent = &ofdev->dev;
	cpm->adap.dev.of_node = of_node_get(ofdev->dev.of_node);

	result = cpm_i2c_setup(cpm);
	if (result) {
		dev_err(&ofdev->dev, "Unable to init hardware\n");
		goto out_free;
	}

	/* register new adapter to i2c module... */

	data = of_get_property(ofdev->dev.of_node, "linux,i2c-index", &len);
	cpm->adap.nr = (data && len == 4) ? be32_to_cpup(data) : -1;
	result = i2c_add_numbered_adapter(&cpm->adap);

	if (result < 0) {
		dev_err(&ofdev->dev, "Unable to register with I2C\n");
		goto out_shut;
	}

	dev_dbg(&ofdev->dev, "hw routines for %s registered.\n",
		cpm->adap.name);

	/*
	 * register OF I2C devices
	 */
	of_i2c_register_devices(&cpm->adap);

	return 0;
out_shut:
	cpm_i2c_shutdown(cpm);
out_free:
	dev_set_drvdata(&ofdev->dev, NULL);
	kfree(cpm);

	return result;
}

static int cpm_i2c_remove(struct platform_device *ofdev)
{
	struct cpm_i2c *cpm = dev_get_drvdata(&ofdev->dev);

	i2c_del_adapter(&cpm->adap);

	cpm_i2c_shutdown(cpm);

	dev_set_drvdata(&ofdev->dev, NULL);
	kfree(cpm);

	return 0;
}

static const struct of_device_id cpm_i2c_match[] = {
	{
		.compatible = "fsl,cpm1-i2c",
	},
	{
		.compatible = "fsl,cpm2-i2c",
	},
	{},
};

MODULE_DEVICE_TABLE(of, cpm_i2c_match);

static struct platform_driver cpm_i2c_driver = {
	.probe		= cpm_i2c_probe,
	.remove		= cpm_i2c_remove,
	.driver = {
		.name = "fsl-i2c-cpm",
		.owner = THIS_MODULE,
		.of_match_table = cpm_i2c_match,
	},
};

module_platform_driver(cpm_i2c_driver);

MODULE_AUTHOR("Jochen Friedrich <jochen@scram.de>");
MODULE_DESCRIPTION("I2C-Bus adapter routines for CPM boards");
MODULE_LICENSE("GPL");
