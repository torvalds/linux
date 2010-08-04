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
#include "dt3155.h"
#include "dt3155_io.h"
#include "dt3155_drv.h"


/****** local copies of board's 32 bit registers ******/
u32 even_dma_start_r;	/*  bit 0 should always be 0 */
u32 odd_dma_start_r;	/*               .. */
u32 even_dma_stride_r;	/*  bits 0&1 should always be 0 */
u32 odd_dma_stride_r;	/*               .. */
u32 even_pixel_fmt_r;
u32 odd_pixel_fmt_r;

FIFO_TRIGGER_R		fifo_trigger_r;
XFER_MODE_R		xfer_mode_r;
CSR1_R			csr1_r;
RETRY_WAIT_CNT_R	retry_wait_cnt_r;
INT_CSR_R		int_csr_r;

u32 even_fld_mask_r;
u32 odd_fld_mask_r;

MASK_LENGTH_R		mask_length_r;
FIFO_FLAG_CNT_R		fifo_flag_cnt_r;
IIC_CLK_DUR_R		iic_clk_dur_r;
IIC_CSR1_R		iic_csr1_r;
IIC_CSR2_R		iic_csr2_r;
DMA_UPPER_LMT_R		even_dma_upper_lmt_r;
DMA_UPPER_LMT_R		odd_dma_upper_lmt_r;



/******** local copies of board's 8 bit I2C registers ******/
I2C_CSR2 i2c_csr2;
I2C_EVEN_CSR i2c_even_csr;
I2C_ODD_CSR i2c_odd_csr;
I2C_CONFIG i2c_config;
u8 i2c_dt_id;
u8 i2c_x_clip_start;
u8 i2c_y_clip_start;
u8 i2c_x_clip_end;
u8 i2c_y_clip_end;
u8 i2c_ad_addr;
u8 i2c_ad_lut;
I2C_AD_CMD i2c_ad_cmd;
u8 i2c_dig_out;
u8 i2c_pm_lut_addr;
u8 i2c_pm_lut_data;

/*
 * wait_ibsyclr()
 *
 * This function handles read/write timing and r/w timeout error
 */
static int wait_ibsyclr(u8 *lpReg)
{
	/* wait 100 microseconds */
	udelay(100L);
	/* __delay(loops_per_sec/10000); */

	ReadMReg(lpReg + IIC_CSR2, iic_csr2_r.reg);
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
int WriteI2C(u8 *lpReg, u_short wIregIndex, u8 byVal)
{
	/* read 32 bit IIC_CSR2 register data into union */

	ReadMReg((lpReg + IIC_CSR2), iic_csr2_r.reg);

	/* for write operation */
	iic_csr2_r.fld.DIR_RD      = 0;
	/* I2C address of I2C register: */
	iic_csr2_r.fld.DIR_ADDR    = wIregIndex;
	/* 8 bit data to be written to I2C reg */
	iic_csr2_r.fld.DIR_WR_DATA = byVal;
	/* will start a direct I2C cycle: */
	iic_csr2_r.fld.NEW_CYCLE   = 1;

	/* xfer union data into 32 bit IIC_CSR2 register */
	WriteMReg((lpReg + IIC_CSR2), iic_csr2_r.reg);

	/* wait for IIC cycle to finish */
	return wait_ibsyclr(lpReg);
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
int ReadI2C(u8 *lpReg, u_short wIregIndex, u8 *byVal)
{
	int writestat;	/* status for return */

	/*  read 32 bit IIC_CSR2 register data into union */
	ReadMReg((lpReg + IIC_CSR2), iic_csr2_r.reg);

	/*  for read operation */
	iic_csr2_r.fld.DIR_RD     = 1;

	/*  I2C address of I2C register: */
	iic_csr2_r.fld.DIR_ADDR   = wIregIndex;

	/*  will start a direct I2C cycle: */
	iic_csr2_r.fld.NEW_CYCLE  = 1;

	/*  xfer union's data into 32 bit IIC_CSR2 register */
	WriteMReg((lpReg + IIC_CSR2), iic_csr2_r.reg);

	/* wait for IIC cycle to finish */
	writestat = wait_ibsyclr(lpReg);

	/* Next 2 commands read 32 bit IIC_CSR1 register's data into union */
	/* first read data is in IIC_CSR1 */
	ReadMReg((lpReg + IIC_CSR1), iic_csr1_r.reg);

	/* now get data u8 out of register */
	*byVal = (u8) iic_csr1_r.fld.RD_DATA;

	return writestat;
}
