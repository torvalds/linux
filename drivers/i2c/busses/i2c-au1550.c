/*
 * i2c-au1550.c: SMBus (i2c) adapter for Alchemy PSC interface
 * Copyright (C) 2004 Embedded Edge, LLC <dan@embeddededge.com>
 *
 * 2.6 port by Matt Porter <mporter@kernel.crashing.org>
 *
 * The documentation describes this as an SMBus controller, but it doesn't
 * understand any of the SMBus protocol in hardware.  It's really an I2C
 * controller that could emulate most of the SMBus in software.
 *
 * This is just a skeleton adapter to use with the Au1550 PSC
 * algorithm.  It was developed for the Pb1550, but will work with
 * any Au1550 board that has a similar PSC configuration.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/i2c.h>

#include <asm/mach-au1x00/au1xxx.h>
#include <asm/mach-au1x00/au1xxx_psc.h>

#include "i2c-au1550.h"

static int
wait_xfer_done(struct i2c_au1550_data *adap)
{
	u32	stat;
	int	i;
	volatile psc_smb_t	*sp;

	sp = (volatile psc_smb_t *)(adap->psc_base);

	/* Wait for Tx Buffer Empty
	*/
	for (i = 0; i < adap->xfer_timeout; i++) {
		stat = sp->psc_smbstat;
		au_sync();
		if ((stat & PSC_SMBSTAT_TE) != 0)
			return 0;

		udelay(1);
	}

	return -ETIMEDOUT;
}

static int
wait_ack(struct i2c_au1550_data *adap)
{
	u32	stat;
	volatile psc_smb_t	*sp;

	if (wait_xfer_done(adap))
		return -ETIMEDOUT;

	sp = (volatile psc_smb_t *)(adap->psc_base);

	stat = sp->psc_smbevnt;
	au_sync();

	if ((stat & (PSC_SMBEVNT_DN | PSC_SMBEVNT_AN | PSC_SMBEVNT_AL)) != 0)
		return -ETIMEDOUT;

	return 0;
}

