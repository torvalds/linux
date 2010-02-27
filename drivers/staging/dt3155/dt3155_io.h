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

#define WriteMReg(Address, Data)	(*((u32 *)(Address)) = Data)
#define ReadMReg(Address, Data)		(Data = *((u32 *)(Address)))

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
	u32   reg;
	struct {
		u32 PACKED:6;
		u32       :9;
		u32 PLANER:7;
		u32       :9;
	} fld;
} FIFO_TRIGGER_R;

typedef union xfer_mode_tag {
	u32   reg;
	struct {
		u32             :2;
		u32 FIELD_TOGGLE:1;
		u32             :5;
		u32             :2;
		u32             :22;
	} fld;
} XFER_MODE_R;

typedef union csr1_tag {
	u32   reg;
	struct {
		u32 CAP_CONT_EVE:1;
		u32 CAP_CONT_ODD:1;
		u32 CAP_SNGL_EVE:1;
		u32 CAP_SNGL_ODD:1;
		u32 FLD_DN_EVE  :1;
		u32 FLD_DN_ODD  :1;
		u32 SRST        :1;
		u32 FIFO_EN     :1;
		u32 FLD_CRPT_EVE:1;
		u32 FLD_CRPT_ODD:1;
		u32 ADDR_ERR_EVE:1;
		u32 ADDR_ERR_ODD:1;
		u32 CRPT_DIS    :1;
		u32 RANGE_EN    :1;
		u32             :16;
	} fld;
} CSR1_R;

typedef union retry_wait_cnt_tag {
	u32   reg;
	struct {
		u32 RTRY_WAIT_CNT:8;
		u32              :24;
	} fld;
} RETRY_WAIT_CNT_R;

typedef union int_csr_tag {
	u32   reg;
	struct {
		u32 FLD_END_EVE   :1;
		u32 FLD_END_ODD   :1;
		u32 FLD_START     :1;
		u32               :5;
		u32 FLD_END_EVE_EN:1;
		u32 FLD_END_ODD_EN:1;
		u32 FLD_START_EN  :1;
		u32               :21;
	} fld;
} INT_CSR_R;

typedef union mask_length_tag {
	u32   reg;
	struct {
		u32 MASK_LEN_EVE:5;
		u32             :11;
		u32 MASK_LEN_ODD:5;
		u32             :11;
	} fld;
} MASK_LENGTH_R;

typedef union fifo_flag_cnt_tag {
	u32   reg;
	struct {
		u32 AF_COUNT:7;
		u32         :9;
		u32 AE_COUNT:7;
		u32         :9;
	} fld;
} FIFO_FLAG_CNT_R;

typedef union iic_clk_dur {
	u32   reg;
	struct {
		u32 PHASE_1:8;
		u32 PHASE_2:8;
		u32 PHASE_3:8;
		u32 PHASE_4:8;
	} fld;
} IIC_CLK_DUR_R;

typedef union iic_csr1_tag {
	u32   reg;
	struct {
		u32 AUTO_EN     :1;
		u32 BYPASS      :1;
		u32 SDA_OUT     :1;
		u32 SCL_OUT     :1;
		u32             :4;
		u32 AUTO_ABORT  :1;
		u32 DIRECT_ABORT:1;
		u32 SDA_IN      :1;
		u32 SCL_IN      :1;
		u32             :4;
		u32 AUTO_ADDR   :8;
		u32 RD_DATA     :8;
	} fld;
} IIC_CSR1_R;

/**********************************
 * iic_csr2_tag
 */
typedef union iic_csr2_tag {
	u32   reg;
	struct {
		u32 DIR_WR_DATA :8;
		u32 DIR_SUB_ADDR:8;
		u32 DIR_RD      :1;
		u32 DIR_ADDR    :7;
		u32 NEW_CYCLE   :1;
		u32             :7;
	} fld;
}  IIC_CSR2_R;

/* use for both EVEN and ODD DMA UPPER LIMITS */

/*
 * dma_upper_lmt_tag
 */
