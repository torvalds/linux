/*

Copyright 1996,2002 Gregory D. Hager, Alfred A. Rizzi, Noah J. Cowan,
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

  Date     Programmer  Description of changes made
  -------------------------------------------------------------------
  24-Jul-2002 SS       GPL licence.

*/

/* This code is a modified version of examples provided by Data Translations.*/

#ifndef DT3155_IO_INC
#define DT3155_IO_INC

/* macros to access registers */

#define WriteMReg(Address, Data)	(*((u_long *)(Address)) = Data)
#define ReadMReg(Address, Data)		(Data = *((u_long *)(Address)))

/***************** 32 bit register globals  **************/

/*  offsets for 32-bit memory mapped registers */

#define EVEN_DMA_START		0x000
#define ODD_DMA_START		0x00C
#define EVEN_DMA_STRIDE		0x018
#define ODD_DMA_STRIDE		0x024
#define EVEN_PIXEL_FMT		0x030
#define ODD_PIXEL_FMT		0x034
#define FIFO_TRIGGER		0x038
#define XFER_MODE		0x03C
#define CSR1			0x040
#define RETRY_WAIT_CNT		0x044
#define INT_CSR			0x048
#define EVEN_FLD_MASK		0x04C
#define ODD_FLD_MASK		0x050
#define MASK_LENGTH		0x054
#define FIFO_FLAG_CNT		0x058
#define IIC_CLK_DUR		0x05C
#define IIC_CSR1		0x060
#define IIC_CSR2		0x064
#define EVEN_DMA_UPPR_LMT	0x08C
#define ODD_DMA_UPPR_LMT	0x090

#define CLK_DUR_VAL		0x01010101



/******** Assignments and Typedefs for 32 bit Memory Mapped Registers ********/

typedef union fifo_trigger_tag {
	u_long   reg;
	struct {
		u_long PACKED:6;
		u_long       :9;
		u_long PLANER:7;
		u_long       :9;
	} fld;
} FIFO_TRIGGER_R;

typedef union xfer_mode_tag {
	u_long   reg;
	struct {
		u_long             :2;
		u_long FIELD_TOGGLE:1;
		u_long             :5;
		u_long             :2;
		u_long             :22;
	} fld;
} XFER_MODE_R;

typedef union csr1_tag {
	u_long   reg;
	struct {
		u_long CAP_CONT_EVE:1;
		u_long CAP_CONT_ODD:1;
		u_long CAP_SNGL_EVE:1;
		u_long CAP_SNGL_ODD:1;
		u_long FLD_DN_EVE  :1;
		u_long FLD_DN_ODD  :1;
		u_long SRST        :1;
		u_long FIFO_EN     :1;
		u_long FLD_CRPT_EVE:1;
		u_long FLD_CRPT_ODD:1;
		u_long ADDR_ERR_EVE:1;
		u_long ADDR_ERR_ODD:1;
		u_long CRPT_DIS    :1;
		u_long RANGE_EN    :1;
		u_long             :16;
	} fld;
} CSR1_R;

typedef union retry_wait_cnt_tag {
	u_long   reg;
	struct {
		u_long RTRY_WAIT_CNT:8;
		u_long              :24;
	} fld;
} RETRY_WAIT_CNT_R;

typedef union int_csr_tag {
	u_long   reg;
	struct {
		u_long FLD_END_EVE   :1;
		u_long FLD_END_ODD   :1;
		u_long FLD_START     :1;
		u_long               :5;
		u_long FLD_END_EVE_EN:1;
		u_long FLD_END_ODD_EN:1;
		u_long FLD_START_EN  :1;
		u_long               :21;
	} fld;
} INT_CSR_R;

typedef union mask_length_tag {
	u_long   reg;
	struct {
		u_long MASK_LEN_EVE:5;
		u_long             :11;
		u_long MASK_LEN_ODD:5;
		u_long             :11;
	} fld;
} MASK_LENGTH_R;

typedef union fifo_flag_cnt_tag {
	u_long   reg;
	struct {
		u_long AF_COUNT:7;
		u_long         :9;
		u_long AE_COUNT:7;
		u_long         :9;
	} fld;
} FIFO_FLAG_CNT_R;

typedef union iic_clk_dur {
	u_long   reg;
	struct {
		u_long PHASE_1:8;
		u_long PHASE_2:8;
		u_long PHASE_3:8;
		u_long PHASE_4:8;
	} fld;
} IIC_CLK_DUR_R;

typedef union iic_csr1_tag {
	u_long   reg;
	struct {
		u_long AUTO_EN     :1;
		u_long BYPASS      :1;
		u_long SDA_OUT     :1;
		u_long SCL_OUT     :1;
		u_long             :4;
		u_long AUTO_ABORT  :1;
		u_long DIRECT_ABORT:1;
		u_long SDA_IN      :1;
		u_long SCL_IN      :1;
		u_long             :4;
		u_long AUTO_ADDR   :8;
		u_long RD_DATA     :8;
	} fld;
} IIC_CSR1_R;

/**********************************
 * iic_csr2_tag
 */
typedef union iic_csr2_tag {
	u_long   reg;
	struct {
		u_long DIR_WR_DATA :8;
		u_long DIR_SUB_ADDR:8;
		u_long DIR_RD      :1;
		u_long DIR_ADDR    :7;
		u_long NEW_CYCLE   :1;
		u_long             :7;
	} fld;
}  IIC_CSR2_R;

/* use for both EVEN and ODD DMA UPPER LIMITS */

