/*
 * Copyright (C) 2004 Steven J. Hill
 * Copyright (C) 2001,2002,2003 Broadcom Corporation
 * Copyright (C) 1995-2000 Simon G. Vogl
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <asm/io.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_smbus.h>


struct i2c_algo_sibyte_data {
	void *data;		/* private data */
	int   bus;		/* which bus */
	void *reg_base;		/* CSR base */
};

/* ----- global defines ----------------------------------------------- */
#define SMB_CSR(a,r) ((long)(a->reg_base + r))

/* ----- global variables --------------------------------------------- */

/* module parameters:
 */
static int bit_scan;	/* have a look at what's hanging 'round */
module_param(bit_scan, int, 0);
MODULE_PARM_DESC(bit_scan, "Scan for active chips on the bus");


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
		csr_out32((V_SMB_ADDR(addr) |
			   (read_write == I2C_SMBUS_READ ? M_SMB_QDATA : 0) |
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
			csr_out32(V_SMB_LB(data->byte),
				  SMB_CSR(adap, R_SMB_DATA));
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
			csr_out32(V_SMB_LB(data->word & 0xff),
				  SMB_CSR(adap, R_SMB_DATA));
			csr_out32(V_SMB_MB(data->word >> 8),
				  SMB_CSR(adap, R_SMB_DATA));
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

static u32 bit_func(struct i2c_adapter *adap)
{
	return (I2C_FUNC_SMBUS_QUICK | I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA);
}


/* -----exported algorithm data: -------------------------------------	*/

static const struct i2c_algorithm i2c_sibyte_algo = {
	.smbus_xfer	= smbus_xfer,
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

	return i2c_add_adapter(i2c_adap);
}


static struct i2c_algo_sibyte_data sibyte_board_data[2] = {
	{ NULL, 0, (void *) (CKSEG1+A_SMB_BASE(0)) },
	{ NULL, 1, (void *) (CKSEG1+A_SMB_BASE(1)) }
};

static struct i2c_adapter sibyte_board_adapter[2] = {
	{
		.owner		= THIS_MODULE,
		.id		= I2C_HW_SIBYTE,
		.class		= I2C_CLASS_HWMON,
		.algo		= NULL,
		.algo_data	= &sibyte_board_data[0],
		.name		= "SiByte SMBus 0",
	},
	{
		.owner		= THIS_MODULE,
		.id		= I2C_HW_SIBYTE,
		.class		= I2C_CLASS_HWMON,
		.algo		= NULL,
		.algo_data	= &sibyte_board_data[1],
		.name		= "SiByte SMBus 1",
	},
};

static int __init i2c_sibyte_init(void)
{
	printk("i2c-swarm.o: i2c SMBus adapter module for SiByte board\n");
	if (i2c_sibyte_add_bus(&sibyte_board_adapter[0], K_SMB_FREQ_100KHZ) < 0)
		return -ENODEV;
	if (i2c_sibyte_add_bus(&sibyte_board_adapter[1], K_SMB_FREQ_400KHZ) < 0)
		return -ENODEV;
	return 0;
}

static void __exit i2c_sibyte_exit(void)
{
	i2c_del_adapter(&sibyte_board_adapter[0]);
	i2c_del_adapter(&sibyte_board_adapter[1]);
}

module_init(i2c_sibyte_init);
module_exit(i2c_sibyte_exit);

MODULE_AUTHOR("Kip Walker (Broadcom Corp.), Steven J. Hill <sjhill@realitydiluted.com>");
MODULE_DESCRIPTION("SMBus adapter routines for SiByte boards");
MODULE_LICENSE("GPL");