typedef union dma_upper_lmt_tag   {
	u32 reg;
	struct {
		u32 DMA_UPPER_LMT_VAL:24;
		u32                  :8;
	} fld;
} DMA_UPPER_LMT_R;


/*
 * Global declarations of local copies of boards' 32 bit registers
 */
extern u32 even_dma_start_r;		/*  bit 0 should always be 0 */
extern u32 odd_dma_start_r;		/*               ..          */
extern u32 even_dma_stride_r;	/*  bits 0&1 should always be 0 */
extern u32 odd_dma_stride_r;		/*               ..             */
extern u32 even_pixel_fmt_r;
extern u32 odd_pixel_fmt_r;

extern FIFO_TRIGGER_R		fifo_trigger_r;
extern XFER_MODE_R		xfer_mode_r;
extern CSR1_R			csr1_r;
extern RETRY_WAIT_CNT_R		retry_wait_cnt_r;
extern INT_CSR_R		int_csr_r;

extern u32 even_fld_mask_r;
extern u32 odd_fld_mask_r;

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
	u8 reg;
	struct {
		u8 CHROM_FIL:1;
		u8 SYNC_SNTL:1;
		u8 HZ50:1;
		u8 SYNC_PRESENT:1;
		u8 BUSY_EVE:1;
		u8 BUSY_ODD:1;
		u8 DISP_PASS:1;
	} fld;
} I2C_CSR2;

typedef union i2c_even_csr_tag {
	u8    reg;
	struct {
		u8 DONE_EVE :1;
		u8 SNGL_EVE :1;
		u8 ERROR_EVE:1;
		u8          :5;
	} fld;
} I2C_EVEN_CSR;

typedef union i2c_odd_csr_tag {
	u8 reg;
	struct {
		u8 DONE_ODD:1;
		u8 SNGL_ODD:1;
		u8 ERROR_ODD:1;
		u8 :5;
	} fld;
} I2C_ODD_CSR;

typedef union i2c_config_tag {
	u8 reg;
	struct {
		u8 ACQ_MODE:2;
		u8 EXT_TRIG_EN:1;
		u8 EXT_TRIG_POL:1;
		u8 H_SCALE:1;
		u8 CLIP:1;
		u8 PM_LUT_SEL:1;
		u8 PM_LUT_PGM:1;
	} fld;
} I2C_CONFIG;


typedef union i2c_ad_cmd_tag {
	/* bits can have 3 different meanings depending on value of AD_ADDR */
	u8 reg;
	/* Bt252 Command Register if AD_ADDR = 00h */
	struct {
		u8             :2;
		u8 SYNC_LVL_SEL:2;
		u8 SYNC_CNL_SEL:2;
		u8 DIGITIZE_CNL_SEL1:2;
		} bt252_command;

	/* Bt252 IOUT0 register if AD_ADDR = 01h */
	struct {
		u8 IOUT_DATA:8;
	} bt252_iout0;

	/* BT252 IOUT1 register if AD_ADDR = 02h */
	struct {
		u8 IOUT_DATA:8;
	} bt252_iout1;
} I2C_AD_CMD;


/***** Global declarations of local copies of boards' 8 bit I2C registers ***/

extern I2C_CSR2			i2c_csr2;
extern I2C_EVEN_CSR		i2c_even_csr;
extern I2C_ODD_CSR		i2c_odd_csr;
extern I2C_CONFIG		i2c_config;
extern u8			i2c_dt_id;
extern u8			i2c_x_clip_start;
extern u8			i2c_y_clip_start;
extern u8			i2c_x_clip_end;
extern u8			i2c_y_clip_end;
extern u8			i2c_ad_addr;
extern u8			i2c_ad_lut;
extern I2C_AD_CMD		i2c_ad_cmd;
extern u8			i2c_dig_out;
extern u8			i2c_pm_lut_addr;
extern u8			i2c_pm_lut_data;

/* Functions for Global use */

/* access 8-bit IIC registers */

extern int ReadI2C(u8 *lpReg, u_short wIregIndex, u8 *byVal);
extern int WriteI2C(u8 *lpReg, u_short wIregIndex, u8 byVal);

#endif