static int
wait_master_done(struct i2c_au1550_data *adap)
{
	u32	stat;
	int	i;
	volatile psc_smb_t	*sp;

	sp = (volatile psc_smb_t *)(adap->psc_base);

	/* Wait for Master Done.
	*/
	for (i = 0; i < adap->xfer_timeout; i++) {
		stat = sp->psc_smbevnt;
		au_sync();
		if ((stat & PSC_SMBEVNT_MD) != 0)
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int
do_address(struct i2c_au1550_data *adap, unsigned int addr, int rd)
{
	volatile psc_smb_t	*sp;
	u32			stat;

	sp = (volatile psc_smb_t *)(adap->psc_base);

	/* Reset the FIFOs, clear events.
	*/
	stat = sp->psc_smbstat;
	sp->psc_smbevnt = PSC_SMBEVNT_ALLCLR;
	au_sync();

	if (!(stat & PSC_SMBSTAT_TE) || !(stat & PSC_SMBSTAT_RE)) {
		sp->psc_smbpcr = PSC_SMBPCR_DC;
		au_sync();
		do {
			stat = sp->psc_smbpcr;
			au_sync();
		} while ((stat & PSC_SMBPCR_DC) != 0);
		udelay(50);
	}

	/* Write out the i2c chip address and specify operation
	*/
	addr <<= 1;
	if (rd)
		addr |= 1;

	/* Put byte into fifo, start up master.
	*/
	sp->psc_smbtxrx = addr;
	au_sync();
	sp->psc_smbpcr = PSC_SMBPCR_MS;
	au_sync();
	if (wait_ack(adap))
		return -EIO;
	return 0;
}

static u32
wait_for_rx_byte(struct i2c_au1550_data *adap, u32 *ret_data)
{
	int	j;
	u32	data, stat;
	volatile psc_smb_t	*sp;

	if (wait_xfer_done(adap))
		return -EIO;

	sp = (volatile psc_smb_t *)(adap->psc_base);

	j =  adap->xfer_timeout * 100;
	do {
		j--;
		if (j <= 0)
			return -EIO;

		stat = sp->psc_smbstat;
		au_sync();
		if ((stat & PSC_SMBSTAT_RE) == 0)
			j = 0;
		else
			udelay(1);
	} while (j > 0);
	data = sp->psc_smbtxrx;
	au_sync();
	*ret_data = data;

	return 0;
}

static int
i2c_read(struct i2c_au1550_data *adap, unsigned char *buf,
		    unsigned int len)
{
	int	i;
	u32	data;
	volatile psc_smb_t	*sp;

	if (len == 0)
		return 0;

	/* A read is performed by stuffing the transmit fifo with
	 * zero bytes for timing, waiting for bytes to appear in the
	 * receive fifo, then reading the bytes.
	 */

	sp = (volatile psc_smb_t *)(adap->psc_base);

	i = 0;
	while (i < (len-1)) {
		sp->psc_smbtxrx = 0;
		au_sync();
		if (wait_for_rx_byte(adap, &data))
			return -EIO;

		buf[i] = data;
		i++;
	}

	/* The last byte has to indicate transfer done.
	*/
	sp->psc_smbtxrx = PSC_SMBTXRX_STP;
	au_sync();
	if (wait_master_done(adap))
		return -EIO;

	data = sp->psc_smbtxrx;
	au_sync();
	buf[i] = data;
	return 0;
}

static int
i2c_write(struct i2c_au1550_data *adap, unsigned char *buf,
		     unsigned int len)
{
	int	i;
	u32	data;
	volatile psc_smb_t	*sp;

	if (len == 0)
		return 0;

	sp = (volatile psc_smb_t *)(adap->psc_base);

	i = 0;
	while (i < (len-1)) {
		data = buf[i];
		sp->psc_smbtxrx = data;
		au_sync();
		if (wait_ack(adap))
			return -EIO;
		i++;
	}

	/* The last byte has to indicate transfer done.
	*/
	data = buf[i];
	data |= PSC_SMBTXRX_STP;
	sp->psc_smbtxrx = data;
	au_sync();
	if (wait_master_done(adap))
		return -EIO;
	return 0;
}

static int
au1550_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs, int num)
{
	struct i2c_au1550_data *adap = i2c_adap->algo_data;
	struct i2c_msg *p;
	int i, err = 0;

	for (i = 0; !err && i < num; i++) {
		p = &msgs[i];
		err = do_address(adap, p->addr, p->flags & I2C_M_RD);
		if (err || !p->len)
			continue;
		if (p->flags & I2C_M_RD)
			err = i2c_read(adap, p->buf, p->len);
		else
			err = i2c_write(adap, p->buf, p->len);
	}

	/* Return the number of messages processed, or the error code.
	*/
	if (err == 0)
		err = num;
	return err;
}

static u32
au1550_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm au1550_algo = {
	.master_xfer	= au1550_xfer,
	.functionality	= au1550_func,
};

/*
 * registering functions to load algorithms at runtime
 * Prior to calling us, the 50MHz clock frequency and routing
 * must have been set up for the PSC indicated by the adapter.
 */
int
i2c_au1550_add_bus(struct i2c_adapter *i2c_adap)
{
	struct i2c_au1550_data *adap = i2c_adap->algo_data;
	volatile psc_smb_t	*sp;
	u32	stat;

	i2c_adap->algo = &au1550_algo;

	/* Now, set up the PSC for SMBus PIO mode.
	*/
	sp = (volatile psc_smb_t *)(adap->psc_base);
	sp->psc_ctrl = PSC_CTRL_DISABLE;
	au_sync();
	sp->psc_sel = PSC_SEL_PS_SMBUSMODE;
	sp->psc_smbcfg = 0;
	au_sync();
	sp->psc_ctrl = PSC_CTRL_ENABLE;
	au_sync();
	do {
		stat = sp->psc_smbstat;
		au_sync();
	} while ((stat & PSC_SMBSTAT_SR) == 0);

	sp->psc_smbcfg = (PSC_SMBCFG_RT_FIFO8 | PSC_SMBCFG_TT_FIFO8 |
				PSC_SMBCFG_DD_DISABLE);

	/* Divide by 8 to get a 6.25 MHz clock.  The later protocol
	 * timings are based on this clock.
	 */
	sp->psc_smbcfg |= PSC_SMBCFG_SET_DIV(PSC_SMBCFG_DIV8);
	sp->psc_smbmsk = PSC_SMBMSK_ALLMASK;
	au_sync();

	/* Set the protocol timer values.  See Table 71 in the
	 * Au1550 Data Book for standard timing values.
	 */
	sp->psc_smbtmr = PSC_SMBTMR_SET_TH(0) | PSC_SMBTMR_SET_PS(15) | \
		PSC_SMBTMR_SET_PU(15) | PSC_SMBTMR_SET_SH(15) | \
		PSC_SMBTMR_SET_SU(15) | PSC_SMBTMR_SET_CL(15) | \
		PSC_SMBTMR_SET_CH(15);
	au_sync();

	sp->psc_smbcfg |= PSC_SMBCFG_DE_ENABLE;
	do {
		stat = sp->psc_smbstat;
		au_sync();
	} while ((stat & PSC_SMBSTAT_DR) == 0);

	return i2c_add_adapter(i2c_adap);
}


int
i2c_au1550_del_bus(struct i2c_adapter *adap)
{
	return i2c_del_adapter(adap);
}

static int
pb1550_reg(struct i2c_client *client)
{
	return 0;
}

static int
pb1550_unreg(struct i2c_client *client)
{
	return 0;
}

static struct i2c_au1550_data pb1550_i2c_info = {
	SMBUS_PSC_BASE, 200, 200
};

static struct i2c_adapter pb1550_board_adapter = {
	name:              "pb1550 adapter",
	id:                I2C_HW_AU1550_PSC,
	algo:              NULL,
	algo_data:         &pb1550_i2c_info,
	client_register:   pb1550_reg,
	client_unregister: pb1550_unreg,
};

/* BIG hack to support the control interface on the Wolfson WM8731
 * audio codec on the Pb1550 board.  We get an address and two data
 * bytes to write, create an i2c message, and send it across the
 * i2c transfer function.  We do this here because we have access to
 * the i2c adapter structure.
 */
static struct i2c_msg wm_i2c_msg;  /* We don't want this stuff on the stack */
static	u8 i2cbuf[2];

int
pb1550_wm_codec_write(u8 addr, u8 reg, u8 val)
{
	wm_i2c_msg.addr = addr;
	wm_i2c_msg.flags = 0;
	wm_i2c_msg.buf = i2cbuf;
	wm_i2c_msg.len = 2;
	i2cbuf[0] = reg;
	i2cbuf[1] = val;

	return pb1550_board_adapter.algo->master_xfer(&pb1550_board_adapter, &wm_i2c_msg, 1);
}

static int __init
i2c_au1550_init(void)
{
	printk(KERN_INFO "Au1550 I2C: ");

	/* This is where we would set up a 50MHz clock source
	 * and routing.  On the Pb1550, the SMBus is PSC2, which
	 * uses a shared clock with USB.  This has been already
	 * configured by Yamon as a 48MHz clock, close enough
	 * for our work.
	 */
        if (i2c_au1550_add_bus(&pb1550_board_adapter) < 0) {
		printk("failed to initialize.\n");
                return -ENODEV;
	}

	printk("initialized.\n");
	return 0;
}

static void __exit
i2c_au1550_exit(void)
{
	i2c_au1550_del_bus(&pb1550_board_adapter);
}

MODULE_AUTHOR("Dan Malek, Embedded Edge, LLC.");
MODULE_DESCRIPTION("SMBus adapter Alchemy pb1550");
MODULE_LICENSE("GPL");

module_init (i2c_au1550_init);
module_exit (i2c_au1550_exit);