/*
 * dma_upper_lmt_tag
 */
typedef union dma_upper_lmt_tag   {
	u_long reg;
	struct {
		u_long DMA_UPPER_LMT_VAL:24;
		u_long                  :8;
	} fld;
} DMA_UPPER_LMT_R;


/*
 * Global declarations of local copies of boards' 32 bit registers
 */
extern u_long even_dma_start_r;		/*  bit 0 should always be 0 */
extern u_long odd_dma_start_r;		/*               ..          */
extern u_long even_dma_stride_r;	/*  bits 0&1 should always be 0 */
extern u_long odd_dma_stride_r;		/*               ..             */
extern u_long even_pixel_fmt_r;
extern u_long odd_pixel_fmt_r;

extern FIFO_TRIGGER_R		fifo_trigger_r;
extern XFER_MODE_R		xfer_mode_r;
extern CSR1_R			csr1_r;
extern RETRY_WAIT_CNT_R		retry_wait_cnt_r;
extern INT_CSR_R		int_csr_r;

extern u_long even_fld_mask_r;
extern u_long odd_fld_mask_r;

extern MASK_LENGTH_R		mask_length_r;
extern FIFO_FLAG_CNT_R		fifo_flag_cnt_r;
extern IIC_CLK_DUR_R		iic_clk_dur_r;
extern IIC_CSR1_R		iic_csr1_r;
extern IIC_CSR2_R		iic_csr2_r;
extern DMA_UPPER_LMT_R		even_dma_upper_lmt_r;
extern DMA_UPPER_LMT_R		odd_dma_upper_lmt_r;



/***************** 8 bit I2C register globals  ***********/
#define CSR2		0x010	/* indices of 8-bit I2C mapped reg's*/
#define EVEN_CSR	0x011
#define ODD_CSR		0x012
#define CONFIG		0x013
#define DT_ID		0x01F
#define X_CLIP_START	0x020
#define Y_CLIP_START	0x022
#define X_CLIP_END	0x024
#define Y_CLIP_END	0x026
#define AD_ADDR		0x030
#define AD_LUT		0x031
#define AD_CMD		0x032
#define DIG_OUT		0x040
#define PM_LUT_ADDR	0x050
#define PM_LUT_DATA	0x051


/******** Assignments and Typedefs for 8 bit I2C Registers********************/

typedef union i2c_csr2_tag {
	u_char reg;
	struct {
		u_char CHROM_FIL:1;
		u_char SYNC_SNTL:1;
		u_char HZ50:1;
		u_char SYNC_PRESENT:1;
		u_char BUSY_EVE:1;
		u_char BUSY_ODD:1;
		u_char DISP_PASS:1;
	} fld;
} I2C_CSR2;

typedef union i2c_even_csr_tag {
	u_char    reg;
	struct {
		u_char DONE_EVE :1;
		u_char SNGL_EVE :1;
		u_char ERROR_EVE:1;
		u_char          :5;
	} fld;
} I2C_EVEN_CSR;

typedef union i2c_odd_csr_tag {
	u_char reg;
	struct {
		u_char DONE_ODD:1;
		u_char SNGL_ODD:1;
		u_char ERROR_ODD:1;
		u_char :5;
	} fld;
} I2C_ODD_CSR;

typedef union i2c_config_tag {
	u_char reg;
	struct {
		u_char ACQ_MODE:2;
		u_char EXT_TRIG_EN:1;
		u_char EXT_TRIG_POL:1;
		u_char H_SCALE:1;
		u_char CLIP:1;
		u_char PM_LUT_SEL:1;
		u_char PM_LUT_PGM:1;
	} fld;
} I2C_CONFIG;


typedef union i2c_ad_cmd_tag {
	/* bits can have 3 different meanings depending on value of AD_ADDR */
	u_char reg;
	/* Bt252 Command Register if AD_ADDR = 00h */
	struct {
		u_char             :2;
		u_char SYNC_LVL_SEL:2;
		u_char SYNC_CNL_SEL:2;
		u_char DIGITIZE_CNL_SEL1:2;
		} bt252_command;

	/* Bt252 IOUT0 register if AD_ADDR = 01h */
	struct {
		u_char IOUT_DATA:8;
	} bt252_iout0;

	/* BT252 IOUT1 register if AD_ADDR = 02h */
	struct {
		u_char IOUT_DATA:8;
	} bt252_iout1;
} I2C_AD_CMD;


/***** Global declarations of local copies of boards' 8 bit I2C registers ***/

extern I2C_CSR2			i2c_csr2;
extern I2C_EVEN_CSR		i2c_even_csr;
extern I2C_ODD_CSR		i2c_odd_csr;
extern I2C_CONFIG		i2c_config;
extern u_char			i2c_dt_id;
extern u_char			i2c_x_clip_start;
extern u_char			i2c_y_clip_start;
extern u_char			i2c_x_clip_end;
extern u_char			i2c_y_clip_end;
extern u_char			i2c_ad_addr;
extern u_char			i2c_ad_lut;
extern I2C_AD_CMD		i2c_ad_cmd;
extern u_char			i2c_dig_out;
extern u_char			i2c_pm_lut_addr;
extern u_char			i2c_pm_lut_data;

/* Functions for Global use */

/* access 8-bit IIC registers */

extern int ReadI2C(u_char *lpReg, u_short wIregIndex, u_char *byVal);
extern int WriteI2C(u_char *lpReg, u_short wIregIndex, u_char byVal);

#endif
