/* ------------------------------------------------------------------------- */
/* i2c-algo-sibyte.c i2c driver algorithms for bit-shift adapters		     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 2001,2002,2003 Broadcom Corporation
     Copyright (C) 1995-2000 Simon G. Vogl

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi> and even
   Frodo Looijaard <frodol@dds.nl>.  */

/* Ported for SiByte SOCs by Broadcom Corporation.  */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_smbus.h>

#include <linux/i2c.h>
#include <linux/i2c-algo-sibyte.h>

/* ----- global defines ----------------------------------------------- */
#define SMB_CSR(a,r) ((long)(a->reg_base + r))

/* ----- global variables ---------------------------------------------	*/

/* module parameters:
 */
static int bit_scan=0;	/* have a look at what's hanging 'round		*/


static int smbus_xfer(struct i2c_adapter *i2c_adap, u16 addr, 
                      unsigned short flags, char read_write,
                      u8 command, int size, union i2c_smbus_data * data)
{
	struct i2c_algo_sibyte_data *adap = i2c_adap->algo_data;
        int data_bytes = 0;
        int error;

        while (csr_in32(SMB_CSR(adap, R_SMB_STATUS)) & M_SMB_BUSY)
                ;

        switch (size) {
        case I2C_SMBUS_QUICK:
                csr_out32((V_SMB_ADDR(addr) | (read_write == I2C_SMBUS_READ ? M_SMB_QDATA : 0) |
			   V_SMB_TT_QUICKCMD), SMB_CSR(adap, R_SMB_START));
                break;
        case I2C_SMBUS_BYTE:
                if (read_write == I2C_SMBUS_READ) {
                        csr_out32((V_SMB_ADDR(addr) | V_SMB_TT_RD1BYTE),
				  SMB_CSR(adap, R_SMB_START));
                        data_bytes = 1;
                } else {
                        csr_out32(V_SMB_CMD(command), SMB_CSR(adap, R_SMB_CMD));
                        csr_out32((V_SMB_ADDR(addr) | V_SMB_TT_WR1BYTE),
				  SMB_CSR(adap, R_SMB_START));
                }
                break;
        case I2C_SMBUS_BYTE_DATA:
                csr_out32(V_SMB_CMD(command), SMB_CSR(adap, R_SMB_CMD));
                if (read_write == I2C_SMBUS_READ) {
                        csr_out32((V_SMB_ADDR(addr) | V_SMB_TT_CMD_RD1BYTE),
				  SMB_CSR(adap, R_SMB_START));
                        data_bytes = 1;
                } else {
                        csr_out32(V_SMB_LB(data->byte), SMB_CSR(adap, R_SMB_DATA));
                        csr_out32((V_SMB_ADDR(addr) | V_SMB_TT_WR2BYTE),
				  SMB_CSR(adap, R_SMB_START));
                }
                break;
        case I2C_SMBUS_WORD_DATA:
                csr_out32(V_SMB_CMD(command), SMB_CSR(adap, R_SMB_CMD));
                if (read_write == I2C_SMBUS_READ) {
                        csr_out32((V_SMB_ADDR(addr) | V_SMB_TT_CMD_RD2BYTE),
				  SMB_CSR(adap, R_SMB_START));
                        data_bytes = 2;
                } else {
                        csr_out32(V_SMB_LB(data->word & 0xff), SMB_CSR(adap, R_SMB_DATA));
                        csr_out32(V_SMB_MB(data->word >> 8), SMB_CSR(adap, R_SMB_DATA));
                        csr_out32((V_SMB_ADDR(addr) | V_SMB_TT_WR2BYTE),
				  SMB_CSR(adap, R_SMB_START));
                }
                break;
        default:
                return -1;      /* XXXKW better error code? */
        }

        while (csr_in32(SMB_CSR(adap, R_SMB_STATUS)) & M_SMB_BUSY)
                ;

        error = csr_in32(SMB_CSR(adap, R_SMB_STATUS));
        if (error & M_SMB_ERROR) {
                /* Clear error bit by writing a 1 */
                csr_out32(M_SMB_ERROR, SMB_CSR(adap, R_SMB_STATUS));
                return -1;      /* XXXKW better error code? */
        }

        if (data_bytes == 1)
                data->byte = csr_in32(SMB_CSR(adap, R_SMB_DATA)) & 0xff;
        if (data_bytes == 2)
                data->word = csr_in32(SMB_CSR(adap, R_SMB_DATA)) & 0xffff;

        return 0;
}

static int algo_control(struct i2c_adapter *adapter, 
	unsigned int cmd, unsigned long arg)
{
	return 0;
}

static u32 bit_func(struct i2c_adapter *adap)
{
	return (I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
                I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA);
}


/* -----exported algorithm data: -------------------------------------	*/

static struct i2c_algorithm i2c_sibyte_algo = {
	.smbus_xfer	= smbus_xfer,
	.algo_control	= algo_control, /* ioctl */
	.functionality	= bit_func,
};

/* 
 * registering functions to load algorithms at runtime 
 */
int i2c_sibyte_add_bus(struct i2c_adapter *i2c_adap, int speed)
{
	int i;
	struct i2c_algo_sibyte_data *adap = i2c_adap->algo_data;

	/* register new adapter to i2c module... */
	i2c_adap->algo = &i2c_sibyte_algo;
        
        /* Set the frequency to 100 kHz */
        csr_out32(speed, SMB_CSR(adap,R_SMB_FREQ));
        csr_out32(0, SMB_CSR(adap,R_SMB_CONTROL));

	/* scan bus */
	if (bit_scan) {
                union i2c_smbus_data data;
                int rc;
		printk(KERN_INFO " i2c-algo-sibyte.o: scanning bus %s.\n",
		       i2c_adap->name);
		for (i = 0x00; i < 0x7f; i++) {
                        /* XXXKW is this a realistic probe? */
                        rc = smbus_xfer(i2c_adap, i, 0, I2C_SMBUS_READ, 0,
                                        I2C_SMBUS_BYTE_DATA, &data);
			if (!rc) {
				printk("(%02x)",i); 
			} else 
				printk("."); 
		}
		printk("\n");
	}

	i2c_add_adapter(i2c_adap);

	return 0;
}


int i2c_sibyte_del_bus(struct i2c_adapter *adap)
{
	int res;

	if ((res = i2c_del_adapter(adap)) < 0)
		return res;

	return 0;
}

int __init i2c_algo_sibyte_init (void)
{
	printk("i2c-algo-sibyte.o: i2c SiByte algorithm module\n");
	return 0;
}


EXPORT_SYMBOL(i2c_sibyte_add_bus);
EXPORT_SYMBOL(i2c_sibyte_del_bus);

#ifdef MODULE
MODULE_AUTHOR("Kip Walker, Broadcom Corp.");
MODULE_DESCRIPTION("SiByte I2C-Bus algorithm");
MODULE_PARM(bit_scan, "i");
MODULE_PARM_DESC(bit_scan, "Scan for active chips on the bus");
MODULE_LICENSE("GPL");

int init_module(void) 
{
	return i2c_algo_sibyte_init();
}

void cleanup_module(void) 
{
}
#endif
