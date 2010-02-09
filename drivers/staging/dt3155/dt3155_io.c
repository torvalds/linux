/*

Copyright 1996,2002,2005 Gregory D. Hager, Alfred A. Rizzi, Noah J. Cowan,
                         Jason Lapenta, Scott Smedley

This file is part of the DT3155 Device Driver.

The DT3155 Device Driver is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The DT3155 Device Driver is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the DT3155 Device Driver; if not, write to the Free
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
MA 02111-1307 USA


-- Changes --

  Date     Programmer   Description of changes made
  -------------------------------------------------------------------
  10-Oct-2001 SS       port to 2.4 kernel.
  24-Jul-2002 SS       GPL licence.
  26-Jul-2002 SS       Bug fix: timing logic was wrong.
  08-Aug-2005 SS       port to 2.6 kernel.

*/

/* This file provides some basic register io routines.  It is modified
   from demo code provided by Data Translations. */

#ifdef __KERNEL__
#include <asm/delay.h>
#endif

#if 0
#include <sys/param.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "dt3155.h"
#include "dt3155_io.h"
#include "dt3155_drv.h"

#ifndef __KERNEL__
#include <stdio.h>
#endif


/****** local copies of board's 32 bit registers ******/
u64            even_dma_start_r;     /*  bit 0 should always be 0 */
u64            odd_dma_start_r;      /*               .. */
u64            even_dma_stride_r;    /*  bits 0&1 should always be 0 */
u64            odd_dma_stride_r;     /*               .. */
u64            even_pixel_fmt_r;
u64            odd_pixel_fmt_r;

FIFO_TRIGGER_R      fifo_trigger_r;
XFER_MODE_R         xfer_mode_r;
CSR1_R              csr1_r;
RETRY_WAIT_CNT_R    retry_wait_cnt_r;
INT_CSR_R           int_csr_r;

u64              even_fld_mask_r;
u64              odd_fld_mask_r;

MASK_LENGTH_R       mask_length_r;
FIFO_FLAG_CNT_R     fifo_flag_cnt_r;
IIC_CLK_DUR_R       iic_clk_dur_r;
IIC_CSR1_R          iic_csr1_r;
IIC_CSR2_R          iic_csr2_r;
DMA_UPPER_LMT_R     even_dma_upper_lmt_r;
DMA_UPPER_LMT_R     odd_dma_upper_lmt_r;



/******** local copies of board's 8 bit I2C registers ******/
I2C_CSR2               i2c_csr2;
I2C_EVEN_CSR           i2c_even_csr;
I2C_ODD_CSR            i2c_odd_csr;
I2C_CONFIG             i2c_config;
u8                 i2c_dt_id;
u8                 i2c_x_clip_start;
u8                 i2c_y_clip_start;
u8                 i2c_x_clip_end;
u8                 i2c_y_clip_end;
u8                 i2c_ad_addr;
u8                 i2c_ad_lut;
I2C_AD_CMD             i2c_ad_cmd;
u8                 i2c_dig_out;
u8                 i2c_pm_lut_addr;
u8                 i2c_pm_lut_data;


// return the time difference (in microseconds) b/w <a> & <b>.
long elapsed2 (const struct timeval *pStart, const struct timeval *pEnd)
{
	long i = (pEnd->tv_sec - pStart->tv_sec) * 1000000;
	i += pEnd->tv_usec - pStart->tv_usec;
	return i;
}

/***********************************************************************
 wait_ibsyclr()

 This function handles read/write timing and r/w timeout error

     Returns TRUE  if NEW_CYCLE clears
     Returns FALSE if NEW_CYCLE doesn't clear in roughly 3 msecs,
             otherwise returns 0

***********************************************************************/
int wait_ibsyclr(u8 * lpReg)
{
  /* wait 100 microseconds */

#ifdef __KERNEL__
  udelay(100L);
  /*    __delay(loops_per_sec/10000); */
  if (iic_csr2_r.fld.NEW_CYCLE )
    { /*  if NEW_CYCLE didn't clear */
      /*  TIMEOUT ERROR */
      dt3155_errno = DT_ERR_I2C_TIMEOUT;
      return FALSE;
    }
  else
    return TRUE;        /*  no  error */
#else
  struct timeval StartTime;
  struct timeval EndTime;

  const int to_3ms = 3000;  /* time out of 3ms = 3000us */

  gettimeofday( &StartTime, NULL );
  do {
    /* get new iic_csr2 value: */
    ReadMReg((lpReg + IIC_CSR2), iic_csr2_r.reg);
    gettimeofday( &EndTime, NULL );
  }
  while ((elapsed2(&StartTime, &EndTime) < to_3ms) && iic_csr2_r.fld.NEW_CYCLE);

  if (iic_csr2_r.fld.NEW_CYCLE )
    { /*  if NEW_CYCLE didn't clear */
      printf("Timed out waiting for NEW_CYCLE to clear!");
      return FALSE;
    }
  else
    return TRUE;        /*  no  error */
#endif
}

/***********************************************************************
 WriteI2C()

 This function handles writing to 8-bit DT3155 registers

   1st parameter is pointer to 32-bit register base address
   2nd parameter is reg. index;
   3rd is value to be written

   Returns    TRUE   -  Successful completion
              FALSE  -  Timeout error - cycle did not complete!
***********************************************************************/
int WriteI2C (u8 * lpReg, u_short wIregIndex, u8 byVal)
{
    int writestat;     /* status for return */

    /*  read 32 bit IIC_CSR2 register data into union */

    ReadMReg((lpReg + IIC_CSR2), iic_csr2_r.reg);

    iic_csr2_r.fld.DIR_RD    = 0;           /*  for write operation */
    iic_csr2_r.fld.DIR_ADDR  = wIregIndex;  /*  I2C address of I2C register: */
    iic_csr2_r.fld.DIR_WR_DATA = byVal;     /*  8 bit data to be written to I2C reg */
    iic_csr2_r.fld.NEW_CYCLE   = 1;         /*  will start a direct I2C cycle: */

    /*  xfer union data into 32 bit IIC_CSR2 register */

    WriteMReg((lpReg + IIC_CSR2), iic_csr2_r.reg);

    /* wait for IIC cycle to finish */

    writestat = wait_ibsyclr( lpReg );
    return writestat;                  /* return with status */
}

/***********************************************************************
 ReadI2C()

 This function handles reading from 8-bit DT3155 registers

   1st parameter is pointer to 32-bit register base address
   2nd parameter is reg. index;
   3rd is adrs of value to be read

   Returns    TRUE   -  Successful completion
              FALSE  -  Timeout error - cycle did not complete!
***********************************************************************/
int ReadI2C (u8 * lpReg, u_short wIregIndex, u8 * byVal)
{
  int writestat;     /* status for return */

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

  return writestat;   /*  return with status */
}
