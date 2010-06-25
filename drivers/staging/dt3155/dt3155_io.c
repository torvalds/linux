/*
 * Copyright 1996,2002,2005 Gregory D. Hager, Alfred A. Rizzi, Noah J. Cowan,
 *			    Jason Lapenta, Scott Smedley
 *
 * This file is part of the DT3155 Device Driver.
 *
 * The DT3155 Device Driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * The DT3155 Device Driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 */

/*
 * This file provides some basic register io routines.  It is modified from
 * demo code provided by Data Translations.
 */

#include <linux/delay.h>
#include <linux/io.h>

#include "dt3155.h"
#include "dt3155_io.h"
#include "dt3155_drv.h"


/*
 * wait_ibsyclr()
 *
 * This function handles read/write timing and r/w timeout error
 */
static int wait_ibsyclr(void __iomem *mmio)
{
	IIC_CSR2_R iic_csr2_r;

	/* wait 100 microseconds */
	udelay(100L);
	/* __delay(loops_per_sec/10000); */

	iic_csr2_r.reg = readl(mmio + IIC_CSR2);
	if (iic_csr2_r.fld.NEW_CYCLE) {
		/* if NEW_CYCLE didn't clear */
		/* TIMEOUT ERROR */
		dt3155_errno = DT_ERR_I2C_TIMEOUT;
		return -ETIMEDOUT;
	}

	return 0;	/* no error */
}

/*
 * WriteI2C()
 *
 * This function handles writing to 8-bit DT3155 registers
 *
 * 1st parameter is pointer to 32-bit register base address
 * 2nd parameter is reg. index;
 * 3rd is value to be written
 */
int WriteI2C(void __iomem *mmio, u_short wIregIndex, u8 byVal)
{
	IIC_CSR2_R iic_csr2_r;

	/* read 32 bit IIC_CSR2 register data into union */
	iic_csr2_r.reg = readl(mmio + IIC_CSR2);

	/* for write operation */
	iic_csr2_r.fld.DIR_RD      = 0;
	/* I2C address of I2C register: */
	iic_csr2_r.fld.DIR_ADDR    = wIregIndex;
	/* 8 bit data to be written to I2C reg */
	iic_csr2_r.fld.DIR_WR_DATA = byVal;
	/* will start a direct I2C cycle: */
	iic_csr2_r.fld.NEW_CYCLE   = 1;

	/* xfer union data into 32 bit IIC_CSR2 register */
	writel(iic_csr2_r.reg, mmio + IIC_CSR2);

	/* wait for IIC cycle to finish */
	return wait_ibsyclr(mmio);
}

/*
 * ReadI2C()
 *
 * This function handles reading from 8-bit DT3155 registers
 *
 * 1st parameter is pointer to 32-bit register base address
 * 2nd parameter is reg. index;
 * 3rd is adrs of value to be read
 */
int ReadI2C(void __iomem *mmio, u_short wIregIndex, u8 *byVal)
{
	IIC_CSR1_R iic_csr1_r;
	IIC_CSR2_R iic_csr2_r;
	int writestat;	/* status for return */

	/*  read 32 bit IIC_CSR2 register data into union */
	iic_csr2_r.reg = readl(mmio + IIC_CSR2);

	/*  for read operation */
	iic_csr2_r.fld.DIR_RD     = 1;

	/*  I2C address of I2C register: */
	iic_csr2_r.fld.DIR_ADDR   = wIregIndex;

	/*  will start a direct I2C cycle: */
	iic_csr2_r.fld.NEW_CYCLE  = 1;

	/*  xfer union's data into 32 bit IIC_CSR2 register */
	writel(iic_csr2_r.reg, mmio + IIC_CSR2);

	/* wait for IIC cycle to finish */
	writestat = wait_ibsyclr(mmio);

	/* Next 2 commands read 32 bit IIC_CSR1 register's data into union */
	/* first read data is in IIC_CSR1 */
	iic_csr1_r.reg = readl(mmio + IIC_CSR1);

	/* now get data u8 out of register */
	*byVal = (u8) iic_csr1_r.fld.RD_DATA;

	return writestat;
}
