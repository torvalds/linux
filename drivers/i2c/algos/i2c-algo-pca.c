/*
 *  i2c-algo-pca.c i2c driver algorithms for PCA9564 adapters                
 *    Copyright (C) 2004 Arcom Control Systems
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
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-pca.h>
#include "i2c-algo-pca.h"

#define DRIVER "i2c-algo-pca"

#define DEB1(fmt, args...) do { if (i2c_debug>=1) printk(fmt, ## args); } while(0)
#define DEB2(fmt, args...) do { if (i2c_debug>=2) printk(fmt, ## args); } while(0)
#define DEB3(fmt, args...) do { if (i2c_debug>=3) printk(fmt, ## args); } while(0)

static int i2c_debug=0;

#define pca_outw(adap, reg, val) adap->write_byte(adap, reg, val)
#define pca_inw(adap, reg) adap->read_byte(adap, reg)

#define pca_status(adap) pca_inw(adap, I2C_PCA_STA)
#define pca_clock(adap) adap->get_clock(adap)
#define pca_own(adap) adap->get_own(adap)
#define pca_set_con(adap, val) pca_outw(adap, I2C_PCA_CON, val)
#define pca_get_con(adap) pca_inw(adap, I2C_PCA_CON)
#define pca_wait(adap) adap->wait_for_interrupt(adap)

/*
 * Generate a start condition on the i2c bus.
 *
 * returns after the start condition has occurred
 */
static void pca_start(struct i2c_algo_pca_data *adap)
{
	int sta = pca_get_con(adap);
	DEB2("=== START\n");
	sta |= I2C_PCA_CON_STA;
	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);
	pca_wait(adap);
}

/*
 * Generate a repeated start condition on the i2c bus
 *
 * return after the repeated start condition has occurred
 */
static void pca_repeated_start(struct i2c_algo_pca_data *adap)
{
	int sta = pca_get_con(adap);
	DEB2("=== REPEATED START\n");
	sta |= I2C_PCA_CON_STA;
	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);
	pca_wait(adap);
}

/*
 * Generate a stop condition on the i2c bus
 *
 * returns after the stop condition has been generated
 *
 * STOPs do not generate an interrupt or set the SI flag, since the
 * part returns the idle state (0xf8). Hence we don't need to
 * pca_wait here.
 */
static void pca_stop(struct i2c_algo_pca_data *adap)
{
	int sta = pca_get_con(adap);
	DEB2("=== STOP\n");
	sta |= I2C_PCA_CON_STO;
	sta &= ~(I2C_PCA_CON_STA|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);
}

/*
 * Send the slave address and R/W bit
 *
 * returns after the address has been sent
 */
static void pca_address(struct i2c_algo_pca_data *adap, 
			struct i2c_msg *msg)
{
	int sta = pca_get_con(adap);
	int addr;

	addr = ( (0x7f & msg->addr) << 1 );
	if (msg->flags & I2C_M_RD )
		addr |= 1;
	DEB2("=== SLAVE ADDRESS %#04x+%c=%#04x\n", 
	     msg->addr, msg->flags & I2C_M_RD ? 'R' : 'W', addr);
	
	pca_outw(adap, I2C_PCA_DAT, addr);

	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_STA|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);

	pca_wait(adap);
}

/*
 * Transmit a byte.
 *
 * Returns after the byte has been transmitted
 */
static void pca_tx_byte(struct i2c_algo_pca_data *adap, 
			__u8 b)
{
	int sta = pca_get_con(adap);
	DEB2("=== WRITE %#04x\n", b);
	pca_outw(adap, I2C_PCA_DAT, b);

	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_STA|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);

	pca_wait(adap);
}

/*
 * Receive a byte
 *
 * returns immediately.
 */
static void pca_rx_byte(struct i2c_algo_pca_data *adap, 
			__u8 *b, int ack)
{
	*b = pca_inw(adap, I2C_PCA_DAT);
	DEB2("=== READ %#04x %s\n", *b, ack ? "ACK" : "NACK");
}

/* 
 * Setup ACK or NACK for next received byte and wait for it to arrive.
 *
 * Returns after next byte has arrived.
 */
static void pca_rx_ack(struct i2c_algo_pca_data *adap, 
		       int ack)
{
	int sta = pca_get_con(adap);

	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_STA|I2C_PCA_CON_SI|I2C_PCA_CON_AA);

	if ( ack )
		sta |= I2C_PCA_CON_AA;

	pca_set_con(adap, sta);
	pca_wait(adap);
}

/* 
 * Reset the i2c bus / SIO 
 */
static void pca_reset(struct i2c_algo_pca_data *adap)
{
	/* apparently only an external reset will do it. not a lot can be done */
	printk(KERN_ERR DRIVER ": Haven't figured out how to do a reset yet\n");
}

static int pca_xfer(struct i2c_adapter *i2c_adap,
                    struct i2c_msg *msgs,
                    int num)
{
        struct i2c_algo_pca_data *adap = i2c_adap->algo_data;
        struct i2c_msg *msg = NULL;
        int curmsg;
	int numbytes = 0;
	int state;
	int ret;
	int timeout = 100;

	while ((state = pca_status(adap)) != 0xf8 && timeout--) {
		msleep(10);
	}
	if (state != 0xf8) {
		dev_dbg(&i2c_adap->dev, "bus is not idle. status is %#04x\n", state);
		return -EIO;
	}

	DEB1("{{{ XFER %d messages\n", num);

	if (i2c_debug>=2) {
		for (curmsg = 0; curmsg < num; curmsg++) {
			int addr, i;
			msg = &msgs[curmsg];
			
			addr = (0x7f & msg->addr) ;
		
			if (msg->flags & I2C_M_RD )
				printk(KERN_INFO "    [%02d] RD %d bytes from %#02x [%#02x, ...]\n", 
				       curmsg, msg->len, addr, (addr<<1) | 1);
			else {
				printk(KERN_INFO "    [%02d] WR %d bytes to %#02x [%#02x%s", 
				       curmsg, msg->len, addr, addr<<1,
				       msg->len == 0 ? "" : ", ");
				for(i=0; i < msg->len; i++)
					printk("%#04x%s", msg->buf[i], i == msg->len - 1 ? "" : ", ");
				printk("]\n");
			}
		}
	}

	curmsg = 0;
	ret = -EREMOTEIO;
	while (curmsg < num) {
		state = pca_status(adap);

		DEB3("STATE is 0x%02x\n", state);
		msg = &msgs[curmsg];

		switch (state) {
		case 0xf8: /* On reset or stop the bus is idle */
			pca_start(adap);
			break;

		case 0x08: /* A START condition has been transmitted */
		case 0x10: /* A repeated start condition has been transmitted */
			pca_address(adap, msg);
			break;
			
		case 0x18: /* SLA+W has been transmitted; ACK has been received */
		case 0x28: /* Data byte in I2CDAT has been transmitted; ACK has been received */
			if (numbytes < msg->len) {
				pca_tx_byte(adap, msg->buf[numbytes]);
				numbytes++;
				break;
			}
			curmsg++; numbytes = 0;
			if (curmsg == num)
				pca_stop(adap);
			else
				pca_repeated_start(adap);
			break;

		case 0x20: /* SLA+W has been transmitted; NOT ACK has been received */
			DEB2("NOT ACK received after SLA+W\n");
			pca_stop(adap);
			goto out;

		case 0x40: /* SLA+R has been transmitted; ACK has been received */
			pca_rx_ack(adap, msg->len > 1);
			break;

		case 0x50: /* Data bytes has been received; ACK has been returned */
			if (numbytes < msg->len) {
				pca_rx_byte(adap, &msg->buf[numbytes], 1);
				numbytes++;
				pca_rx_ack(adap, numbytes < msg->len - 1);
				break;
			}
			curmsg++; numbytes = 0;
			if (curmsg == num)
				pca_stop(adap);
			else
				pca_repeated_start(adap);
			break;

		case 0x48: /* SLA+R has been transmitted; NOT ACK has been received */
			DEB2("NOT ACK received after SLA+R\n");
			pca_stop(adap);
			goto out;

		case 0x30: /* Data byte in I2CDAT has been transmitted; NOT ACK has been received */
			DEB2("NOT ACK received after data byte\n");
			goto out;

		case 0x38: /* Arbitration lost during SLA+W, SLA+R or data bytes */
			DEB2("Arbitration lost\n");
			goto out;
			
		case 0x58: /* Data byte has been received; NOT ACK has been returned */
			if ( numbytes == msg->len - 1 ) {
				pca_rx_byte(adap, &msg->buf[numbytes], 0);
				curmsg++; numbytes = 0;
				if (curmsg == num)
					pca_stop(adap);
				else
					pca_repeated_start(adap);
			} else {
				DEB2("NOT ACK sent after data byte received. "
				     "Not final byte. numbytes %d. len %d\n",
				     numbytes, msg->len);
				pca_stop(adap);
				goto out;
			}
			break;
		case 0x70: /* Bus error - SDA stuck low */
			DEB2("BUS ERROR - SDA Stuck low\n");
			pca_reset(adap);
			goto out;
		case 0x90: /* Bus error - SCL stuck low */
			DEB2("BUS ERROR - SCL Stuck low\n");
			pca_reset(adap);
			goto out;
		case 0x00: /* Bus error during master or slave mode due to illegal START or STOP condition */
			DEB2("BUS ERROR - Illegal START or STOP\n");
			pca_reset(adap);
			goto out;
		default:
			printk(KERN_ERR DRIVER ": unhandled SIO state 0x%02x\n", state);
			break;
		}
		
	}

	ret = curmsg;
 out:
	DEB1(KERN_CRIT "}}} transfered %d/%d messages. "
	     "status is %#04x. control is %#04x\n", 
	     curmsg, num, pca_status(adap),
	     pca_get_con(adap));
	return ret;
}

static u32 pca_func(struct i2c_adapter *adap)
{
        return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static int pca_init(struct i2c_algo_pca_data *adap)
{
	static int freqs[] = {330,288,217,146,88,59,44,36};
	int own, clock;

	own = pca_own(adap);
	clock = pca_clock(adap);
	DEB1(KERN_INFO DRIVER ": own address is %#04x\n", own);
	DEB1(KERN_INFO DRIVER ": clock freqeuncy is %dkHz\n", freqs[clock]);

	pca_outw(adap, I2C_PCA_ADR, own << 1);

	pca_set_con(adap, I2C_PCA_CON_ENSIO | clock);
	udelay(500); /* 500 µs for oscilator to stabilise */

	return 0;
}

static struct i2c_algorithm pca_algo = {
	.master_xfer	= pca_xfer,
	.functionality	= pca_func,
};

/* 
 * registering functions to load algorithms at runtime 
 */
int i2c_pca_add_bus(struct i2c_adapter *adap)
{
	struct i2c_algo_pca_data *pca_adap = adap->algo_data;
	int rval;

	/* register new adapter to i2c module... */
	adap->algo = &pca_algo;

	adap->timeout = 100;		/* default values, should	*/
	adap->retries = 3;		/* be replaced by defines	*/

	rval = pca_init(pca_adap);

	if (!rval)
		i2c_add_adapter(adap);

	return rval;
}

int i2c_pca_del_bus(struct i2c_adapter *adap)
{
	return i2c_del_adapter(adap);
}

EXPORT_SYMBOL(i2c_pca_add_bus);
EXPORT_SYMBOL(i2c_pca_del_bus);

MODULE_AUTHOR("Ian Campbell <icampbell@arcom.com>");
MODULE_DESCRIPTION("I2C-Bus PCA9564 algorithm");
MODULE_LICENSE("GPL");

module_param(i2c_debug, int, 0);
