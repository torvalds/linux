/*
  Copyright (c), 2004-2005,2007-2010 Trident Microsystems, Inc.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
	and/or other materials provided with the distribution.
  * Neither the name of Trident Microsystems nor Hauppauge Computer Works
    nor the names of its contributors may be used to endorse or promote
	products derived from this software without specific prior written
	permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

  DRXJ specific implementation of DRX driver
  authors: Dragan Savic, Milos Nikolic, Mihajlo Katona, Tao Ding, Paul Janssen
*/

/*-----------------------------------------------------------------------------
INCLUDE FILES
----------------------------------------------------------------------------*/

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include "drxj.h"
#include "drxj_map.h"
#include "drx_driver.h"

/*============================================================================*/
/*=== DEFINES ================================================================*/
/*============================================================================*/

/**
* \brief Maximum u32 value.
*/
#ifndef MAX_U32
#define MAX_U32  ((u32) (0xFFFFFFFFL))
#endif

/* Customer configurable hardware settings, etc */
#ifndef MPEG_SERIAL_OUTPUT_PIN_DRIVE_STRENGTH
#define MPEG_SERIAL_OUTPUT_PIN_DRIVE_STRENGTH 0x02
#endif

#ifndef MPEG_PARALLEL_OUTPUT_PIN_DRIVE_STRENGTH
#define MPEG_PARALLEL_OUTPUT_PIN_DRIVE_STRENGTH 0x02
#endif

#ifndef MPEG_OUTPUT_CLK_DRIVE_STRENGTH
#define MPEG_OUTPUT_CLK_DRIVE_STRENGTH 0x06
#endif

#ifndef OOB_CRX_DRIVE_STRENGTH
#define OOB_CRX_DRIVE_STRENGTH 0x02
#endif

#ifndef OOB_DRX_DRIVE_STRENGTH
#define OOB_DRX_DRIVE_STRENGTH 0x02
#endif
/**** START DJCOMBO patches to DRXJ registermap constants *********************/
/**** registermap 200706071303 from drxj **************************************/
#define   ATV_TOP_CR_AMP_TH_FM                                              0x0
#define   ATV_TOP_CR_AMP_TH_L                                               0xA
#define   ATV_TOP_CR_AMP_TH_LP                                              0xA
#define   ATV_TOP_CR_AMP_TH_BG                                              0x8
#define   ATV_TOP_CR_AMP_TH_DK                                              0x8
#define   ATV_TOP_CR_AMP_TH_I                                               0x8
#define     ATV_TOP_CR_CONT_CR_D_MN                                         0x18
#define     ATV_TOP_CR_CONT_CR_D_FM                                         0x0
#define     ATV_TOP_CR_CONT_CR_D_L                                          0x20
#define     ATV_TOP_CR_CONT_CR_D_LP                                         0x20
#define     ATV_TOP_CR_CONT_CR_D_BG                                         0x18
#define     ATV_TOP_CR_CONT_CR_D_DK                                         0x18
#define     ATV_TOP_CR_CONT_CR_D_I                                          0x18
#define     ATV_TOP_CR_CONT_CR_I_MN                                         0x80
#define     ATV_TOP_CR_CONT_CR_I_FM                                         0x0
#define     ATV_TOP_CR_CONT_CR_I_L                                          0x80
#define     ATV_TOP_CR_CONT_CR_I_LP                                         0x80
#define     ATV_TOP_CR_CONT_CR_I_BG                                         0x80
#define     ATV_TOP_CR_CONT_CR_I_DK                                         0x80
#define     ATV_TOP_CR_CONT_CR_I_I                                          0x80
#define     ATV_TOP_CR_CONT_CR_P_MN                                         0x4
#define     ATV_TOP_CR_CONT_CR_P_FM                                         0x0
#define     ATV_TOP_CR_CONT_CR_P_L                                          0x4
#define     ATV_TOP_CR_CONT_CR_P_LP                                         0x4
#define     ATV_TOP_CR_CONT_CR_P_BG                                         0x4
#define     ATV_TOP_CR_CONT_CR_P_DK                                         0x4
#define     ATV_TOP_CR_CONT_CR_P_I                                          0x4
#define   ATV_TOP_CR_OVM_TH_MN                                              0xA0
#define   ATV_TOP_CR_OVM_TH_FM                                              0x0
#define   ATV_TOP_CR_OVM_TH_L                                               0xA0
#define   ATV_TOP_CR_OVM_TH_LP                                              0xA0
#define   ATV_TOP_CR_OVM_TH_BG                                              0xA0
#define   ATV_TOP_CR_OVM_TH_DK                                              0xA0
#define   ATV_TOP_CR_OVM_TH_I                                               0xA0
#define     ATV_TOP_EQU0_EQU_C0_FM                                          0x0
#define     ATV_TOP_EQU0_EQU_C0_L                                           0x3
#define     ATV_TOP_EQU0_EQU_C0_LP                                          0x3
#define     ATV_TOP_EQU0_EQU_C0_BG                                          0x7
#define     ATV_TOP_EQU0_EQU_C0_DK                                          0x0
#define     ATV_TOP_EQU0_EQU_C0_I                                           0x3
#define     ATV_TOP_EQU1_EQU_C1_FM                                          0x0
#define     ATV_TOP_EQU1_EQU_C1_L                                           0x1F6
#define     ATV_TOP_EQU1_EQU_C1_LP                                          0x1F6
#define     ATV_TOP_EQU1_EQU_C1_BG                                          0x197
#define     ATV_TOP_EQU1_EQU_C1_DK                                          0x198
#define     ATV_TOP_EQU1_EQU_C1_I                                           0x1F6
#define     ATV_TOP_EQU2_EQU_C2_FM                                          0x0
#define     ATV_TOP_EQU2_EQU_C2_L                                           0x28
#define     ATV_TOP_EQU2_EQU_C2_LP                                          0x28
#define     ATV_TOP_EQU2_EQU_C2_BG                                          0xC5
#define     ATV_TOP_EQU2_EQU_C2_DK                                          0xB0
#define     ATV_TOP_EQU2_EQU_C2_I                                           0x28
#define     ATV_TOP_EQU3_EQU_C3_FM                                          0x0
#define     ATV_TOP_EQU3_EQU_C3_L                                           0x192
#define     ATV_TOP_EQU3_EQU_C3_LP                                          0x192
#define     ATV_TOP_EQU3_EQU_C3_BG                                          0x12E
#define     ATV_TOP_EQU3_EQU_C3_DK                                          0x18E
#define     ATV_TOP_EQU3_EQU_C3_I                                           0x192
#define     ATV_TOP_STD_MODE_MN                                             0x0
#define     ATV_TOP_STD_MODE_FM                                             0x1
#define     ATV_TOP_STD_MODE_L                                              0x0
#define     ATV_TOP_STD_MODE_LP                                             0x0
#define     ATV_TOP_STD_MODE_BG                                             0x0
#define     ATV_TOP_STD_MODE_DK                                             0x0
#define     ATV_TOP_STD_MODE_I                                              0x0
#define     ATV_TOP_STD_VID_POL_MN                                          0x0
#define     ATV_TOP_STD_VID_POL_FM                                          0x0
#define     ATV_TOP_STD_VID_POL_L                                           0x2
#define     ATV_TOP_STD_VID_POL_LP                                          0x2
#define     ATV_TOP_STD_VID_POL_BG                                          0x0
#define     ATV_TOP_STD_VID_POL_DK                                          0x0
#define     ATV_TOP_STD_VID_POL_I                                           0x0
#define   ATV_TOP_VID_AMP_MN                                                0x380
#define   ATV_TOP_VID_AMP_FM                                                0x0
#define   ATV_TOP_VID_AMP_L                                                 0xF50
#define   ATV_TOP_VID_AMP_LP                                                0xF50
#define   ATV_TOP_VID_AMP_BG                                                0x380
#define   ATV_TOP_VID_AMP_DK                                                0x394
#define   ATV_TOP_VID_AMP_I                                                 0x3D8
#define   IQM_CF_OUT_ENA_OFDM__M                                            0x4
#define     IQM_FS_ADJ_SEL_B_QAM                                            0x1
#define     IQM_FS_ADJ_SEL_B_OFF                                            0x0
#define     IQM_FS_ADJ_SEL_B_VSB                                            0x2
#define     IQM_RC_ADJ_SEL_B_OFF                                            0x0
#define     IQM_RC_ADJ_SEL_B_QAM                                            0x1
#define     IQM_RC_ADJ_SEL_B_VSB                                            0x2
/**** END DJCOMBO patches to DRXJ registermap *********************************/

#include "drx_driver_version.h"

/* #define DRX_DEBUG */
#ifdef DRX_DEBUG
#include <stdio.h>
#endif

/*-----------------------------------------------------------------------------
ENUMS
----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
DEFINES
----------------------------------------------------------------------------*/
#ifndef DRXJ_WAKE_UP_KEY
#define DRXJ_WAKE_UP_KEY (demod->my_i2c_dev_addr->i2c_addr)
#endif

/**
* \def DRXJ_DEF_I2C_ADDR
* \brief Default I2C addres of a demodulator instance.
*/
#define DRXJ_DEF_I2C_ADDR (0x52)

/**
* \def DRXJ_DEF_DEMOD_DEV_ID
* \brief Default device identifier of a demodultor instance.
*/
#define DRXJ_DEF_DEMOD_DEV_ID      (1)

/**
* \def DRXJ_SCAN_TIMEOUT
* \brief Timeout value for waiting on demod lock during channel scan (millisec).
*/
#define DRXJ_SCAN_TIMEOUT    1000

/**
* \def DRXJ_DAP
* \brief Name of structure containing all data access protocol functions.
*/
#define DRXJ_DAP drx_dap_drxj_funct_g

/**
* \def HI_I2C_DELAY
* \brief HI timing delay for I2C timing (in nano seconds)
*
*  Used to compute HI_CFG_DIV
*/
#define HI_I2C_DELAY    42

/**
* \def HI_I2C_BRIDGE_DELAY
* \brief HI timing delay for I2C timing (in nano seconds)
*
*  Used to compute HI_CFG_BDL
*/
#define HI_I2C_BRIDGE_DELAY   750

/**
* \brief Time Window for MER and SER Measurement in Units of Segment duration.
*/
#define VSB_TOP_MEASUREMENT_PERIOD  64
#define SYMBOLS_PER_SEGMENT         832

/**
* \brief bit rate and segment rate constants used for SER and BER.
*/
/* values taken from the QAM microcode */
#define DRXJ_QAM_SL_SIG_POWER_QAM_UNKNOWN 0
#define DRXJ_QAM_SL_SIG_POWER_QPSK        32768
#define DRXJ_QAM_SL_SIG_POWER_QAM8        24576
#define DRXJ_QAM_SL_SIG_POWER_QAM16       40960
#define DRXJ_QAM_SL_SIG_POWER_QAM32       20480
#define DRXJ_QAM_SL_SIG_POWER_QAM64       43008
#define DRXJ_QAM_SL_SIG_POWER_QAM128      20992
#define DRXJ_QAM_SL_SIG_POWER_QAM256      43520
/**
* \brief Min supported symbolrates.
*/
#ifndef DRXJ_QAM_SYMBOLRATE_MIN
#define DRXJ_QAM_SYMBOLRATE_MIN          (520000)
#endif

/**
* \brief Max supported symbolrates.
*/
#ifndef DRXJ_QAM_SYMBOLRATE_MAX
#define DRXJ_QAM_SYMBOLRATE_MAX         (7233000)
#endif

/**
* \def DRXJ_QAM_MAX_WAITTIME
* \brief Maximal wait time for QAM auto constellation in ms
*/
#ifndef DRXJ_QAM_MAX_WAITTIME
#define DRXJ_QAM_MAX_WAITTIME 900
#endif

#ifndef DRXJ_QAM_FEC_LOCK_WAITTIME
#define DRXJ_QAM_FEC_LOCK_WAITTIME 150
#endif

#ifndef DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME
#define DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME 200
#endif

/**
* \def SCU status and results
* \brief SCU
*/
#define DRX_SCU_READY               0
#define DRXJ_MAX_WAITTIME           100	/* ms */
#define FEC_RS_MEASUREMENT_PERIOD   12894	/* 1 sec */
#define FEC_RS_MEASUREMENT_PRESCALE 1	/* n sec */

/**
* \def DRX_AUD_MAX_DEVIATION
* \brief Needed for calculation of prescale feature in AUD
*/
#ifndef DRXJ_AUD_MAX_FM_DEVIATION
#define DRXJ_AUD_MAX_FM_DEVIATION  100	/* kHz */
#endif

/**
* \brief Needed for calculation of NICAM prescale feature in AUD
*/
#ifndef DRXJ_AUD_MAX_NICAM_PRESCALE
#define DRXJ_AUD_MAX_NICAM_PRESCALE  (9)	/* dB */
#endif

/**
* \brief Needed for calculation of NICAM prescale feature in AUD
*/
#ifndef DRXJ_AUD_MAX_WAITTIME
#define DRXJ_AUD_MAX_WAITTIME  250	/* ms */
#endif

/* ATV config changed flags */
#define DRXJ_ATV_CHANGED_COEF          (0x00000001UL)
#define DRXJ_ATV_CHANGED_PEAK_FLT      (0x00000008UL)
#define DRXJ_ATV_CHANGED_NOISE_FLT     (0x00000010UL)
#define DRXJ_ATV_CHANGED_OUTPUT        (0x00000020UL)
#define DRXJ_ATV_CHANGED_SIF_ATT       (0x00000040UL)

/* UIO define */
#define DRX_UIO_MODE_FIRMWARE_SMA DRX_UIO_MODE_FIRMWARE0
#define DRX_UIO_MODE_FIRMWARE_SAW DRX_UIO_MODE_FIRMWARE1

/*
 * MICROCODE RELATED DEFINES
 */

/* Magic word for checking correct Endianess of microcode data */
#define DRX_UCODE_MAGIC_WORD         ((((u16)'H')<<8)+((u16)'L'))

/* CRC flag in ucode header, flags field. */
#define DRX_UCODE_CRC_FLAG           (0x0001)

/*
 * Maximum size of buffer used to verify the microcode.
 * Must be an even number
 */
#define DRX_UCODE_MAX_BUF_SIZE       (DRXDAP_MAX_RCHUNKSIZE)

#if DRX_UCODE_MAX_BUF_SIZE & 1
#error DRX_UCODE_MAX_BUF_SIZE must be an even number
#endif

/*
 * Power mode macros
 */

#define DRX_ISPOWERDOWNMODE(mode) ((mode == DRX_POWER_MODE_9) || \
				       (mode == DRX_POWER_MODE_10) || \
				       (mode == DRX_POWER_MODE_11) || \
				       (mode == DRX_POWER_MODE_12) || \
				       (mode == DRX_POWER_MODE_13) || \
				       (mode == DRX_POWER_MODE_14) || \
				       (mode == DRX_POWER_MODE_15) || \
				       (mode == DRX_POWER_MODE_16) || \
				       (mode == DRX_POWER_DOWN))

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
/*============================================================================*/
/*=== MICROCODE RELATED DEFINES ==============================================*/
/*============================================================================*/

/**
* \def DRXJ_UCODE_MAGIC_WORD
* \brief Magic word for checking correct Endianess of microcode data.
*
*/

#ifndef DRXJ_UCODE_MAGIC_WORD
#define DRXJ_UCODE_MAGIC_WORD         ((((u16)'H')<<8)+((u16)'L'))
#endif

/**
* \def DRXJ_UCODE_CRC_FLAG
* \brief CRC flag in ucode header, flags field.
*
*/

#ifndef DRXJ_UCODE_CRC_FLAG
#define DRXJ_UCODE_CRC_FLAG           (0x0001)
#endif

/**
* \def DRXJ_UCODE_COMPRESSION_FLAG
* \brief Compression flag in ucode header, flags field.
*
*/

#ifndef DRXJ_UCODE_COMPRESSION_FLAG
#define DRXJ_UCODE_COMPRESSION_FLAG   (0x0002)
#endif

/**
* \def DRXJ_UCODE_MAX_BUF_SIZE
* \brief Maximum size of buffer used to verify the microcode.Must be an even number.
*
*/

#ifndef DRXJ_UCODE_MAX_BUF_SIZE
#define DRXJ_UCODE_MAX_BUF_SIZE       (DRXDAP_MAX_RCHUNKSIZE)
#endif
#if DRXJ_UCODE_MAX_BUF_SIZE & 1
#error DRXJ_UCODE_MAX_BUF_SIZE must be an even number
#endif

#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

/* Pin safe mode macro */
#define DRXJ_PIN_SAFE_MODE 0x0000
/*============================================================================*/
/*=== GLOBAL VARIABLEs =======================================================*/
/*============================================================================*/
/**
*/

/**
* \brief Temporary register definitions.
*        (register definitions that are not yet available in register master)
*/

/******************************************************************************/
/* Audio block 0x103 is write only. To avoid shadowing in driver accessing    */
/* RAM adresses directly. This must be READ ONLY to avoid problems.           */
/* Writing to the interface adresses is more than only writing the RAM        */
/* locations                                                                  */
/******************************************************************************/
/**
* \brief RAM location of MODUS registers
*/
#define AUD_DEM_RAM_MODUS_HI__A              0x10204A3
#define AUD_DEM_RAM_MODUS_HI__M              0xF000

#define AUD_DEM_RAM_MODUS_LO__A              0x10204A4
#define AUD_DEM_RAM_MODUS_LO__M              0x0FFF

/**
* \brief RAM location of I2S config registers
*/
#define AUD_DEM_RAM_I2S_CONFIG1__A           0x10204B1
#define AUD_DEM_RAM_I2S_CONFIG2__A           0x10204B2

/**
* \brief RAM location of DCO config registers
*/
#define AUD_DEM_RAM_DCO_B_HI__A              0x1020461
#define AUD_DEM_RAM_DCO_B_LO__A              0x1020462
#define AUD_DEM_RAM_DCO_A_HI__A              0x1020463
#define AUD_DEM_RAM_DCO_A_LO__A              0x1020464

/**
* \brief RAM location of Threshold registers
*/
#define AUD_DEM_RAM_NICAM_THRSHLD__A         0x102045A
#define AUD_DEM_RAM_A2_THRSHLD__A            0x10204BB
#define AUD_DEM_RAM_BTSC_THRSHLD__A          0x10204A6

/**
* \brief RAM location of Carrier Threshold registers
*/
#define AUD_DEM_RAM_CM_A_THRSHLD__A          0x10204AF
#define AUD_DEM_RAM_CM_B_THRSHLD__A          0x10204B0

/**
* \brief FM Matrix register fix
*/
#ifdef AUD_DEM_WR_FM_MATRIX__A
#undef  AUD_DEM_WR_FM_MATRIX__A
#endif
#define AUD_DEM_WR_FM_MATRIX__A              0x105006F

/*============================================================================*/
/**
* \brief Defines required for audio
*/
#define AUD_VOLUME_ZERO_DB                      115
#define AUD_VOLUME_DB_MIN                       -60
#define AUD_VOLUME_DB_MAX                       12
#define AUD_CARRIER_STRENGTH_QP_0DB             0x4000
#define AUD_CARRIER_STRENGTH_QP_0DB_LOG10T100   421
#define AUD_MAX_AVC_REF_LEVEL                   15
#define AUD_I2S_FREQUENCY_MAX                   48000UL
#define AUD_I2S_FREQUENCY_MIN                   12000UL
#define AUD_RDS_ARRAY_SIZE                      18

/**
* \brief Needed for calculation of prescale feature in AUD
*/
#ifndef DRX_AUD_MAX_FM_DEVIATION
#define DRX_AUD_MAX_FM_DEVIATION  (100)	/* kHz */
#endif

/**
* \brief Needed for calculation of NICAM prescale feature in AUD
*/
#ifndef DRX_AUD_MAX_NICAM_PRESCALE
#define DRX_AUD_MAX_NICAM_PRESCALE  (9)	/* dB */
#endif

/*============================================================================*/
/* Values for I2S Master/Slave pin configurations */
#define SIO_PDR_I2S_CL_CFG_MODE__MASTER      0x0004
#define SIO_PDR_I2S_CL_CFG_DRIVE__MASTER     0x0008
#define SIO_PDR_I2S_CL_CFG_MODE__SLAVE       0x0004
#define SIO_PDR_I2S_CL_CFG_DRIVE__SLAVE      0x0000

#define SIO_PDR_I2S_DA_CFG_MODE__MASTER      0x0003
#define SIO_PDR_I2S_DA_CFG_DRIVE__MASTER     0x0008
#define SIO_PDR_I2S_DA_CFG_MODE__SLAVE       0x0003
#define SIO_PDR_I2S_DA_CFG_DRIVE__SLAVE      0x0008

#define SIO_PDR_I2S_WS_CFG_MODE__MASTER      0x0004
#define SIO_PDR_I2S_WS_CFG_DRIVE__MASTER     0x0008
#define SIO_PDR_I2S_WS_CFG_MODE__SLAVE       0x0004
#define SIO_PDR_I2S_WS_CFG_DRIVE__SLAVE      0x0000

/*============================================================================*/
/*=== REGISTER ACCESS MACROS =================================================*/
/*============================================================================*/

/**
* This macro is used to create byte arrays for block writes.
* Block writes speed up I2C traffic between host and demod.
* The macro takes care of the required byte order in a 16 bits word.
* x -> lowbyte(x), highbyte(x)
*/
#define DRXJ_16TO8(x) ((u8) (((u16)x) & 0xFF)), \
		       ((u8)((((u16)x)>>8)&0xFF))
/**
* This macro is used to convert byte array to 16 bit register value for block read.
* Block read speed up I2C traffic between host and demod.
* The macro takes care of the required byte order in a 16 bits word.
*/
#define DRXJ_8TO16(x) ((u16) (x[0] | (x[1] << 8)))

/*============================================================================*/
/*=== MISC DEFINES ===========================================================*/
/*============================================================================*/

/*============================================================================*/
/*=== HI COMMAND RELATED DEFINES =============================================*/
/*============================================================================*/

/**
* \brief General maximum number of retries for ucode command interfaces
*/
#define DRXJ_MAX_RETRIES (100)

/*============================================================================*/
/*=== STANDARD RELATED MACROS ================================================*/
/*============================================================================*/

#define DRXJ_ISATVSTD(std) ((std == DRX_STANDARD_PAL_SECAM_BG) || \
			       (std == DRX_STANDARD_PAL_SECAM_DK) || \
			       (std == DRX_STANDARD_PAL_SECAM_I) || \
			       (std == DRX_STANDARD_PAL_SECAM_L) || \
			       (std == DRX_STANDARD_PAL_SECAM_LP) || \
			       (std == DRX_STANDARD_NTSC) || \
			       (std == DRX_STANDARD_FM))

#define DRXJ_ISQAMSTD(std) ((std == DRX_STANDARD_ITU_A) || \
			       (std == DRX_STANDARD_ITU_B) || \
			       (std == DRX_STANDARD_ITU_C) || \
			       (std == DRX_STANDARD_ITU_D))

/*-----------------------------------------------------------------------------
GLOBAL VARIABLES
----------------------------------------------------------------------------*/
/*
 * DRXJ DAP structures
 */

static int drxj_dap_read_block(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u16 datasize,
				      u8 *data, u32 flags);

static int drxj_dap_read_modify_write_reg8(struct i2c_device_addr *dev_addr,
						u32 waddr,
						u32 raddr,
						u8 wdata, u8 *rdata);

static int drxj_dap_read_modify_write_reg16(struct i2c_device_addr *dev_addr,
						 u32 waddr,
						 u32 raddr,
						 u16 wdata, u16 *rdata);

static int drxj_dap_read_modify_write_reg32(struct i2c_device_addr *dev_addr,
						 u32 waddr,
						 u32 raddr,
						 u32 wdata, u32 *rdata);

static int drxj_dap_read_reg8(struct i2c_device_addr *dev_addr,
				     u32 addr,
				     u8 *data, u32 flags);

static int drxj_dap_read_reg16(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u16 *data, u32 flags);

static int drxj_dap_read_reg32(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u32 *data, u32 flags);

static int drxj_dap_write_block(struct i2c_device_addr *dev_addr,
				       u32 addr,
				       u16 datasize,
				       u8 *data, u32 flags);

static int drxj_dap_write_reg8(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u8 data, u32 flags);

static int drxj_dap_write_reg16(struct i2c_device_addr *dev_addr,
				       u32 addr,
				       u16 data, u32 flags);

static int drxj_dap_write_reg32(struct i2c_device_addr *dev_addr,
				       u32 addr,
				       u32 data, u32 flags);

/* The version structure of this protocol implementation */
char drx_dap_drxj_module_name[] = "DRXJ Data Access Protocol";
char drx_dap_drxj_version_text[] = "0.0.0";

struct drx_version drx_dap_drxj_version = {
	DRX_MODULE_DAP,	      /**< type identifier of the module  */
	drx_dap_drxj_module_name, /**< name or description of module  */

	0,		      /**< major version number           */
	0,		      /**< minor version number           */
	0,		      /**< patch version number           */
	drx_dap_drxj_version_text /**< version as text string         */
};

/* The structure containing the protocol interface */
struct drx_access_func drx_dap_drxj_funct_g = {
	&drx_dap_drxj_version,
	drxj_dap_write_block,	/* Supported       */
	drxj_dap_read_block,	/* Supported       */
	drxj_dap_write_reg8,	/* Not supported   */
	drxj_dap_read_reg8,	/* Not supported   */
	drxj_dap_read_modify_write_reg8,	/* Not supported   */
	drxj_dap_write_reg16,	/* Supported       */
	drxj_dap_read_reg16,	/* Supported       */
	drxj_dap_read_modify_write_reg16,	/* Supported       */
	drxj_dap_write_reg32,	/* Supported       */
	drxj_dap_read_reg32,	/* Supported       */
	drxj_dap_read_modify_write_reg32,	/* Not supported   */
};

/**
* /var DRXJ_Func_g
* /brief The driver functions of the drxj
*/
struct drx_demod_func drxj_functions_g = {
	DRXJ_TYPE_ID,
	drxj_open,
	drxj_close,
	drxj_ctrl
};

struct drxj_data drxj_data_g = {
	false,			/* has_lna : true if LNA (aka PGA) present      */
	false,			/* has_oob : true if OOB supported              */
	false,			/* has_ntsc: true if NTSC supported             */
	false,			/* has_btsc: true if BTSC supported             */
	false,			/* has_smatx: true if SMA_TX pin is available   */
	false,			/* has_smarx: true if SMA_RX pin is available   */
	false,			/* has_gpio : true if GPIO pin is available     */
	false,			/* has_irqn : true if IRQN pin is available     */
	0,			/* mfx A1/A2/A... */

	/* tuner settings */
	false,			/* tuner mirrors RF signal    */
	/* standard/channel settings */
	DRX_STANDARD_UNKNOWN,	/* current standard           */
	DRX_CONSTELLATION_AUTO,	/* constellation              */
	0,			/* frequency in KHz           */
	DRX_BANDWIDTH_UNKNOWN,	/* curr_bandwidth              */
	DRX_MIRROR_NO,		/* mirror                     */

	/* signal quality information: */
	/* default values taken from the QAM Programming guide */
	/*   fec_bits_desired should not be less than 4000000    */
	4000000,		/* fec_bits_desired    */
	5,			/* fec_vd_plen         */
	4,			/* qam_vd_prescale     */
	0xFFFF,			/* qamVDPeriod       */
	204 * 8,		/* fec_rs_plen annex A */
	1,			/* fec_rs_prescale     */
	FEC_RS_MEASUREMENT_PERIOD,	/* fec_rs_period     */
	true,			/* reset_pkt_err_acc    */
	0,			/* pkt_err_acc_start    */

	/* HI configuration */
	0,			/* hi_cfg_timing_div    */
	0,			/* hi_cfg_bridge_delay  */
	0,			/* hi_cfg_wake_up_key    */
	0,			/* hi_cfg_ctrl         */
	0,			/* HICfgTimeout      */
	/* UIO configuartion */
	DRX_UIO_MODE_DISABLE,	/* uio_sma_rx_mode      */
	DRX_UIO_MODE_DISABLE,	/* uio_sma_tx_mode      */
	DRX_UIO_MODE_DISABLE,	/* uioASELMode       */
	DRX_UIO_MODE_DISABLE,	/* uio_irqn_mode       */
	/* FS setting */
	0UL,			/* iqm_fs_rate_ofs      */
	false,			/* pos_image          */
	/* RC setting */
	0UL,			/* iqm_rc_rate_ofs      */
	/* AUD information */
/*   false,                  * flagSetAUDdone    */
/*   false,                  * detectedRDS       */
/*   true,                   * flagASDRequest    */
/*   false,                  * flagHDevClear     */
/*   false,                  * flagHDevSet       */
/*   (u16) 0xFFF,          * rdsLastCount      */

/*#ifdef DRXJ_SPLIT_UCODE_UPLOAD
   false,                  * flag_aud_mc_uploaded */
/*#endif * DRXJ_SPLIT_UCODE_UPLOAD */
	/* ATV configuartion */
	0UL,			/* flags cfg changes */
	/* shadow of ATV_TOP_EQU0__A */
	{-5,
	 ATV_TOP_EQU0_EQU_C0_FM,
	 ATV_TOP_EQU0_EQU_C0_L,
	 ATV_TOP_EQU0_EQU_C0_LP,
	 ATV_TOP_EQU0_EQU_C0_BG,
	 ATV_TOP_EQU0_EQU_C0_DK,
	 ATV_TOP_EQU0_EQU_C0_I},
	/* shadow of ATV_TOP_EQU1__A */
	{-50,
	 ATV_TOP_EQU1_EQU_C1_FM,
	 ATV_TOP_EQU1_EQU_C1_L,
	 ATV_TOP_EQU1_EQU_C1_LP,
	 ATV_TOP_EQU1_EQU_C1_BG,
	 ATV_TOP_EQU1_EQU_C1_DK,
	 ATV_TOP_EQU1_EQU_C1_I},
	/* shadow of ATV_TOP_EQU2__A */
	{210,
	 ATV_TOP_EQU2_EQU_C2_FM,
	 ATV_TOP_EQU2_EQU_C2_L,
	 ATV_TOP_EQU2_EQU_C2_LP,
	 ATV_TOP_EQU2_EQU_C2_BG,
	 ATV_TOP_EQU2_EQU_C2_DK,
	 ATV_TOP_EQU2_EQU_C2_I},
	/* shadow of ATV_TOP_EQU3__A */
	{-160,
	 ATV_TOP_EQU3_EQU_C3_FM,
	 ATV_TOP_EQU3_EQU_C3_L,
	 ATV_TOP_EQU3_EQU_C3_LP,
	 ATV_TOP_EQU3_EQU_C3_BG,
	 ATV_TOP_EQU3_EQU_C3_DK,
	 ATV_TOP_EQU3_EQU_C3_I},
	false,			/* flag: true=bypass             */
	ATV_TOP_VID_PEAK__PRE,	/* shadow of ATV_TOP_VID_PEAK__A */
	ATV_TOP_NOISE_TH__PRE,	/* shadow of ATV_TOP_NOISE_TH__A */
	true,			/* flag CVBS ouput enable        */
	false,			/* flag SIF ouput enable         */
	DRXJ_SIF_ATTENUATION_0DB,	/* current SIF att setting       */
	{			/* qam_rf_agc_cfg */
	 DRX_STANDARD_ITU_B,	/* standard            */
	 DRX_AGC_CTRL_AUTO,	/* ctrl_mode            */
	 0,			/* output_level         */
	 0,			/* min_output_level      */
	 0xFFFF,		/* max_output_level      */
	 0x0000,		/* speed               */
	 0x0000,		/* top                 */
	 0x0000			/* c.o.c.              */
	 },
	{			/* qam_if_agc_cfg */
	 DRX_STANDARD_ITU_B,	/* standard            */
	 DRX_AGC_CTRL_AUTO,	/* ctrl_mode            */
	 0,			/* output_level         */
	 0,			/* min_output_level      */
	 0xFFFF,		/* max_output_level      */
	 0x0000,		/* speed               */
	 0x0000,		/* top    (don't care) */
	 0x0000			/* c.o.c. (don't care) */
	 },
	{			/* vsb_rf_agc_cfg */
	 DRX_STANDARD_8VSB,	/* standard       */
	 DRX_AGC_CTRL_AUTO,	/* ctrl_mode       */
	 0,			/* output_level    */
	 0,			/* min_output_level */
	 0xFFFF,		/* max_output_level */
	 0x0000,		/* speed          */
	 0x0000,		/* top    (don't care) */
	 0x0000			/* c.o.c. (don't care) */
	 },
	{			/* vsb_if_agc_cfg */
	 DRX_STANDARD_8VSB,	/* standard       */
	 DRX_AGC_CTRL_AUTO,	/* ctrl_mode       */
	 0,			/* output_level    */
	 0,			/* min_output_level */
	 0xFFFF,		/* max_output_level */
	 0x0000,		/* speed          */
	 0x0000,		/* top    (don't care) */
	 0x0000			/* c.o.c. (don't care) */
	 },
	0,			/* qam_pga_cfg */
	0,			/* vsb_pga_cfg */
	{			/* qam_pre_saw_cfg */
	 DRX_STANDARD_ITU_B,	/* standard  */
	 0,			/* reference */
	 false			/* use_pre_saw */
	 },
	{			/* vsb_pre_saw_cfg */
	 DRX_STANDARD_8VSB,	/* standard  */
	 0,			/* reference */
	 false			/* use_pre_saw */
	 },

	/* Version information */
#ifndef _CH_
	{
	 "01234567890",		/* human readable version microcode             */
	 "01234567890"		/* human readable version device specific code  */
	 },
	{
	 {			/* struct drx_version for microcode                   */
	  DRX_MODULE_UNKNOWN,
	  (char *)(NULL),
	  0,
	  0,
	  0,
	  (char *)(NULL)
	  },
	 {			/* struct drx_version for device specific code */
	  DRX_MODULE_UNKNOWN,
	  (char *)(NULL),
	  0,
	  0,
	  0,
	  (char *)(NULL)
	  }
	 },
	{
	 {			/* struct drx_version_list for microcode */
	  (struct drx_version *) (NULL),
	  (struct drx_version_list *) (NULL)
	  },
	 {			/* struct drx_version_list for device specific code */
	  (struct drx_version *) (NULL),
	  (struct drx_version_list *) (NULL)
	  }
	 },
#endif
	false,			/* smart_ant_inverted */
	/* Tracking filter setting for OOB  */
	{
	 12000,
	 9300,
	 6600,
	 5280,
	 3700,
	 3000,
	 2000,
	 0},
	false,			/* oob_power_on           */
	0,			/* mpeg_ts_static_bitrate  */
	false,			/* disable_te_ihandling   */
	false,			/* bit_reverse_mpeg_outout */
	DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO,	/* mpeg_output_clock_rate */
	DRXJ_MPEG_START_WIDTH_1CLKCYC,	/* mpeg_start_width */

	/* Pre SAW & Agc configuration for ATV */
	{
	 DRX_STANDARD_NTSC,	/* standard     */
	 7,			/* reference    */
	 true			/* use_pre_saw    */
	 },
	{			/* ATV RF-AGC */
	 DRX_STANDARD_NTSC,	/* standard              */
	 DRX_AGC_CTRL_AUTO,	/* ctrl_mode              */
	 0,			/* output_level           */
	 0,			/* min_output_level (d.c.) */
	 0,			/* max_output_level (d.c.) */
	 3,			/* speed                 */
	 9500,			/* top                   */
	 4000			/* cut-off current       */
	 },
	{			/* ATV IF-AGC */
	 DRX_STANDARD_NTSC,	/* standard              */
	 DRX_AGC_CTRL_AUTO,	/* ctrl_mode              */
	 0,			/* output_level           */
	 0,			/* min_output_level (d.c.) */
	 0,			/* max_output_level (d.c.) */
	 3,			/* speed                 */
	 2400,			/* top                   */
	 0			/* c.o.c.         (d.c.) */
	 },
	140,			/* ATV PGA config */
	0,			/* curr_symbol_rate */

	false,			/* pdr_safe_mode     */
	SIO_PDR_GPIO_CFG__PRE,	/* pdr_safe_restore_val_gpio  */
	SIO_PDR_VSYNC_CFG__PRE,	/* pdr_safe_restore_val_v_sync */
	SIO_PDR_SMA_RX_CFG__PRE,	/* pdr_safe_restore_val_sma_rx */
	SIO_PDR_SMA_TX_CFG__PRE,	/* pdr_safe_restore_val_sma_tx */

	4,			/* oob_pre_saw            */
	DRXJ_OOB_LO_POW_MINUS10DB,	/* oob_lo_pow             */
	{
	 false			/* aud_data, only first member */
	 },
};

/**
* \var drxj_default_addr_g
* \brief Default I2C address and device identifier.
*/
struct i2c_device_addr drxj_default_addr_g = {
	DRXJ_DEF_I2C_ADDR,	/* i2c address */
	DRXJ_DEF_DEMOD_DEV_ID	/* device id */
};

/**
* \var drxj_default_comm_attr_g
* \brief Default common attributes of a drxj demodulator instance.
*/
struct drx_common_attr drxj_default_comm_attr_g = {
	NULL,			/* ucode file           */
	true,			/* ucode verify switch  */
	{0},			/* version record       */

	44000,			/* IF in kHz in case no tuner instance is used  */
	(151875 - 0),		/* system clock frequency in kHz                */
	0,			/* oscillator frequency kHz                     */
	0,			/* oscillator deviation in ppm, signed          */
	false,			/* If true mirror frequency spectrum            */
	{
	 /* MPEG output configuration */
	 true,			/* If true, enable MPEG ouput    */
	 false,			/* If true, insert RS byte       */
	 true,			/* If true, parallel out otherwise serial */
	 false,			/* If true, invert DATA signals  */
	 false,			/* If true, invert ERR signal    */
	 false,			/* If true, invert STR signals   */
	 false,			/* If true, invert VAL signals   */
	 false,			/* If true, invert CLK signals   */
	 true,			/* If true, static MPEG clockrate will
				   be used, otherwise clockrate will
				   adapt to the bitrate of the TS */
	 19392658UL,		/* Maximum bitrate in b/s in case
				   static clockrate is selected */
	 DRX_MPEG_STR_WIDTH_1	/* MPEG Start width in clock cycles */
	 },
	/* Initilisations below can be ommited, they require no user input and
	   are initialy 0, NULL or false. The compiler will initialize them to these
	   values when ommited.  */
	false,			/* is_opened */

	/* SCAN */
	NULL,			/* no scan params yet               */
	0,			/* current scan index               */
	0,			/* next scan frequency              */
	false,			/* scan ready flag                  */
	0,			/* max channels to scan             */
	0,			/* nr of channels scanned           */
	NULL,			/* default scan function            */
	NULL,			/* default context pointer          */
	0,			/* millisec to wait for demod lock  */
	DRXJ_DEMOD_LOCK,	/* desired lock               */
	false,

	/* Power management */
	DRX_POWER_UP,

	/* Tuner */
	1,			/* nr of I2C port to wich tuner is     */
	0L,			/* minimum RF input frequency, in kHz  */
	0L,			/* maximum RF input frequency, in kHz  */
	false,			/* Rf Agc Polarity                     */
	false,			/* If Agc Polarity                     */
	false,			/* tuner slow mode                     */

	{			/* current channel (all 0)             */
	 0UL			/* channel.frequency */
	 },
	DRX_STANDARD_UNKNOWN,	/* current standard */
	DRX_STANDARD_UNKNOWN,	/* previous standard */
	DRX_STANDARD_UNKNOWN,	/* di_cache_standard   */
	false,			/* use_bootloader */
	0UL,			/* capabilities */
	0			/* mfx */
};

/**
* \var drxj_default_demod_g
* \brief Default drxj demodulator instance.
*/
struct drx_demod_instance drxj_default_demod_g = {
	&drxj_functions_g,	/* demod functions */
	&DRXJ_DAP,		/* data access protocol functions */
	NULL,			/* tuner instance */
	&drxj_default_addr_g,	/* i2c address & device id */
	&drxj_default_comm_attr_g,	/* demod common attributes */
	&drxj_data_g		/* demod device specific attributes */
};

/**
* \brief Default audio data structure for DRK demodulator instance.
*
* This structure is DRXK specific.
*
*/
struct drx_aud_data drxj_default_aud_data_g = {
	false,			/* audio_is_active */
	DRX_AUD_STANDARD_AUTO,	/* audio_standard  */

	/* i2sdata */
	{
	 false,			/* output_enable   */
	 48000,			/* frequency      */
	 DRX_I2S_MODE_MASTER,	/* mode           */
	 DRX_I2S_WORDLENGTH_32,	/* word_length     */
	 DRX_I2S_POLARITY_RIGHT,	/* polarity       */
	 DRX_I2S_FORMAT_WS_WITH_DATA	/* format         */
	 },
	/* volume            */
	{
	 true,			/* mute;          */
	 0,			/* volume         */
	 DRX_AUD_AVC_OFF,	/* avc_mode        */
	 0,			/* avc_ref_level    */
	 DRX_AUD_AVC_MAX_GAIN_12DB,	/* avc_max_gain     */
	 DRX_AUD_AVC_MAX_ATTEN_24DB,	/* avc_max_atten    */
	 0,			/* strength_left   */
	 0			/* strength_right  */
	 },
	DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_ON,	/* auto_sound */
	/*  ass_thresholds */
	{
	 440,			/* A2    */
	 12,			/* BTSC  */
	 700,			/* NICAM */
	 },
	/* carrier */
	{
	 /* a */
	 {
	  42,			/* thres */
	  DRX_NO_CARRIER_NOISE,	/* opt   */
	  0,			/* shift */
	  0			/* dco   */
	  },
	 /* b */
	 {
	  42,			/* thres */
	  DRX_NO_CARRIER_MUTE,	/* opt   */
	  0,			/* shift */
	  0			/* dco   */
	  },

	 },
	/* mixer */
	{
	 DRX_AUD_SRC_STEREO_OR_A,	/* source_i2s */
	 DRX_AUD_I2S_MATRIX_STEREO,	/* matrix_i2s */
	 DRX_AUD_FM_MATRIX_SOUND_A	/* matrix_fm  */
	 },
	DRX_AUD_DEVIATION_NORMAL,	/* deviation */
	DRX_AUD_AVSYNC_OFF,	/* av_sync */

	/* prescale */
	{
	 DRX_AUD_MAX_FM_DEVIATION,	/* fm_deviation */
	 DRX_AUD_MAX_NICAM_PRESCALE	/* nicam_gain */
	 },
	DRX_AUD_FM_DEEMPH_75US,	/* deemph */
	DRX_BTSC_STEREO,	/* btsc_detect */
	0,			/* rds_data_counter */
	false			/* rds_data_present */
};

/*-----------------------------------------------------------------------------
STRUCTURES
----------------------------------------------------------------------------*/
struct drxjeq_stat {
	u16 eq_mse;
	u8 eq_mode;
	u8 eq_ctrl;
	u8 eq_stat;
};

/* HI command */
struct drxj_hi_cmd {
	u16 cmd;
	u16 param1;
	u16 param2;
	u16 param3;
	u16 param4;
	u16 param5;
	u16 param6;
};

/*============================================================================*/
/*=== MICROCODE RELATED STRUCTURES ===========================================*/
/*============================================================================*/

/**
 * struct drxu_code_block_hdr - Structure of the microcode block headers
 *
 * @addr:	Destination address of the data in this block
 * @size:	Size of the block data following this header counted in
 *		16 bits words
 * @CRC:	CRC value of the data block, only valid if CRC flag is
 *		set.
 */
struct drxu_code_block_hdr {
	u32 addr;
	u16 size;
	u16 flags;
	u16 CRC;
};

/*-----------------------------------------------------------------------------
FUNCTIONS
----------------------------------------------------------------------------*/
/* Some prototypes */
static int
hi_command(struct i2c_device_addr *dev_addr,
	   const struct drxj_hi_cmd *cmd, u16 *result);

static int
ctrl_lock_status(struct drx_demod_instance *demod, enum drx_lock_status *lock_stat);

static int
ctrl_power_mode(struct drx_demod_instance *demod, enum drx_power_mode *mode);

static int power_down_aud(struct drx_demod_instance *demod);

#ifndef DRXJ_DIGITAL_ONLY
static int power_up_aud(struct drx_demod_instance *demod, bool set_standard);
#endif

static int
aud_ctrl_set_standard(struct drx_demod_instance *demod, enum drx_aud_standard *standard);

static int
ctrl_set_cfg_pre_saw(struct drx_demod_instance *demod, struct drxj_cfg_pre_saw *pre_saw);

static int
ctrl_set_cfg_afe_gain(struct drx_demod_instance *demod, struct drxj_cfg_afe_gain *afe_gain);

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
static int
ctrl_u_code_upload(struct drx_demod_instance *demod,
		  struct drxu_code_info *mc_info,
		enum drxu_code_actionaction, bool audio_mc_upload);
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

/*============================================================================*/
/*============================================================================*/
/*==                          HELPER FUNCTIONS                              ==*/
/*============================================================================*/
/*============================================================================*/

/**
* \fn void mult32(u32 a, u32 b, u32 *h, u32 *l)
* \brief 32bitsx32bits signed multiplication
* \param a 32 bits multiplicant, typecast from signed to unisgned
* \param b 32 bits multiplier, typecast from signed to unisgned
* \param h pointer to high part 64 bits result, typecast from signed to unisgned
* \param l pointer to low part 64 bits result
*
* For the 2n+n addition a + b:
* if a >= 0, then h += 0 (sign extension = 0)
* but if a < 0, then h += 2^n-1 ==> h -= 1.
*
* Also, if a + b < 2^n, then a + b  >= a && a + b >= b
* but if a + b >= 2^n, then R = a + b - 2^n,
* and because a < 2^n && b < 2*n ==> R < a && R < b.
* Therefore, to detect overflow, simply compare the addition result with
* one of the operands; if the result is smaller, overflow has occurred and
* h must be incremented.
*
* Booth multiplication uses additions and subtractions to reduce the number
* of iterations. This is done by taking three subsequent bits abc and calculating
* the following multiplication factor: -2a + b + c. This factor is multiplied
* by the second operand and added to the result. Next, the first operand is
* shifted two bits (hence one of the three bits is reused) and the process
* repeated. The last iteration has only two bits left, but we simply add
* a zero to the end.
*
* Hence: (n=4)
*  1 * a =  0 * 4a + 1 * a
*  2 * a =  1 * 4a - 2 * a
*  3 * a =  1 * 4a - 1 * a
* -1 * a =  0 * 4a - 1 * a
* -5 * a = -1 * 4a - 1 * a
*
* etc.
*
* Note that the function is type size independent. Any unsigned integer type
* can be substituted for booth_t.
*
*/

#define DRX_IS_BOOTH_NEGATIVE(__a)  (((__a) & (1 << (sizeof(u32) * 8 - 1))) != 0)

static void mult32(u32 a, u32 b, u32 *h, u32 *l)
{
	unsigned int i;
	*h = *l = 0;

	/* n/2 iterations; shift operand a left two bits after each iteration.      */
	/* This automatically appends a zero to the operand for the last iteration. */
	for (i = 0; i < sizeof(a) * 8; i += 2, a = a << 2) {
		/* Shift result left two bits */
		*h = (*h << 2) + (*l >> (sizeof(*l) * 8 - 2));
		*l = (*l << 2);

		/* Take the first three bits of operand a for the Booth conversion: */
		/* 0, 7: do nothing  */
		/* 1, 2: add b       */
		/* 3   : add 2b      */
		/* 4   : subtract 2b */
		/* 5, 6: subtract b  */
		switch (a >> (sizeof(a) * 8 - 3)) {
		case 3:
			*l += b;
			*h = *h - DRX_IS_BOOTH_NEGATIVE(b) + (*l < b);
		case 1:
		case 2:
			*l += b;
			*h = *h - DRX_IS_BOOTH_NEGATIVE(b) + (*l < b);
			break;
		case 4:
			*l -= b;
			*h = *h - !DRX_IS_BOOTH_NEGATIVE(b) + !b + (*l <
								    ((u32)
								     (-
								      ((s32)
								       b))));
		case 5:
		case 6:
			*l -= b;
			*h = *h - !DRX_IS_BOOTH_NEGATIVE(b) + !b + (*l <
								    ((u32)
								     (-
								      ((s32)
								       b))));
			break;
		}
	}
}

/*============================================================================*/

/*
* \fn u32 frac28(u32 N, u32 D)
* \brief Compute: (1<<28)*N/D
* \param N 32 bits
* \param D 32 bits
* \return (1<<28)*N/D
* This function is used to avoid floating-point calculations as they may
* not be present on the target platform.

* frac28 performs an unsigned 28/28 bits division to 32-bit fixed point
* fraction used for setting the Frequency Shifter registers.
* N and D can hold numbers up to width: 28-bits.
* The 4 bits integer part and the 28 bits fractional part are calculated.

* Usage condition: ((1<<28)*n)/d < ((1<<32)-1) => (n/d) < 15.999

* N: 0...(1<<28)-1 = 268435454
* D: 0...(1<<28)-1
* Q: 0...(1<<32)-1
*/
static u32 frac28(u32 N, u32 D)
{
	int i = 0;
	u32 Q1 = 0;
	u32 R0 = 0;

	R0 = (N % D) << 4;	/* 32-28 == 4 shifts possible at max */
	Q1 = N / D;		/* integer part, only the 4 least significant bits
				   will be visible in the result */

	/* division using radix 16, 7 nibbles in the result */
	for (i = 0; i < 7; i++) {
		Q1 = (Q1 << 4) | R0 / D;
		R0 = (R0 % D) << 4;
	}
	/* rounding */
	if ((R0 >> 3) >= D)
		Q1++;

	return Q1;
}

/**
* \fn u32 log1_times100( u32 x)
* \brief Compute: 100*log10(x)
* \param x 32 bits
* \return 100*log10(x)
*
* 100*log10(x)
* = 100*(log2(x)/log2(10)))
* = (100*(2^15)*log2(x))/((2^15)*log2(10))
* = ((200*(2^15)*log2(x))/((2^15)*log2(10)))/2
* = ((200*(2^15)*(log2(x/y)+log2(y)))/((2^15)*log2(10)))/2
* = ((200*(2^15)*log2(x/y))+(200*(2^15)*log2(y)))/((2^15)*log2(10)))/2
*
* where y = 2^k and 1<= (x/y) < 2
*/

static u32 log1_times100(u32 x)
{
	static const u8 scale = 15;
	static const u8 index_width = 5;
	/*
	   log2lut[n] = (1<<scale) * 200 * log2( 1.0 + ( (1.0/(1<<INDEXWIDTH)) * n ))
	   0 <= n < ((1<<INDEXWIDTH)+1)
	 */

	static const u32 log2lut[] = {
		0,		/* 0.000000 */
		290941,		/* 290941.300628 */
		573196,		/* 573196.476418 */
		847269,		/* 847269.179851 */
		1113620,	/* 1113620.489452 */
		1372674,	/* 1372673.576986 */
		1624818,	/* 1624817.752104 */
		1870412,	/* 1870411.981536 */
		2109788,	/* 2109787.962654 */
		2343253,	/* 2343252.817465 */
		2571091,	/* 2571091.461923 */
		2793569,	/* 2793568.696416 */
		3010931,	/* 3010931.055901 */
		3223408,	/* 3223408.452106 */
		3431216,	/* 3431215.635215 */
		3634553,	/* 3634553.498355 */
		3833610,	/* 3833610.244726 */
		4028562,	/* 4028562.434393 */
		4219576,	/* 4219575.925308 */
		4406807,	/* 4406806.721144 */
		4590402,	/* 4590401.736809 */
		4770499,	/* 4770499.491025 */
		4947231,	/* 4947230.734179 */
		5120719,	/* 5120719.018555 */
		5291081,	/* 5291081.217197 */
		5458428,	/* 5458427.996830 */
		5622864,	/* 5622864.249668 */
		5784489,	/* 5784489.488298 */
		5943398,	/* 5943398.207380 */
		6099680,	/* 6099680.215452 */
		6253421,	/* 6253420.939751 */
		6404702,	/* 6404701.706649 */
		6553600,	/* 6553600.000000 */
	};

	u8 i = 0;
	u32 y = 0;
	u32 d = 0;
	u32 k = 0;
	u32 r = 0;

	if (x == 0)
		return 0;

	/* Scale x (normalize) */
	/* computing y in log(x/y) = log(x) - log(y) */
	if ((x & (((u32) (-1)) << (scale + 1))) == 0) {
		for (k = scale; k > 0; k--) {
			if (x & (((u32) 1) << scale))
				break;
			x <<= 1;
		}
	} else {
		for (k = scale; k < 31; k++) {
			if ((x & (((u32) (-1)) << (scale + 1))) == 0)
				break;
			x >>= 1;
		}
	}
	/*
	   Now x has binary point between bit[scale] and bit[scale-1]
	   and 1.0 <= x < 2.0 */

	/* correction for divison: log(x) = log(x/y)+log(y) */
	y = k * ((((u32) 1) << scale) * 200);

	/* remove integer part */
	x &= ((((u32) 1) << scale) - 1);
	/* get index */
	i = (u8) (x >> (scale - index_width));
	/* compute delta (x-a) */
	d = x & ((((u32) 1) << (scale - index_width)) - 1);
	/* compute log, multiplication ( d* (.. )) must be within range ! */
	y += log2lut[i] +
	    ((d * (log2lut[i + 1] - log2lut[i])) >> (scale - index_width));
	/* Conver to log10() */
	y /= 108853;		/* (log2(10) << scale) */
	r = (y >> 1);
	/* rounding */
	if (y & ((u32)1))
		r++;

	return r;

}

/**
* \fn u32 frac_times1e6( u16 N, u32 D)
* \brief Compute: (N/D) * 1000000.
* \param N nominator 16-bits.
* \param D denominator 32-bits.
* \return u32
* \retval ((N/D) * 1000000), 32 bits
*
* No check on D=0!
*/
static u32 frac_times1e6(u32 N, u32 D)
{
	u32 remainder = 0;
	u32 frac = 0;

	/*
	   frac = (N * 1000000) / D
	   To let it fit in a 32 bits computation:
	   frac = (N * (1000000 >> 4)) / (D >> 4)
	   This would result in a problem in case D < 16 (div by 0).
	   So we do it more elaborate as shown below.
	 */
	frac = (((u32) N) * (1000000 >> 4)) / D;
	frac <<= 4;
	remainder = (((u32) N) * (1000000 >> 4)) % D;
	remainder <<= 4;
	frac += remainder / D;
	remainder = remainder % D;
	if ((remainder * 2) > D)
		frac++;

	return frac;
}

/*============================================================================*/

/**
* \brief Compute: 100 * 10^( gd_b / 200 ).
* \param  u32   gd_b      Gain in 0.1dB
* \return u32            Gainfactor in 0.01 resolution
*
*/
static u32 d_b2lin_times100(u32 gd_b)
{
	u32 result = 0;
	u32 nr6d_b_steps = 0;
	u32 remainder = 0;
	u32 remainder_fac = 0;

	/* start with factors 2 (6.02dB) */
	nr6d_b_steps = gd_b * 1000UL / 60206UL;
	if (nr6d_b_steps > 17) {
		/* Result max overflow if > log2( maxu32 / 2e4 ) ~= 17.7 */
		return MAX_U32;
	}
	result = (1 << nr6d_b_steps);

	/* calculate remaining factor,
	   poly approximation of 10^(gd_b/200):

	   y = 1E-04x2 + 0.0106x + 1.0026

	   max deviation < 0.005 for range x = [0 ... 60]
	 */
	remainder = ((gd_b * 1000UL) % 60206UL) / 1000UL;
	/* using 1e-4 for poly calculation  */
	remainder_fac = 1 * remainder * remainder;
	remainder_fac += 106 * remainder;
	remainder_fac += 10026;

	/* multiply by remaining factor */
	result *= remainder_fac;

	/* conversion from 1e-4 to 1e-2 */
	return (result + 50) / 100;
}

#ifndef DRXJ_DIGITAL_ONLY
#define FRAC_FLOOR    0
#define FRAC_CEIL     1
#define FRAC_ROUND    2
/**
* \fn u32 frac( u32 N, u32 D, u16 RC )
* \brief Compute: N/D.
* \param N nominator 32-bits.
* \param D denominator 32-bits.
* \param RC-result correction: 0-floor; 1-ceil; 2-round
* \return u32
* \retval N/D, 32 bits
*
* If D=0 returns 0
*/
static u32 frac(u32 N, u32 D, u16 RC)
{
	u32 remainder = 0;
	u32 frac = 0;
	u16 bit_cnt = 32;

	if (D == 0) {
		frac = 0;
		remainder = 0;

		return frac;
	}

	if (D > N) {
		frac = 0;
		remainder = N;
	} else {
		remainder = 0;
		frac = N;
		while (bit_cnt-- > 0) {
			remainder <<= 1;
			remainder |= ((frac & 0x80000000) >> 31);
			frac <<= 1;
			if (remainder < D) {
				frac &= 0xFFFFFFFE;
			} else {
				remainder -= D;
				frac |= 0x1;
			}
		}

		/* result correction if needed */
		if ((RC == FRAC_CEIL) && (remainder != 0)) {
			/* ceil the result */
			/*(remainder is not zero -> value behind decimal point exists) */
			frac++;
		}
		if ((RC == FRAC_ROUND) && (remainder >= D >> 1)) {
			/* remainder is bigger from D/2 -> round the result */
			frac++;
		}
	}

	return frac;
}
#endif

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
/*============================================================================*/

/**
* \fn u16 u_code_read16( u8 *addr)
* \brief Read a 16 bits word, expect big endian data.
* \return u16 The data read.
*/
static u16 u_code_read16(u8 *addr)
{
	/* Works fo any host processor */

	u16 word = 0;

	word = ((u16) addr[0]);
	word <<= 8;
	word |= ((u16) addr[1]);

	return word;
}

/*============================================================================*/

/**
* \fn u32 u_code_read32( u8 *addr)
* \brief Read a 32 bits word, expect big endian data.
* \return u32 The data read.
*/
static u32 u_code_read32(u8 *addr)
{
	/* Works fo any host processor */

	u32 word = 0;

	word = ((u16) addr[0]);
	word <<= 8;
	word |= ((u16) addr[1]);
	word <<= 8;
	word |= ((u16) addr[2]);
	word <<= 8;
	word |= ((u16) addr[3]);

	return word;
}

/*============================================================================*/

/**
* \fn u16 u_code_compute_crc (u8 *block_data, u16 nr_words)
* \brief Compute CRC of block of microcode data.
* \param block_data Pointer to microcode data.
* \param nr_words Size of microcode block (number of 16 bits words).
* \return u16 The computed CRC residu.
*/
static u16 u_code_compute_crc(u8 *block_data, u16 nr_words)
{
	u16 i = 0;
	u16 j = 0;
	u32 crc_word = 0;
	u32 carry = 0;

	while (i < nr_words) {
		crc_word |= (u32) u_code_read16(block_data);
		for (j = 0; j < 16; j++) {
			crc_word <<= 1;
			if (carry != 0)
				crc_word ^= 0x80050000UL;
			carry = crc_word & 0x80000000UL;
		}
		i++;
		block_data += (sizeof(u16));
	}
	return (u16)(crc_word >> 16);
}
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

/**
* \brief Values for NICAM prescaler gain. Computed from dB to integer
*        and rounded. For calc used formula: 16*10^(prescaleGain[dB]/20).
*
*/
static const u16 nicam_presc_table_val[43] = {
	1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4,
	5, 5, 6, 6, 7, 8, 9, 10, 11, 13, 14, 16,
	18, 20, 23, 25, 28, 32, 36, 40, 45,
	51, 57, 64, 71, 80, 90, 101, 113, 127
};

/*============================================================================*/
/*==                        END HELPER FUNCTIONS                            ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                      DRXJ DAP FUNCTIONS                                ==*/
/*============================================================================*/
/*============================================================================*/

/*
   This layer takes care of some device specific register access protocols:
   -conversion to short address format
   -access to audio block
   This layer is placed between the drx_dap_fasi and the rest of the drxj
   specific implementation. This layer can use address map knowledge whereas
   dap_fasi may not use memory map knowledge.

   * For audio currently only 16 bits read and write register access is
     supported. More is not needed. RMW and 32 or 8 bit access on audio
     registers will have undefined behaviour. Flags (RMW, CRC reset, broadcast
     single/multi master) will be ignored.

   TODO: check ignoring single/multimaster is ok for AUD access ?
*/

#define DRXJ_ISAUDWRITE(addr) (((((addr)>>16)&1) == 1) ? true : false)
#define DRXJ_DAP_AUDTRIF_TIMEOUT 80	/* millisec */
/*============================================================================*/

/**
* \fn bool is_handled_by_aud_tr_if( u32 addr )
* \brief Check if this address is handled by the audio token ring interface.
* \param addr
* \return bool
* \retval true  Yes, handled by audio token ring interface
* \retval false No, not handled by audio token ring interface
*
*/
static
bool is_handled_by_aud_tr_if(u32 addr)
{
	bool retval = false;

	if ((DRXDAP_FASI_ADDR2BLOCK(addr) == 4) &&
	    (DRXDAP_FASI_ADDR2BANK(addr) > 1) &&
	    (DRXDAP_FASI_ADDR2BANK(addr) < 6)) {
		retval = true;
	}

	return retval;
}

/*============================================================================*/

static int drxj_dap_read_block(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u16 datasize,
				      u8 *data, u32 flags)
{
	return drx_dap_fasi_funct_g.read_block_func(dev_addr,
					       addr, datasize, data, flags);
}

/*============================================================================*/

static int drxj_dap_read_modify_write_reg8(struct i2c_device_addr *dev_addr,
						u32 waddr,
						u32 raddr,
						u8 wdata, u8 *rdata)
{
	return drx_dap_fasi_funct_g.read_modify_write_reg8func(dev_addr,
							 waddr,
							 raddr, wdata, rdata);
}

/*============================================================================*/

/**
* \fn int drxj_dap_rm_write_reg16short
* \brief Read modify write 16 bits audio register using short format only.
* \param dev_addr
* \param waddr    Address to write to
* \param raddr    Address to read from (usually SIO_HI_RA_RAM_S0_RMWBUF__A)
* \param wdata    Data to write
* \param rdata    Buffer for data to read
* \return int
* \retval 0 Succes
* \retval -EIO Timeout, I2C error, illegal bank
*
* 16 bits register read modify write access using short addressing format only.
* Requires knowledge of the registermap, thus device dependent.
* Using DAP FASI directly to avoid endless recursion of RMWs to audio registers.
*
*/

/* TODO correct define should be #if ( DRXDAPFASI_SHORT_ADDR_ALLOWED==1 )
   See comments drxj_dap_read_modify_write_reg16 */
#if (DRXDAPFASI_LONG_ADDR_ALLOWED == 0)
static int drxj_dap_rm_write_reg16short(struct i2c_device_addr *dev_addr,
					      u32 waddr,
					      u32 raddr,
					      u16 wdata, u16 *rdata)
{
	int rc;

	if (rdata == NULL)
		return -EINVAL;

	/* Set RMW flag */
	rc = drx_dap_fasi_funct_g.write_reg16func(dev_addr,
					      SIO_HI_RA_RAM_S0_FLG_ACC__A,
					      SIO_HI_RA_RAM_S0_FLG_ACC_S0_RWM__M,
					      0x0000);
	if (rc == 0) {
		/* Write new data: triggers RMW */
		rc = drx_dap_fasi_funct_g.write_reg16func(dev_addr, waddr, wdata,
						      0x0000);
	}
	if (rc == 0) {
		/* Read old data */
		rc = drx_dap_fasi_funct_g.read_reg16func(dev_addr, raddr, rdata,
						     0x0000);
	}
	if (rc == 0) {
		/* Reset RMW flag */
		rc = drx_dap_fasi_funct_g.write_reg16func(dev_addr,
						      SIO_HI_RA_RAM_S0_FLG_ACC__A,
						      0, 0x0000);
	}

	return rc;
}
#endif

/*============================================================================*/

static int drxj_dap_read_modify_write_reg16(struct i2c_device_addr *dev_addr,
						 u32 waddr,
						 u32 raddr,
						 u16 wdata, u16 *rdata)
{
	/* TODO: correct short/long addressing format decision,
	   now long format has higher prio then short because short also
	   needs virt bnks (not impl yet) for certain audio registers */
#if (DRXDAPFASI_LONG_ADDR_ALLOWED == 1)
	return drx_dap_fasi_funct_g.read_modify_write_reg16func(dev_addr,
							  waddr,
							  raddr, wdata, rdata);
#else
	return drxj_dap_rm_write_reg16short(dev_addr, waddr, raddr, wdata, rdata);
#endif
}

/*============================================================================*/

static int drxj_dap_read_modify_write_reg32(struct i2c_device_addr *dev_addr,
						 u32 waddr,
						 u32 raddr,
						 u32 wdata, u32 *rdata)
{
	return drx_dap_fasi_funct_g.read_modify_write_reg32func(dev_addr,
							  waddr,
							  raddr, wdata, rdata);
}

/*============================================================================*/

static int drxj_dap_read_reg8(struct i2c_device_addr *dev_addr,
				     u32 addr,
				     u8 *data, u32 flags)
{
	return drx_dap_fasi_funct_g.read_reg8func(dev_addr, addr, data, flags);
}

/*============================================================================*/

/**
* \fn int drxj_dap_read_aud_reg16
* \brief Read 16 bits audio register
* \param dev_addr
* \param addr
* \param data
* \return int
* \retval 0 Succes
* \retval -EIO Timeout, I2C error, illegal bank
*
* 16 bits register read access via audio token ring interface.
*
*/
static int drxj_dap_read_aud_reg16(struct i2c_device_addr *dev_addr,
					 u32 addr, u16 *data)
{
	u32 start_timer = 0;
	u32 current_timer = 0;
	u32 delta_timer = 0;
	u16 tr_status = 0;
	int stat = -EIO;

	/* No read possible for bank 3, return with error */
	if (DRXDAP_FASI_ADDR2BANK(addr) == 3) {
		stat = -EINVAL;
	} else {
		const u32 write_bit = ((dr_xaddr_t) 1) << 16;

		/* Force reset write bit */
		addr &= (~write_bit);

		/* Set up read */
		start_timer = drxbsp_hst_clock();
		do {
			/* RMW to aud TR IF until request is granted or timeout */
			stat = drxj_dap_read_modify_write_reg16(dev_addr,
							     addr,
							     SIO_HI_RA_RAM_S0_RMWBUF__A,
							     0x0000, &tr_status);

			if (stat != 0)
				break;

			current_timer = drxbsp_hst_clock();
			delta_timer = current_timer - start_timer;
			if (delta_timer > DRXJ_DAP_AUDTRIF_TIMEOUT) {
				stat = -EIO;
				break;
			}

		} while (((tr_status & AUD_TOP_TR_CTR_FIFO_LOCK__M) ==
			  AUD_TOP_TR_CTR_FIFO_LOCK_LOCKED) ||
			 ((tr_status & AUD_TOP_TR_CTR_FIFO_FULL__M) ==
			  AUD_TOP_TR_CTR_FIFO_FULL_FULL));
	}			/* if ( DRXDAP_FASI_ADDR2BANK(addr)!=3 ) */

	/* Wait for read ready status or timeout */
	if (stat == 0) {
		start_timer = drxbsp_hst_clock();

		while ((tr_status & AUD_TOP_TR_CTR_FIFO_RD_RDY__M) !=
		       AUD_TOP_TR_CTR_FIFO_RD_RDY_READY) {
			stat = drxj_dap_read_reg16(dev_addr,
						  AUD_TOP_TR_CTR__A,
						  &tr_status, 0x0000);
			if (stat != 0)
				break;

			current_timer = drxbsp_hst_clock();
			delta_timer = current_timer - start_timer;
			if (delta_timer > DRXJ_DAP_AUDTRIF_TIMEOUT) {
				stat = -EIO;
				break;
			}
		}		/* while ( ... ) */
	}

	/* Read value */
	if (stat == 0)
		stat = drxj_dap_read_modify_write_reg16(dev_addr,
						     AUD_TOP_TR_RD_REG__A,
						     SIO_HI_RA_RAM_S0_RMWBUF__A,
						     0x0000, data);
	return stat;
}

/*============================================================================*/

static int drxj_dap_read_reg16(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u16 *data, u32 flags)
{
	int stat = -EIO;

	/* Check param */
	if ((dev_addr == NULL) || (data == NULL))
		return -EINVAL;

	if (is_handled_by_aud_tr_if(addr))
		stat = drxj_dap_read_aud_reg16(dev_addr, addr, data);
	else
		stat = drx_dap_fasi_funct_g.read_reg16func(dev_addr,
							   addr, data, flags);

	return stat;
}

/*============================================================================*/

static int drxj_dap_read_reg32(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u32 *data, u32 flags)
{
	return drx_dap_fasi_funct_g.read_reg32func(dev_addr, addr, data, flags);
}

/*============================================================================*/

static int drxj_dap_write_block(struct i2c_device_addr *dev_addr,
				       u32 addr,
				       u16 datasize,
				       u8 *data, u32 flags)
{
	return drx_dap_fasi_funct_g.write_block_func(dev_addr,
						addr, datasize, data, flags);
}

/*============================================================================*/

static int drxj_dap_write_reg8(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u8 data, u32 flags)
{
	return drx_dap_fasi_funct_g.write_reg8func(dev_addr, addr, data, flags);
}

/*============================================================================*/

/**
* \fn int drxj_dap_write_aud_reg16
* \brief Write 16 bits audio register
* \param dev_addr
* \param addr
* \param data
* \return int
* \retval 0 Succes
* \retval -EIO Timeout, I2C error, illegal bank
*
* 16 bits register write access via audio token ring interface.
*
*/
static int drxj_dap_write_aud_reg16(struct i2c_device_addr *dev_addr,
					  u32 addr, u16 data)
{
	int stat = -EIO;

	/* No write possible for bank 2, return with error */
	if (DRXDAP_FASI_ADDR2BANK(addr) == 2) {
		stat = -EINVAL;
	} else {
		u32 start_timer = 0;
		u32 current_timer = 0;
		u32 delta_timer = 0;
		u16 tr_status = 0;
		const u32 write_bit = ((dr_xaddr_t) 1) << 16;

		/* Force write bit */
		addr |= write_bit;
		start_timer = drxbsp_hst_clock();
		do {
			/* RMW to aud TR IF until request is granted or timeout */
			stat = drxj_dap_read_modify_write_reg16(dev_addr,
							     addr,
							     SIO_HI_RA_RAM_S0_RMWBUF__A,
							     data, &tr_status);
			if (stat != 0)
				break;

			current_timer = drxbsp_hst_clock();
			delta_timer = current_timer - start_timer;
			if (delta_timer > DRXJ_DAP_AUDTRIF_TIMEOUT) {
				stat = -EIO;
				break;
			}

		} while (((tr_status & AUD_TOP_TR_CTR_FIFO_LOCK__M) ==
			  AUD_TOP_TR_CTR_FIFO_LOCK_LOCKED) ||
			 ((tr_status & AUD_TOP_TR_CTR_FIFO_FULL__M) ==
			  AUD_TOP_TR_CTR_FIFO_FULL_FULL));

	}			/* if ( DRXDAP_FASI_ADDR2BANK(addr)!=2 ) */

	return stat;
}

/*============================================================================*/

static int drxj_dap_write_reg16(struct i2c_device_addr *dev_addr,
				       u32 addr,
				       u16 data, u32 flags)
{
	int stat = -EIO;

	/* Check param */
	if (dev_addr == NULL)
		return -EINVAL;

	if (is_handled_by_aud_tr_if(addr))
		stat = drxj_dap_write_aud_reg16(dev_addr, addr, data);
	else
		stat = drx_dap_fasi_funct_g.write_reg16func(dev_addr,
							    addr, data, flags);

	return stat;
}

/*============================================================================*/

static int drxj_dap_write_reg32(struct i2c_device_addr *dev_addr,
				       u32 addr,
				       u32 data, u32 flags)
{
	return drx_dap_fasi_funct_g.write_reg32func(dev_addr, addr, data, flags);
}

/*============================================================================*/

/* Free data ram in SIO HI */
#define SIO_HI_RA_RAM_USR_BEGIN__A 0x420040
#define SIO_HI_RA_RAM_USR_END__A   0x420060

#define DRXJ_HI_ATOMIC_BUF_START (SIO_HI_RA_RAM_USR_BEGIN__A)
#define DRXJ_HI_ATOMIC_BUF_END   (SIO_HI_RA_RAM_USR_BEGIN__A + 7)
#define DRXJ_HI_ATOMIC_READ      SIO_HI_RA_RAM_PAR_3_ACP_RW_READ
#define DRXJ_HI_ATOMIC_WRITE     SIO_HI_RA_RAM_PAR_3_ACP_RW_WRITE

/**
* \fn int drxj_dap_atomic_read_write_block()
* \brief Basic access routine for atomic read or write access
* \param dev_addr  pointer to i2c dev address
* \param addr     destination/source address
* \param datasize size of data buffer in bytes
* \param data     pointer to data buffer
* \return int
* \retval 0 Succes
* \retval -EIO Timeout, I2C error, illegal bank
*
*/
static
int drxj_dap_atomic_read_write_block(struct i2c_device_addr *dev_addr,
					  u32 addr,
					  u16 datasize,
					  u8 *data, bool read_flag)
{
	struct drxj_hi_cmd hi_cmd;
	int rc;
	u16 word;
	u16 dummy = 0;
	u16 i = 0;

	/* Parameter check */
	if (!data || !dev_addr || ((datasize % 2)) || ((datasize / 2) > 8))
		return -EINVAL;

	/* Set up HI parameters to read or write n bytes */
	hi_cmd.cmd = SIO_HI_RA_RAM_CMD_ATOMIC_COPY;
	hi_cmd.param1 =
	    (u16) ((DRXDAP_FASI_ADDR2BLOCK(DRXJ_HI_ATOMIC_BUF_START) << 6) +
		     DRXDAP_FASI_ADDR2BANK(DRXJ_HI_ATOMIC_BUF_START));
	hi_cmd.param2 =
	    (u16) DRXDAP_FASI_ADDR2OFFSET(DRXJ_HI_ATOMIC_BUF_START);
	hi_cmd.param3 = (u16) ((datasize / 2) - 1);
	if (!read_flag)
		hi_cmd.param3 |= DRXJ_HI_ATOMIC_WRITE;
	else
		hi_cmd.param3 |= DRXJ_HI_ATOMIC_READ;
	hi_cmd.param4 = (u16) ((DRXDAP_FASI_ADDR2BLOCK(addr) << 6) +
				DRXDAP_FASI_ADDR2BANK(addr));
	hi_cmd.param5 = (u16) DRXDAP_FASI_ADDR2OFFSET(addr);

	if (!read_flag) {
		/* write data to buffer */
		for (i = 0; i < (datasize / 2); i++) {

			word = ((u16) data[2 * i]);
			word += (((u16) data[(2 * i) + 1]) << 8);
			drxj_dap_write_reg16(dev_addr,
					     (DRXJ_HI_ATOMIC_BUF_START + i),
					    word, 0);
		}
	}

	rc = hi_command(dev_addr, &hi_cmd, &dummy);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (read_flag) {
		/* read data from buffer */
		for (i = 0; i < (datasize / 2); i++) {
			drxj_dap_read_reg16(dev_addr,
					    (DRXJ_HI_ATOMIC_BUF_START + i),
					   &word, 0);
			data[2 * i] = (u8) (word & 0xFF);
			data[(2 * i) + 1] = (u8) (word >> 8);
		}
	}

	return 0;

rw_error:
	return -EIO;

}

/*============================================================================*/

/**
* \fn int drxj_dap_atomic_read_reg32()
* \brief Atomic read of 32 bits words
*/
static
int drxj_dap_atomic_read_reg32(struct i2c_device_addr *dev_addr,
				     u32 addr,
				     u32 *data, u32 flags)
{
	u8 buf[sizeof(*data)];
	int rc = -EIO;
	u32 word = 0;

	if (!data)
		return -EINVAL;

	rc = drxj_dap_atomic_read_write_block(dev_addr, addr,
					      sizeof(*data), buf, true);

	if (rc < 0)
		return 0;

	word = (u32) buf[3];
	word <<= 8;
	word |= (u32) buf[2];
	word <<= 8;
	word |= (u32) buf[1];
	word <<= 8;
	word |= (u32) buf[0];

	*data = word;

	return rc;
}

/*============================================================================*/

/*============================================================================*/
/*==                        END DRXJ DAP FUNCTIONS                          ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                      HOST INTERFACE FUNCTIONS                          ==*/
/*============================================================================*/
/*============================================================================*/

/**
* \fn int hi_cfg_command()
* \brief Configure HI with settings stored in the demod structure.
* \param demod Demodulator.
* \return int.
*
* This routine was created because to much orthogonal settings have
* been put into one HI API function (configure). Especially the I2C bridge
* enable/disable should not need re-configuration of the HI.
*
*/
static int hi_cfg_command(const struct drx_demod_instance *demod)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	struct drxj_hi_cmd hi_cmd;
	u16 result = 0;
	int rc;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	hi_cmd.cmd = SIO_HI_RA_RAM_CMD_CONFIG;
	hi_cmd.param1 = SIO_HI_RA_RAM_PAR_1_PAR1_SEC_KEY;
	hi_cmd.param2 = ext_attr->hi_cfg_timing_div;
	hi_cmd.param3 = ext_attr->hi_cfg_bridge_delay;
	hi_cmd.param4 = ext_attr->hi_cfg_wake_up_key;
	hi_cmd.param5 = ext_attr->hi_cfg_ctrl;
	hi_cmd.param6 = ext_attr->hi_cfg_transmit;

	rc = hi_command(demod->my_i2c_dev_addr, &hi_cmd, &result);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Reset power down flag (set one call only) */
	ext_attr->hi_cfg_ctrl &= (~(SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ));

	return 0;

rw_error:
	return -EIO;
}

/**
* \fn int hi_command()
* \brief Configure HI with settings stored in the demod structure.
* \param dev_addr I2C address.
* \param cmd HI command.
* \param result HI command result.
* \return int.
*
* Sends command to HI
*
*/
static int
hi_command(struct i2c_device_addr *dev_addr, const struct drxj_hi_cmd *cmd, u16 *result)
{
	u16 wait_cmd = 0;
	u16 nr_retries = 0;
	bool powerdown_cmd = false;
	int rc;

	/* Write parameters */
	switch (cmd->cmd) {

	case SIO_HI_RA_RAM_CMD_CONFIG:
	case SIO_HI_RA_RAM_CMD_ATOMIC_COPY:
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_HI_RA_RAM_PAR_6__A, cmd->param6, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_HI_RA_RAM_PAR_5__A, cmd->param5, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_HI_RA_RAM_PAR_4__A, cmd->param4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_HI_RA_RAM_PAR_3__A, cmd->param3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* fallthrough */
	case SIO_HI_RA_RAM_CMD_BRDCTRL:
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_HI_RA_RAM_PAR_2__A, cmd->param2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_HI_RA_RAM_PAR_1__A, cmd->param1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* fallthrough */
	case SIO_HI_RA_RAM_CMD_NULL:
		/* No parameters */
		break;

	default:
		return -EINVAL;
		break;
	}

	/* Write command */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_HI_RA_RAM_CMD__A, cmd->cmd, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if ((cmd->cmd) == SIO_HI_RA_RAM_CMD_RESET)
		drxbsp_hst_sleep(1);

	/* Detect power down to ommit reading result */
	powerdown_cmd = (bool) ((cmd->cmd == SIO_HI_RA_RAM_CMD_CONFIG) &&
				  (((cmd->
				     param5) & SIO_HI_RA_RAM_PAR_5_CFG_SLEEP__M)
				   == SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ));
	if (!powerdown_cmd) {
		/* Wait until command rdy */
		do {
			nr_retries++;
			if (nr_retries > DRXJ_MAX_RETRIES) {
				pr_err("timeout\n");
				goto rw_error;
			}

			rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_HI_RA_RAM_CMD__A, &wait_cmd, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} while (wait_cmd != 0);

		/* Read result */
		rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_HI_RA_RAM_RES__A, result, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

	}
	/* if ( powerdown_cmd == true ) */
	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int init_hi( const struct drx_demod_instance *demod )
* \brief Initialise and configurate HI.
* \param demod pointer to demod data.
* \return int Return status.
* \retval 0 Success.
* \retval -EIO Failure.
*
* Needs to know Psys (System Clock period) and Posc (Osc Clock period)
* Need to store configuration in driver because of the way I2C
* bridging is controlled.
*
*/
static int init_hi(const struct drx_demod_instance *demod)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	struct drx_common_attr *common_attr = (struct drx_common_attr *) (NULL);
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	int rc;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;
	dev_addr = demod->my_i2c_dev_addr;

	/* PATCH for bug 5003, HI ucode v3.1.0 */
	rc = DRXJ_DAP.write_reg16func(dev_addr, 0x4301D7, 0x801, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Timing div, 250ns/Psys */
	/* Timing div, = ( delay (nano seconds) * sysclk (kHz) )/ 1000 */
	ext_attr->hi_cfg_timing_div =
	    (u16) ((common_attr->sys_clock_freq / 1000) * HI_I2C_DELAY) / 1000;
	/* Clipping */
	if ((ext_attr->hi_cfg_timing_div) > SIO_HI_RA_RAM_PAR_2_CFG_DIV__M)
		ext_attr->hi_cfg_timing_div = SIO_HI_RA_RAM_PAR_2_CFG_DIV__M;
	/* Bridge delay, uses oscilator clock */
	/* Delay = ( delay (nano seconds) * oscclk (kHz) )/ 1000 */
	/* SDA brdige delay */
	ext_attr->hi_cfg_bridge_delay =
	    (u16) ((common_attr->osc_clock_freq / 1000) * HI_I2C_BRIDGE_DELAY) /
	    1000;
	/* Clipping */
	if ((ext_attr->hi_cfg_bridge_delay) > SIO_HI_RA_RAM_PAR_3_CFG_DBL_SDA__M)
		ext_attr->hi_cfg_bridge_delay = SIO_HI_RA_RAM_PAR_3_CFG_DBL_SDA__M;
	/* SCL bridge delay, same as SDA for now */
	ext_attr->hi_cfg_bridge_delay += ((ext_attr->hi_cfg_bridge_delay) <<
				      SIO_HI_RA_RAM_PAR_3_CFG_DBL_SCL__B);
	/* Wakeup key, setting the read flag (as suggest in the documentation) does
	   not always result into a working solution (barebones worked VI2C failed).
	   Not setting the bit works in all cases . */
	ext_attr->hi_cfg_wake_up_key = DRXJ_WAKE_UP_KEY;
	/* port/bridge/power down ctrl */
	ext_attr->hi_cfg_ctrl = (SIO_HI_RA_RAM_PAR_5_CFG_SLV0_SLAVE);
	/* transit mode time out delay and watch dog divider */
	ext_attr->hi_cfg_transmit = SIO_HI_RA_RAM_PAR_6__PRE;

	rc = hi_cfg_command(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;

rw_error:
	return -EIO;
}

/*============================================================================*/
/*==                   END HOST INTERFACE FUNCTIONS                         ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                        AUXILIARY FUNCTIONS                             ==*/
/*============================================================================*/
/*============================================================================*/

/**
* \fn int get_device_capabilities()
* \brief Get and store device capabilities.
* \param demod  Pointer to demodulator instance.
* \return int.
* \return 0    Success
* \retval -EIO Failure
*
* Depending on pulldowns on MDx pins the following internals are set:
*  * common_attr->osc_clock_freq
*  * ext_attr->has_lna
*  * ext_attr->has_ntsc
*  * ext_attr->has_btsc
*  * ext_attr->has_oob
*
*/
static int get_device_capabilities(struct drx_demod_instance *demod)
{
	struct drx_common_attr *common_attr = (struct drx_common_attr *) (NULL);
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	u16 sio_pdr_ohw_cfg = 0;
	u32 sio_top_jtagid_lo = 0;
	u16 bid = 0;
	int rc;

	common_attr = (struct drx_common_attr *) demod->my_common_attr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	dev_addr = demod->my_i2c_dev_addr;

	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_PDR_OHW_CFG__A, &sio_pdr_ohw_cfg, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY__PRE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	switch ((sio_pdr_ohw_cfg & SIO_PDR_OHW_CFG_FREF_SEL__M)) {
	case 0:
		/* ignore (bypass ?) */
		break;
	case 1:
		/* 27 MHz */
		common_attr->osc_clock_freq = 27000;
		break;
	case 2:
		/* 20.25 MHz */
		common_attr->osc_clock_freq = 20250;
		break;
	case 3:
		/* 4 MHz */
		common_attr->osc_clock_freq = 4000;
		break;
	default:
		return -EIO;
	}

	/*
	   Determine device capabilities
	   Based on pinning v47
	 */
	rc = DRXJ_DAP.read_reg32func(dev_addr, SIO_TOP_JTAGID_LO__A, &sio_top_jtagid_lo, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->mfx = (u8) ((sio_top_jtagid_lo >> 29) & 0xF);

	switch ((sio_top_jtagid_lo >> 12) & 0xFF) {
	case 0x31:
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_PDR_UIO_IN_HI__A, &bid, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		bid = (bid >> 10) & 0xf;
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY__PRE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		ext_attr->has_lna = true;
		ext_attr->has_ntsc = false;
		ext_attr->has_btsc = false;
		ext_attr->has_oob = false;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = false;
		ext_attr->has_gpio = false;
		ext_attr->has_irqn = false;
		break;
	case 0x33:
		ext_attr->has_lna = false;
		ext_attr->has_ntsc = false;
		ext_attr->has_btsc = false;
		ext_attr->has_oob = false;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = false;
		ext_attr->has_gpio = false;
		ext_attr->has_irqn = false;
		break;
	case 0x45:
		ext_attr->has_lna = true;
		ext_attr->has_ntsc = true;
		ext_attr->has_btsc = false;
		ext_attr->has_oob = false;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = true;
		ext_attr->has_gpio = true;
		ext_attr->has_irqn = false;
		break;
	case 0x46:
		ext_attr->has_lna = false;
		ext_attr->has_ntsc = true;
		ext_attr->has_btsc = false;
		ext_attr->has_oob = false;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = true;
		ext_attr->has_gpio = true;
		ext_attr->has_irqn = false;
		break;
	case 0x41:
		ext_attr->has_lna = true;
		ext_attr->has_ntsc = true;
		ext_attr->has_btsc = true;
		ext_attr->has_oob = false;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = true;
		ext_attr->has_gpio = true;
		ext_attr->has_irqn = false;
		break;
	case 0x43:
		ext_attr->has_lna = false;
		ext_attr->has_ntsc = true;
		ext_attr->has_btsc = true;
		ext_attr->has_oob = false;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = true;
		ext_attr->has_gpio = true;
		ext_attr->has_irqn = false;
		break;
	case 0x32:
		ext_attr->has_lna = true;
		ext_attr->has_ntsc = false;
		ext_attr->has_btsc = false;
		ext_attr->has_oob = true;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = true;
		ext_attr->has_gpio = true;
		ext_attr->has_irqn = true;
		break;
	case 0x34:
		ext_attr->has_lna = false;
		ext_attr->has_ntsc = true;
		ext_attr->has_btsc = true;
		ext_attr->has_oob = true;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = true;
		ext_attr->has_gpio = true;
		ext_attr->has_irqn = true;
		break;
	case 0x42:
		ext_attr->has_lna = true;
		ext_attr->has_ntsc = true;
		ext_attr->has_btsc = true;
		ext_attr->has_oob = true;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = true;
		ext_attr->has_gpio = true;
		ext_attr->has_irqn = true;
		break;
	case 0x44:
		ext_attr->has_lna = false;
		ext_attr->has_ntsc = true;
		ext_attr->has_btsc = true;
		ext_attr->has_oob = true;
		ext_attr->has_smatx = true;
		ext_attr->has_smarx = true;
		ext_attr->has_gpio = true;
		ext_attr->has_irqn = true;
		break;
	default:
		/* Unknown device variant */
		return -EIO;
		break;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int power_up_device()
* \brief Power up device.
* \param demod  Pointer to demodulator instance.
* \return int.
* \return 0    Success
* \retval -EIO Failure, I2C or max retries reached
*
*/

#ifndef DRXJ_MAX_RETRIES_POWERUP
#define DRXJ_MAX_RETRIES_POWERUP 10
#endif

static int power_up_device(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	u8 data = 0;
	u16 retry_count = 0;
	struct i2c_device_addr wake_up_addr;

	dev_addr = demod->my_i2c_dev_addr;
	wake_up_addr.i2c_addr = DRXJ_WAKE_UP_KEY;
	wake_up_addr.i2c_dev_id = dev_addr->i2c_dev_id;
	wake_up_addr.user_data = dev_addr->user_data;
	/*
	 * I2C access may fail in this case: no ack
	 * dummy write must be used to wake uop device, dummy read must be used to
	 * reset HI state machine (avoiding actual writes)
	 */
	do {
		data = 0;
		drxbsp_i2c_write_read(&wake_up_addr, 1, &data,
				      (struct i2c_device_addr *)(NULL), 0,
				     (u8 *)(NULL));
		drxbsp_hst_sleep(10);
		retry_count++;
	} while ((drxbsp_i2c_write_read
		  ((struct i2c_device_addr *) (NULL), 0, (u8 *)(NULL), dev_addr, 1,
		   &data)
		  != 0) && (retry_count < DRXJ_MAX_RETRIES_POWERUP));

	/* Need some recovery time .... */
	drxbsp_hst_sleep(10);

	if (retry_count == DRXJ_MAX_RETRIES_POWERUP)
		return -EIO;

	return 0;
}

/*----------------------------------------------------------------------------*/
/* MPEG Output Configuration Functions - begin                                */
/*----------------------------------------------------------------------------*/
/**
* \fn int ctrl_set_cfg_mpeg_output()
* \brief Set MPEG output configuration of the device.
* \param devmod  Pointer to demodulator instance.
* \param cfg_data Pointer to mpeg output configuaration.
* \return int.
*
*  Configure MPEG output parameters.
*
*/
static int
ctrl_set_cfg_mpeg_output(struct drx_demod_instance *demod, struct drx_cfg_mpeg_output *cfg_data)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	struct drx_common_attr *common_attr = (struct drx_common_attr *) (NULL);
	int rc;
	u16 fec_oc_reg_mode = 0;
	u16 fec_oc_reg_ipr_mode = 0;
	u16 fec_oc_reg_ipr_invert = 0;
	u32 max_bit_rate = 0;
	u32 rcn_rate = 0;
	u32 nr_bits = 0;
	u16 sio_pdr_md_cfg = 0;
	/* data mask for the output data byte */
	u16 invert_data_mask =
	    FEC_OC_IPR_INVERT_MD7__M | FEC_OC_IPR_INVERT_MD6__M |
	    FEC_OC_IPR_INVERT_MD5__M | FEC_OC_IPR_INVERT_MD4__M |
	    FEC_OC_IPR_INVERT_MD3__M | FEC_OC_IPR_INVERT_MD2__M |
	    FEC_OC_IPR_INVERT_MD1__M | FEC_OC_IPR_INVERT_MD0__M;

	/* check arguments */
	if ((demod == NULL) || (cfg_data == NULL))
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;

	if (cfg_data->enable_mpeg_output == true) {
		/* quick and dirty patch to set MPEG incase current std is not
		   producing MPEG */
		switch (ext_attr->standard) {
		case DRX_STANDARD_8VSB:
		case DRX_STANDARD_ITU_A:
		case DRX_STANDARD_ITU_B:
		case DRX_STANDARD_ITU_C:
			break;
		default:
			/* not an MPEG producing std, just store MPEG cfg */
			common_attr->mpeg_cfg.enable_mpeg_output =
			    cfg_data->enable_mpeg_output;
			common_attr->mpeg_cfg.insert_rs_byte =
			    cfg_data->insert_rs_byte;
			common_attr->mpeg_cfg.enable_parallel =
			    cfg_data->enable_parallel;
			common_attr->mpeg_cfg.invert_data = cfg_data->invert_data;
			common_attr->mpeg_cfg.invert_err = cfg_data->invert_err;
			common_attr->mpeg_cfg.invert_str = cfg_data->invert_str;
			common_attr->mpeg_cfg.invert_val = cfg_data->invert_val;
			common_attr->mpeg_cfg.invert_clk = cfg_data->invert_clk;
			common_attr->mpeg_cfg.static_clk = cfg_data->static_clk;
			common_attr->mpeg_cfg.bitrate = cfg_data->bitrate;
			return 0;
		}

		rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_OCR_INVERT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		switch (ext_attr->standard) {
		case DRX_STANDARD_8VSB:
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_FCT_USAGE__A, 7, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* 2048 bytes fifo ram */
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_TMD_CTL_UPD_RATE__A, 10, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_TMD_INT_UPD_RATE__A, 10, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_AVR_PARM_A__A, 5, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_AVR_PARM_B__A, 7, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_RCN_GAIN__A, 10, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			/* Low Water Mark for synchronization  */
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_SNC_LWM__A, 3, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			/* High Water Mark for synchronization */
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_SNC_HWM__A, 5, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_STANDARD_ITU_A:
		case DRX_STANDARD_ITU_C:
			switch (ext_attr->constellation) {
			case DRX_CONSTELLATION_QAM256:
				nr_bits = 8;
				break;
			case DRX_CONSTELLATION_QAM128:
				nr_bits = 7;
				break;
			case DRX_CONSTELLATION_QAM64:
				nr_bits = 6;
				break;
			case DRX_CONSTELLATION_QAM32:
				nr_bits = 5;
				break;
			case DRX_CONSTELLATION_QAM16:
				nr_bits = 4;
				break;
			default:
				return -EIO;
			}	/* ext_attr->constellation */
			/* max_bit_rate = symbol_rate * nr_bits * coef */
			/* coef = 188/204                          */
			max_bit_rate =
			    (ext_attr->curr_symbol_rate / 8) * nr_bits * 188;
			/* pass through b/c Annex A/c need following settings */
		case DRX_STANDARD_ITU_B:
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_FCT_USAGE__A, FEC_OC_FCT_USAGE__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_TMD_CTL_UPD_RATE__A, FEC_OC_TMD_CTL_UPD_RATE__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_TMD_INT_UPD_RATE__A, 5, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_AVR_PARM_A__A, FEC_OC_AVR_PARM_A__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_AVR_PARM_B__A, FEC_OC_AVR_PARM_B__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			if (cfg_data->static_clk == true) {
				rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_RCN_GAIN__A, 0xD, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
			} else {
				rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_RCN_GAIN__A, FEC_OC_RCN_GAIN__PRE, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_SNC_LWM__A, 2, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_SNC_HWM__A, 12, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		default:
			break;
		}		/* swtich (standard) */

		/* Check insertion of the Reed-Solomon parity bytes */
		rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_MODE__A, &fec_oc_reg_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_IPR_MODE__A, &fec_oc_reg_ipr_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (cfg_data->insert_rs_byte == true) {
			/* enable parity symbol forward */
			fec_oc_reg_mode |= FEC_OC_MODE_PARITY__M;
			/* MVAL disable during parity bytes */
			fec_oc_reg_ipr_mode |= FEC_OC_IPR_MODE_MVAL_DIS_PAR__M;
			switch (ext_attr->standard) {
			case DRX_STANDARD_8VSB:
				rcn_rate = 0x004854D3;
				break;
			case DRX_STANDARD_ITU_B:
				fec_oc_reg_mode |= FEC_OC_MODE_TRANSPARENT__M;
				switch (ext_attr->constellation) {
				case DRX_CONSTELLATION_QAM256:
					rcn_rate = 0x008945E7;
					break;
				case DRX_CONSTELLATION_QAM64:
					rcn_rate = 0x005F64D4;
					break;
				default:
					return -EIO;
				}
				break;
			case DRX_STANDARD_ITU_A:
			case DRX_STANDARD_ITU_C:
				/* insert_rs_byte = true -> coef = 188/188 -> 1, RS bits are in MPEG output */
				rcn_rate =
				    (frac28
				     (max_bit_rate,
				      (u32) (common_attr->sys_clock_freq / 8))) /
				    188;
				break;
			default:
				return -EIO;
			}	/* ext_attr->standard */
		} else {	/* insert_rs_byte == false */

			/* disable parity symbol forward */
			fec_oc_reg_mode &= (~FEC_OC_MODE_PARITY__M);
			/* MVAL enable during parity bytes */
			fec_oc_reg_ipr_mode &= (~FEC_OC_IPR_MODE_MVAL_DIS_PAR__M);
			switch (ext_attr->standard) {
			case DRX_STANDARD_8VSB:
				rcn_rate = 0x0041605C;
				break;
			case DRX_STANDARD_ITU_B:
				fec_oc_reg_mode &= (~FEC_OC_MODE_TRANSPARENT__M);
				switch (ext_attr->constellation) {
				case DRX_CONSTELLATION_QAM256:
					rcn_rate = 0x0082D6A0;
					break;
				case DRX_CONSTELLATION_QAM64:
					rcn_rate = 0x005AEC1A;
					break;
				default:
					return -EIO;
				}
				break;
			case DRX_STANDARD_ITU_A:
			case DRX_STANDARD_ITU_C:
				/* insert_rs_byte = false -> coef = 188/204, RS bits not in MPEG output */
				rcn_rate =
				    (frac28
				     (max_bit_rate,
				      (u32) (common_attr->sys_clock_freq / 8))) /
				    204;
				break;
			default:
				return -EIO;
			}	/* ext_attr->standard */
		}

		if (cfg_data->enable_parallel == true) {	/* MPEG data output is paralel -> clear ipr_mode[0] */
			fec_oc_reg_ipr_mode &= (~(FEC_OC_IPR_MODE_SERIAL__M));
		} else {	/* MPEG data output is serial -> set ipr_mode[0] */
			fec_oc_reg_ipr_mode |= FEC_OC_IPR_MODE_SERIAL__M;
		}

		/* Control slective inversion of output bits */
		if (cfg_data->invert_data == true)
			fec_oc_reg_ipr_invert |= invert_data_mask;
		else
			fec_oc_reg_ipr_invert &= (~(invert_data_mask));

		if (cfg_data->invert_err == true)
			fec_oc_reg_ipr_invert |= FEC_OC_IPR_INVERT_MERR__M;
		else
			fec_oc_reg_ipr_invert &= (~(FEC_OC_IPR_INVERT_MERR__M));

		if (cfg_data->invert_str == true)
			fec_oc_reg_ipr_invert |= FEC_OC_IPR_INVERT_MSTRT__M;
		else
			fec_oc_reg_ipr_invert &= (~(FEC_OC_IPR_INVERT_MSTRT__M));

		if (cfg_data->invert_val == true)
			fec_oc_reg_ipr_invert |= FEC_OC_IPR_INVERT_MVAL__M;
		else
			fec_oc_reg_ipr_invert &= (~(FEC_OC_IPR_INVERT_MVAL__M));

		if (cfg_data->invert_clk == true)
			fec_oc_reg_ipr_invert |= FEC_OC_IPR_INVERT_MCLK__M;
		else
			fec_oc_reg_ipr_invert &= (~(FEC_OC_IPR_INVERT_MCLK__M));

		if (cfg_data->static_clk == true) {	/* Static mode */
			u32 dto_rate = 0;
			u32 bit_rate = 0;
			u16 fec_oc_dto_burst_len = 0;
			u16 fec_oc_dto_period = 0;

			fec_oc_dto_burst_len = FEC_OC_DTO_BURST_LEN__PRE;

			switch (ext_attr->standard) {
			case DRX_STANDARD_8VSB:
				fec_oc_dto_period = 4;
				if (cfg_data->insert_rs_byte == true)
					fec_oc_dto_burst_len = 208;
				break;
			case DRX_STANDARD_ITU_A:
				{
					u32 symbol_rate_th = 6400000;
					if (cfg_data->insert_rs_byte == true) {
						fec_oc_dto_burst_len = 204;
						symbol_rate_th = 5900000;
					}
					if (ext_attr->curr_symbol_rate >=
					    symbol_rate_th) {
						fec_oc_dto_period = 0;
					} else {
						fec_oc_dto_period = 1;
					}
				}
				break;
			case DRX_STANDARD_ITU_B:
				fec_oc_dto_period = 1;
				if (cfg_data->insert_rs_byte == true)
					fec_oc_dto_burst_len = 128;
				break;
			case DRX_STANDARD_ITU_C:
				fec_oc_dto_period = 1;
				if (cfg_data->insert_rs_byte == true)
					fec_oc_dto_burst_len = 204;
				break;
			default:
				return -EIO;
			}
			bit_rate =
			    common_attr->sys_clock_freq * 1000 / (fec_oc_dto_period +
							       2);
			dto_rate =
			    frac28(bit_rate, common_attr->sys_clock_freq * 1000);
			dto_rate >>= 3;
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_DTO_RATE_HI__A, (u16)((dto_rate >> 16) & FEC_OC_DTO_RATE_HI__M), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_DTO_RATE_LO__A, (u16)(dto_rate & FEC_OC_DTO_RATE_LO_RATE_LO__M), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_DTO_MODE__A, FEC_OC_DTO_MODE_DYNAMIC__M | FEC_OC_DTO_MODE_OFFSET_ENABLE__M, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_FCT_MODE__A, FEC_OC_FCT_MODE_RAT_ENA__M | FEC_OC_FCT_MODE_VIRT_ENA__M, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_DTO_BURST_LEN__A, fec_oc_dto_burst_len, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			if (ext_attr->mpeg_output_clock_rate != DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO)
				fec_oc_dto_period = ext_attr->mpeg_output_clock_rate - 1;
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_DTO_PERIOD__A, fec_oc_dto_period, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} else {	/* Dynamic mode */

			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_DTO_MODE__A, FEC_OC_DTO_MODE_DYNAMIC__M, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_FCT_MODE__A, 0, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}

		rc = DRXJ_DAP.write_reg32func(dev_addr, FEC_OC_RCN_CTL_RATE_LO__A, rcn_rate, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* Write appropriate registers with requested configuration */
		rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_MODE__A, fec_oc_reg_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_IPR_MODE__A, fec_oc_reg_ipr_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_IPR_INVERT__A, fec_oc_reg_ipr_invert, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* enabling for both parallel and serial now */
		/*  Write magic word to enable pdr reg write */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, 0xFABA, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/*  Set MPEG TS pads to outputmode */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MSTRT_CFG__A, 0x0013, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MERR_CFG__A, 0x0013, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MCLK_CFG__A, MPEG_OUTPUT_CLK_DRIVE_STRENGTH << SIO_PDR_MCLK_CFG_DRIVE__B | 0x03 << SIO_PDR_MCLK_CFG_MODE__B, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MVAL_CFG__A, 0x0013, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		sio_pdr_md_cfg =
		    MPEG_SERIAL_OUTPUT_PIN_DRIVE_STRENGTH <<
		    SIO_PDR_MD0_CFG_DRIVE__B | 0x03 << SIO_PDR_MD0_CFG_MODE__B;
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD0_CFG__A, sio_pdr_md_cfg, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (cfg_data->enable_parallel == true) {	/* MPEG data output is paralel -> set MD1 to MD7 to output mode */
			sio_pdr_md_cfg =
			    MPEG_PARALLEL_OUTPUT_PIN_DRIVE_STRENGTH <<
			    SIO_PDR_MD0_CFG_DRIVE__B | 0x03 <<
			    SIO_PDR_MD0_CFG_MODE__B;
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD0_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD1_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD2_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD3_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD4_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD5_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD6_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD7_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} else {	/* MPEG data output is serial -> set MD1 to MD7 to tri-state */
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD1_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD2_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD3_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD4_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD5_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD6_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD7_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		/*  Enable Monitor Bus output over MPEG pads and ctl input */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MON_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/*  Write nomagic word to enable pdr reg write */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		/*  Write magic word to enable pdr reg write */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, 0xFABA, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/*  Set MPEG TS pads to inputmode */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MSTRT_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MERR_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MCLK_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MVAL_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD0_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD1_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD2_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD3_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD4_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD5_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD6_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD7_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* Enable Monitor Bus output over MPEG pads and ctl input */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MON_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* Write nomagic word to enable pdr reg write */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* save values for restore after re-acquire */
	common_attr->mpeg_cfg.enable_mpeg_output = cfg_data->enable_mpeg_output;
	common_attr->mpeg_cfg.insert_rs_byte = cfg_data->insert_rs_byte;
	common_attr->mpeg_cfg.enable_parallel = cfg_data->enable_parallel;
	common_attr->mpeg_cfg.invert_data = cfg_data->invert_data;
	common_attr->mpeg_cfg.invert_err = cfg_data->invert_err;
	common_attr->mpeg_cfg.invert_str = cfg_data->invert_str;
	common_attr->mpeg_cfg.invert_val = cfg_data->invert_val;
	common_attr->mpeg_cfg.invert_clk = cfg_data->invert_clk;
	common_attr->mpeg_cfg.static_clk = cfg_data->static_clk;
	common_attr->mpeg_cfg.bitrate = cfg_data->bitrate;

	return 0;
rw_error:
	return -EIO;
}

/*----------------------------------------------------------------------------*/

/**
* \fn int ctrl_get_cfg_mpeg_output()
* \brief Get MPEG output configuration of the device.
* \param devmod  Pointer to demodulator instance.
* \param cfg_data Pointer to MPEG output configuaration struct.
* \return int.
*
*  Retrieve MPEG output configuartion.
*
*/
static int
ctrl_get_cfg_mpeg_output(struct drx_demod_instance *demod, struct drx_cfg_mpeg_output *cfg_data)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	struct drx_common_attr *common_attr = (struct drx_common_attr *) (NULL);
	enum drx_lock_status lock_status = DRX_NOT_LOCKED;
	int rc;
	u32 rate_reg = 0;
	u32 data64hi = 0;
	u32 data64lo = 0;

	if (cfg_data == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	common_attr = demod->my_common_attr;

	cfg_data->enable_mpeg_output = common_attr->mpeg_cfg.enable_mpeg_output;
	cfg_data->insert_rs_byte = common_attr->mpeg_cfg.insert_rs_byte;
	cfg_data->enable_parallel = common_attr->mpeg_cfg.enable_parallel;
	cfg_data->invert_data = common_attr->mpeg_cfg.invert_data;
	cfg_data->invert_err = common_attr->mpeg_cfg.invert_err;
	cfg_data->invert_str = common_attr->mpeg_cfg.invert_str;
	cfg_data->invert_val = common_attr->mpeg_cfg.invert_val;
	cfg_data->invert_clk = common_attr->mpeg_cfg.invert_clk;
	cfg_data->static_clk = common_attr->mpeg_cfg.static_clk;
	cfg_data->bitrate = 0;

	rc = ctrl_lock_status(demod, &lock_status);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if ((lock_status == DRX_LOCKED)) {
		rc = DRXJ_DAP.read_reg32func(dev_addr, FEC_OC_RCN_DYN_RATE_LO__A, &rate_reg, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* Frcn_rate = rate_reg * Fsys / 2 ^ 25 */
		mult32(rate_reg, common_attr->sys_clock_freq * 1000, &data64hi,
		       &data64lo);
		cfg_data->bitrate = (data64hi << 7) | (data64lo >> 25);
	}

	return 0;
rw_error:
	return -EIO;
}

/*----------------------------------------------------------------------------*/
/* MPEG Output Configuration Functions - end                                  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* miscellaneous configuartions - begin                           */
/*----------------------------------------------------------------------------*/

/**
* \fn int set_mpegtei_handling()
* \brief Activate MPEG TEI handling settings.
* \param devmod  Pointer to demodulator instance.
* \return int.
*
* This routine should be called during a set channel of QAM/VSB
*
*/
static int set_mpegtei_handling(struct drx_demod_instance *demod)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	int rc;
	u16 fec_oc_dpr_mode = 0;
	u16 fec_oc_snc_mode = 0;
	u16 fec_oc_ems_mode = 0;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_DPR_MODE__A, &fec_oc_dpr_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_SNC_MODE__A, &fec_oc_snc_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_EMS_MODE__A, &fec_oc_ems_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* reset to default, allow TEI bit to be changed */
	fec_oc_dpr_mode &= (~FEC_OC_DPR_MODE_ERR_DISABLE__M);
	fec_oc_snc_mode &= (~(FEC_OC_SNC_MODE_ERROR_CTL__M |
			   FEC_OC_SNC_MODE_CORR_DISABLE__M));
	fec_oc_ems_mode &= (~FEC_OC_EMS_MODE_MODE__M);

	if (ext_attr->disable_te_ihandling) {
		/* do not change TEI bit */
		fec_oc_dpr_mode |= FEC_OC_DPR_MODE_ERR_DISABLE__M;
		fec_oc_snc_mode |= FEC_OC_SNC_MODE_CORR_DISABLE__M |
		    ((0x2) << (FEC_OC_SNC_MODE_ERROR_CTL__B));
		fec_oc_ems_mode |= ((0x01) << (FEC_OC_EMS_MODE_MODE__B));
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_DPR_MODE__A, fec_oc_dpr_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_SNC_MODE__A, fec_oc_snc_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_EMS_MODE__A, fec_oc_ems_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*----------------------------------------------------------------------------*/
/**
* \fn int bit_reverse_mpeg_output()
* \brief Set MPEG output bit-endian settings.
* \param devmod  Pointer to demodulator instance.
* \return int.
*
* This routine should be called during a set channel of QAM/VSB
*
*/
static int bit_reverse_mpeg_output(struct drx_demod_instance *demod)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	int rc;
	u16 fec_oc_ipr_mode = 0;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_IPR_MODE__A, &fec_oc_ipr_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* reset to default (normal bit order) */
	fec_oc_ipr_mode &= (~FEC_OC_IPR_MODE_REVERSE_ORDER__M);

	if (ext_attr->bit_reverse_mpeg_outout)
		fec_oc_ipr_mode |= FEC_OC_IPR_MODE_REVERSE_ORDER__M;

	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_IPR_MODE__A, fec_oc_ipr_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*----------------------------------------------------------------------------*/
/**
* \fn int set_mpeg_output_clock_rate()
* \brief Set MPEG output clock rate.
* \param devmod  Pointer to demodulator instance.
* \return int.
*
* This routine should be called during a set channel of QAM/VSB
*
*/
static int set_mpeg_output_clock_rate(struct drx_demod_instance *demod)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	int rc;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	if (ext_attr->mpeg_output_clock_rate != DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_DTO_PERIOD__A, ext_attr->mpeg_output_clock_rate - 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	return 0;
rw_error:
	return -EIO;
}

/*----------------------------------------------------------------------------*/
/**
* \fn int set_mpeg_start_width()
* \brief Set MPEG start width.
* \param devmod  Pointer to demodulator instance.
* \return int.
*
* This routine should be called during a set channel of QAM/VSB
*
*/
static int set_mpeg_start_width(struct drx_demod_instance *demod)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	struct drx_common_attr *common_attr = (struct drx_common_attr *) NULL;
	int rc;
	u16 fec_oc_comm_mb = 0;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	common_attr = demod->my_common_attr;

	if ((common_attr->mpeg_cfg.static_clk == true)
	    && (common_attr->mpeg_cfg.enable_parallel == false)) {
		rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_COMM_MB__A, &fec_oc_comm_mb, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		fec_oc_comm_mb &= ~FEC_OC_COMM_MB_CTL_ON;
		if (ext_attr->mpeg_start_width == DRXJ_MPEG_START_WIDTH_8CLKCYC)
			fec_oc_comm_mb |= FEC_OC_COMM_MB_CTL_ON;
		rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_COMM_MB__A, fec_oc_comm_mb, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	return 0;
rw_error:
	return -EIO;
}

/*----------------------------------------------------------------------------*/
/**
* \fn int ctrl_set_cfg_mpeg_output_misc()
* \brief Set miscellaneous configuartions
* \param devmod  Pointer to demodulator instance.
* \param cfg_data pDRXJCfgMisc_t
* \return int.
*
*  This routine can be used to set configuartion options that are DRXJ
*  specific and/or added to the requirements at a late stage.
*
*/
static int
ctrl_set_cfg_mpeg_output_misc(struct drx_demod_instance *demod,
			      struct drxj_cfg_mpeg_output_misc *cfg_data)
{
	struct drxj_data *ext_attr = NULL;
	int rc;

	if (cfg_data == NULL)
		return -EINVAL;

	ext_attr = demod->my_ext_attr;

	/*
	   Set disable TEI bit handling flag.
	   TEI must be left untouched by device in case of BER measurements using
	   external equipment that is unable to ignore the TEI bit in the TS.
	   Default will false (enable TEI bit handling).
	   Reverse output bit order. Default is false (msb on MD7 (parallel) or out first (serial)).
	   Set clock rate. Default is auto that is derived from symbol rate.
	   The flags and values will also be used to set registers during a set channel.
	 */
	ext_attr->disable_te_ihandling = cfg_data->disable_tei_handling;
	ext_attr->bit_reverse_mpeg_outout = cfg_data->bit_reverse_mpeg_outout;
	ext_attr->mpeg_output_clock_rate = cfg_data->mpeg_output_clock_rate;
	ext_attr->mpeg_start_width = cfg_data->mpeg_start_width;
	/* Don't care what the active standard is, activate setting immediatly */
	rc = set_mpegtei_handling(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = bit_reverse_mpeg_output(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = set_mpeg_output_clock_rate(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = set_mpeg_start_width(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*----------------------------------------------------------------------------*/

/**
* \fn int ctrl_get_cfg_mpeg_output_misc()
* \brief Get miscellaneous configuartions.
* \param devmod  Pointer to demodulator instance.
* \param cfg_data Pointer to DRXJCfgMisc_t.
* \return int.
*
*  This routine can be used to retreive the current setting of the configuartion
*  options that are DRXJ specific and/or added to the requirements at a
*  late stage.
*
*/
static int
ctrl_get_cfg_mpeg_output_misc(struct drx_demod_instance *demod,
			      struct drxj_cfg_mpeg_output_misc *cfg_data)
{
	struct drxj_data *ext_attr = NULL;
	int rc;
	u16 data = 0;

	if (cfg_data == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	cfg_data->disable_tei_handling = ext_attr->disable_te_ihandling;
	cfg_data->bit_reverse_mpeg_outout = ext_attr->bit_reverse_mpeg_outout;
	cfg_data->mpeg_start_width = ext_attr->mpeg_start_width;
	if (ext_attr->mpeg_output_clock_rate != DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO) {
		cfg_data->mpeg_output_clock_rate = ext_attr->mpeg_output_clock_rate;
	} else {
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, FEC_OC_DTO_PERIOD__A, &data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		cfg_data->mpeg_output_clock_rate =
		    (enum drxj_mpeg_output_clock_rate) (data + 1);
	}

	return 0;
rw_error:
	return -EIO;
}

/*----------------------------------------------------------------------------*/

/**
* \fn int ctrl_get_cfg_hw_cfg()
* \brief Get HW configuartions.
* \param devmod  Pointer to demodulator instance.
* \param cfg_data Pointer to Bool.
* \return int.
*
*  This routine can be used to retreive the current setting of the configuartion
*  options that are DRXJ specific and/or added to the requirements at a
*  late stage.
*
*/
static int
ctrl_get_cfg_hw_cfg(struct drx_demod_instance *demod, struct drxj_cfg_hw_cfg *cfg_data)
{
	int rc;
	u16 data = 0;

	if (cfg_data == NULL)
		return -EINVAL;

	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0xFABA, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SIO_PDR_OHW_CFG__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	cfg_data->i2c_speed = (enum drxji2c_speed) ((data >> 6) & 0x1);
	cfg_data->xtal_freq = (enum drxj_xtal_freq) (data & 0x3);

	return 0;
rw_error:
	return -EIO;
}

/*----------------------------------------------------------------------------*/
/* miscellaneous configuartions - end                             */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* UIO Configuration Functions - begin                                        */
/*----------------------------------------------------------------------------*/
/**
* \fn int ctrl_set_uio_cfg()
* \brief Configure modus oprandi UIO.
* \param demod Pointer to demodulator instance.
* \param uio_cfg Pointer to a configuration setting for a certain UIO.
* \return int.
*/
static int ctrl_set_uio_cfg(struct drx_demod_instance *demod, struct drxuio_cfg *uio_cfg)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	int rc;

	if ((uio_cfg == NULL) || (demod == NULL))
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/*  Write magic word to enable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	switch (uio_cfg->uio) {
      /*====================================================================*/
	case DRX_UIO1:
		/* DRX_UIO1: SMA_TX UIO-1 */
		if (!ext_attr->has_smatx)
			return -EIO;
		switch (uio_cfg->mode) {
		case DRX_UIO_MODE_FIRMWARE_SMA:	/* falltrough */
		case DRX_UIO_MODE_FIRMWARE_SAW:	/* falltrough */
		case DRX_UIO_MODE_READWRITE:
			ext_attr->uio_sma_tx_mode = uio_cfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			ext_attr->uio_sma_tx_mode = uio_cfg->mode;
			/* pad configuration register is set 0 - input mode */
			rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_SMA_TX_CFG__A, 0, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		default:
			return -EINVAL;
		}		/* switch ( uio_cfg->mode ) */
		break;
      /*====================================================================*/
	case DRX_UIO2:
		/* DRX_UIO2: SMA_RX UIO-2 */
		if (!ext_attr->has_smarx)
			return -EIO;
		switch (uio_cfg->mode) {
		case DRX_UIO_MODE_FIRMWARE0:	/* falltrough */
		case DRX_UIO_MODE_READWRITE:
			ext_attr->uio_sma_rx_mode = uio_cfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			ext_attr->uio_sma_rx_mode = uio_cfg->mode;
			/* pad configuration register is set 0 - input mode */
			rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_SMA_RX_CFG__A, 0, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		default:
			return -EINVAL;
			break;
		}		/* switch ( uio_cfg->mode ) */
		break;
      /*====================================================================*/
	case DRX_UIO3:
		/* DRX_UIO3: GPIO UIO-3 */
		if (!ext_attr->has_gpio)
			return -EIO;
		switch (uio_cfg->mode) {
		case DRX_UIO_MODE_FIRMWARE0:	/* falltrough */
		case DRX_UIO_MODE_READWRITE:
			ext_attr->uio_gpio_mode = uio_cfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			ext_attr->uio_gpio_mode = uio_cfg->mode;
			/* pad configuration register is set 0 - input mode */
			rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_GPIO_CFG__A, 0, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		default:
			return -EINVAL;
			break;
		}		/* switch ( uio_cfg->mode ) */
		break;
      /*====================================================================*/
	case DRX_UIO4:
		/* DRX_UIO4: IRQN UIO-4 */
		if (!ext_attr->has_irqn)
			return -EIO;
		switch (uio_cfg->mode) {
		case DRX_UIO_MODE_READWRITE:
			ext_attr->uio_irqn_mode = uio_cfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			/* pad configuration register is set 0 - input mode */
			rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_IRQN_CFG__A, 0, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			ext_attr->uio_irqn_mode = uio_cfg->mode;
			break;
		case DRX_UIO_MODE_FIRMWARE0:	/* falltrough */
		default:
			return -EINVAL;
			break;
		}		/* switch ( uio_cfg->mode ) */
		break;
      /*====================================================================*/
	default:
		return -EINVAL;
	}			/* switch ( uio_cfg->uio ) */

	/*  Write magic word to disable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \fn int ctrl_getuio_cfg()
* \brief Get modus oprandi UIO.
* \param demod Pointer to demodulator instance.
* \param uio_cfg Pointer to a configuration setting for a certain UIO.
* \return int.
*/
static int ctrl_getuio_cfg(struct drx_demod_instance *demod, struct drxuio_cfg *uio_cfg)
{

	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	enum drxuio_mode *uio_mode[4] = { NULL };
	bool *uio_available[4] = { NULL };

	ext_attr = demod->my_ext_attr;

	uio_mode[DRX_UIO1] = &ext_attr->uio_sma_tx_mode;
	uio_mode[DRX_UIO2] = &ext_attr->uio_sma_rx_mode;
	uio_mode[DRX_UIO3] = &ext_attr->uio_gpio_mode;
	uio_mode[DRX_UIO4] = &ext_attr->uio_irqn_mode;

	uio_available[DRX_UIO1] = &ext_attr->has_smatx;
	uio_available[DRX_UIO2] = &ext_attr->has_smarx;
	uio_available[DRX_UIO3] = &ext_attr->has_gpio;
	uio_available[DRX_UIO4] = &ext_attr->has_irqn;

	if (uio_cfg == NULL)
		return -EINVAL;

	if ((uio_cfg->uio > DRX_UIO4) || (uio_cfg->uio < DRX_UIO1))
		return -EINVAL;

	if (!*uio_available[uio_cfg->uio])
		return -EIO;

	uio_cfg->mode = *uio_mode[uio_cfg->uio];

	return 0;
}

/**
* \fn int ctrl_uio_write()
* \brief Write to a UIO.
* \param demod Pointer to demodulator instance.
* \param uio_data Pointer to data container for a certain UIO.
* \return int.
*/
static int
ctrl_uio_write(struct drx_demod_instance *demod, struct drxuio_data *uio_data)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	int rc;
	u16 pin_cfg_value = 0;
	u16 value = 0;

	if ((uio_data == NULL) || (demod == NULL))
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/*  Write magic word to enable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	switch (uio_data->uio) {
      /*====================================================================*/
	case DRX_UIO1:
		/* DRX_UIO1: SMA_TX UIO-1 */
		if (!ext_attr->has_smatx)
			return -EIO;
		if ((ext_attr->uio_sma_tx_mode != DRX_UIO_MODE_READWRITE)
		    && (ext_attr->uio_sma_tx_mode != DRX_UIO_MODE_FIRMWARE_SAW)) {
			return -EIO;
		}
		pin_cfg_value = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pin_cfg_value |= 0x0113;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - output mode */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_SMA_TX_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* use corresponding bit in io data output registar */
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (!uio_data->value)
			value &= 0x7FFF;	/* write zero to 15th bit - 1st UIO */
		else
			value |= 0x8000;	/* write one to 15th bit - 1st UIO */

		/* write back to io data output register */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
   /*======================================================================*/
	case DRX_UIO2:
		/* DRX_UIO2: SMA_RX UIO-2 */
		if (!ext_attr->has_smarx)
			return -EIO;
		if (ext_attr->uio_sma_rx_mode != DRX_UIO_MODE_READWRITE)
			return -EIO;

		pin_cfg_value = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pin_cfg_value |= 0x0113;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - output mode */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_SMA_RX_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* use corresponding bit in io data output registar */
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (!uio_data->value)
			value &= 0xBFFF;	/* write zero to 14th bit - 2nd UIO */
		else
			value |= 0x4000;	/* write one to 14th bit - 2nd UIO */

		/* write back to io data output register */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
   /*====================================================================*/
	case DRX_UIO3:
		/* DRX_UIO3: ASEL UIO-3 */
		if (!ext_attr->has_gpio)
			return -EIO;
		if (ext_attr->uio_gpio_mode != DRX_UIO_MODE_READWRITE)
			return -EIO;

		pin_cfg_value = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pin_cfg_value |= 0x0113;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - output mode */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_GPIO_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* use corresponding bit in io data output registar */
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_HI__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (!uio_data->value)
			value &= 0xFFFB;	/* write zero to 2nd bit - 3rd UIO */
		else
			value |= 0x0004;	/* write one to 2nd bit - 3rd UIO */

		/* write back to io data output register */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_HI__A, value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
   /*=====================================================================*/
	case DRX_UIO4:
		/* DRX_UIO4: IRQN UIO-4 */
		if (!ext_attr->has_irqn)
			return -EIO;

		if (ext_attr->uio_irqn_mode != DRX_UIO_MODE_READWRITE)
			return -EIO;

		pin_cfg_value = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pin_cfg_value |= 0x0113;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - output mode */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_IRQN_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* use corresponding bit in io data output registar */
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (uio_data->value == false)
			value &= 0xEFFF;	/* write zero to 12th bit - 4th UIO */
		else
			value |= 0x1000;	/* write one to 12th bit - 4th UIO */

		/* write back to io data output register */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
      /*=====================================================================*/
	default:
		return -EINVAL;
	}			/* switch ( uio_data->uio ) */

	/*  Write magic word to disable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
*\fn int ctrl_uio_read
*\brief Read from a UIO.
* \param demod Pointer to demodulator instance.
* \param uio_data Pointer to data container for a certain UIO.
* \return int.
*/
static int ctrl_uio_read(struct drx_demod_instance *demod, struct drxuio_data *uio_data)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	int rc;
	u16 pin_cfg_value = 0;
	u16 value = 0;

	if ((uio_data == NULL) || (demod == NULL))
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/*  Write magic word to enable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	switch (uio_data->uio) {
      /*====================================================================*/
	case DRX_UIO1:
		/* DRX_UIO1: SMA_TX UIO-1 */
		if (!ext_attr->has_smatx)
			return -EIO;

		if (ext_attr->uio_sma_tx_mode != DRX_UIO_MODE_READWRITE)
			return -EIO;

		pin_cfg_value = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pin_cfg_value |= 0x0110;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - input mode */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_SMA_TX_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_IN_LO__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if ((value & 0x8000) != 0) {	/* check 15th bit - 1st UIO */
			uio_data->value = true;
		} else {
			uio_data->value = false;
		}
		break;
   /*======================================================================*/
	case DRX_UIO2:
		/* DRX_UIO2: SMA_RX UIO-2 */
		if (!ext_attr->has_smarx)
			return -EIO;

		if (ext_attr->uio_sma_rx_mode != DRX_UIO_MODE_READWRITE)
			return -EIO;

		pin_cfg_value = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pin_cfg_value |= 0x0110;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - input mode */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_SMA_RX_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_IN_LO__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		if ((value & 0x4000) != 0)	/* check 14th bit - 2nd UIO */
			uio_data->value = true;
		else
			uio_data->value = false;

		break;
   /*=====================================================================*/
	case DRX_UIO3:
		/* DRX_UIO3: GPIO UIO-3 */
		if (!ext_attr->has_gpio)
			return -EIO;

		if (ext_attr->uio_gpio_mode != DRX_UIO_MODE_READWRITE)
			return -EIO;

		pin_cfg_value = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pin_cfg_value |= 0x0110;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - input mode */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_GPIO_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* read io input data registar */
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_IN_HI__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if ((value & 0x0004) != 0) {	/* check 2nd bit - 3rd UIO */
			uio_data->value = true;
		} else {
			uio_data->value = false;
		}
		break;
   /*=====================================================================*/
	case DRX_UIO4:
		/* DRX_UIO4: IRQN UIO-4 */
		if (!ext_attr->has_irqn)
			return -EIO;

		if (ext_attr->uio_irqn_mode != DRX_UIO_MODE_READWRITE)
			return -EIO;

		pin_cfg_value = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pin_cfg_value |= 0x0110;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - input mode */
		rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_IRQN_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* read io input data registar */
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SIO_PDR_UIO_IN_LO__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if ((value & 0x1000) != 0)	/* check 12th bit - 4th UIO */
			uio_data->value = true;
		else
			uio_data->value = false;

		break;
      /*====================================================================*/
	default:
		return -EINVAL;
	}			/* switch ( uio_data->uio ) */

	/*  Write magic word to disable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*---------------------------------------------------------------------------*/
/* UIO Configuration Functions - end                                         */
/*---------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* I2C Bridge Functions - begin                                               */
/*----------------------------------------------------------------------------*/
/**
* \fn int ctrl_i2c_bridge()
* \brief Open or close the I2C switch to tuner.
* \param demod Pointer to demodulator instance.
* \param bridge_closed Pointer to bool indication if bridge is closed not.
* \return int.

*/
static int
ctrl_i2c_bridge(struct drx_demod_instance *demod, bool *bridge_closed)
{
	struct drxj_hi_cmd hi_cmd;
	u16 result = 0;

	/* check arguments */
	if (bridge_closed == NULL)
		return -EINVAL;

	hi_cmd.cmd = SIO_HI_RA_RAM_CMD_BRDCTRL;
	hi_cmd.param1 = SIO_HI_RA_RAM_PAR_1_PAR1_SEC_KEY;
	if (*bridge_closed)
		hi_cmd.param2 = SIO_HI_RA_RAM_PAR_2_BRD_CFG_CLOSED;
	else
		hi_cmd.param2 = SIO_HI_RA_RAM_PAR_2_BRD_CFG_OPEN;

	return hi_command(demod->my_i2c_dev_addr, &hi_cmd, &result);
}

/*----------------------------------------------------------------------------*/
/* I2C Bridge Functions - end                                                 */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Smart antenna Functions - begin                                            */
/*----------------------------------------------------------------------------*/
/**
* \fn int smart_ant_init()
* \brief Initialize Smart Antenna.
* \param pointer to struct drx_demod_instance.
* \return int.
*
*/
static int smart_ant_init(struct drx_demod_instance *demod)
{
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	struct drxuio_cfg uio_cfg = { DRX_UIO1, DRX_UIO_MODE_FIRMWARE_SMA };
	int rc;
	u16 data = 0;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/*  Write magic word to enable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* init smart antenna */
	rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_SA_TX_COMMAND__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (ext_attr->smart_ant_inverted) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_SA_TX_COMMAND__A, (data | SIO_SA_TX_COMMAND_TX_INVERT__M) | SIO_SA_TX_COMMAND_TX_ENABLE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_SA_TX_COMMAND__A, (data & (~SIO_SA_TX_COMMAND_TX_INVERT__M)) | SIO_SA_TX_COMMAND_TX_ENABLE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* config SMA_TX pin to smart antenna mode */
	rc = ctrl_set_uio_cfg(demod, &uio_cfg);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_SMA_TX_CFG__A, 0x13, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_PDR_SMA_TX_GPIO_FNC__A, 0x03, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/*  Write magic word to disable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int ctrl_set_cfg_smart_ant()
* \brief Set Smart Antenna.
* \param pointer to struct drxj_cfg_smart_ant.
* \return int.
*
*/
static int
ctrl_set_cfg_smart_ant(struct drx_demod_instance *demod, struct drxj_cfg_smart_ant *smart_ant)
{
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	u32 start_time = 0;
	u16 data = 0;
	static bool bit_inverted;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* check arguments */
	if (smart_ant == NULL)
		return -EINVAL;

	if (bit_inverted != ext_attr->smart_ant_inverted
	    || ext_attr->uio_sma_tx_mode != DRX_UIO_MODE_FIRMWARE_SMA) {
		rc = smart_ant_init(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		bit_inverted = ext_attr->smart_ant_inverted;
	}

	/*  Write magic word to enable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	switch (smart_ant->io) {
	case DRXJ_SMT_ANT_OUTPUT:
		/* enable Tx if Mode B (input) is supported */
		/*
		   RR16( dev_addr, SIO_SA_TX_COMMAND__A, &data );
		   WR16( dev_addr, SIO_SA_TX_COMMAND__A, data | SIO_SA_TX_COMMAND_TX_ENABLE__M );
		 */
		start_time = drxbsp_hst_clock();
		do {
			rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_SA_TX_STATUS__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} while ((data & SIO_SA_TX_STATUS_BUSY__M) && ((drxbsp_hst_clock() - start_time) < DRXJ_MAX_WAITTIME));

		if (data & SIO_SA_TX_STATUS_BUSY__M)
			return -EIO;

		/* write to smart antenna configuration register */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_SA_TX_DATA0__A, 0x9200 | ((smart_ant->ctrl_data & 0x0001) << 8) | ((smart_ant->ctrl_data & 0x0002) << 10) | ((smart_ant->ctrl_data & 0x0004) << 12), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_SA_TX_DATA1__A, 0x4924 | ((smart_ant->ctrl_data & 0x0008) >> 2) | ((smart_ant->ctrl_data & 0x0010)) | ((smart_ant->ctrl_data & 0x0020) << 2) | ((smart_ant->ctrl_data & 0x0040) << 4) | ((smart_ant->ctrl_data & 0x0080) << 6), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_SA_TX_DATA2__A, 0x2492 | ((smart_ant->ctrl_data & 0x0100) >> 8) | ((smart_ant->ctrl_data & 0x0200) >> 6) | ((smart_ant->ctrl_data & 0x0400) >> 4) | ((smart_ant->ctrl_data & 0x0800) >> 2) | ((smart_ant->ctrl_data & 0x1000)) | ((smart_ant->ctrl_data & 0x2000) << 2), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_SA_TX_DATA3__A, 0xff8d, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* trigger the sending */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_SA_TX_LENGTH__A, 56, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		break;
	case DRXJ_SMT_ANT_INPUT:
		/* disable Tx if Mode B (input) is supported */
		/*
		   RR16( dev_addr, SIO_SA_TX_COMMAND__A, &data );
		   WR16( dev_addr, SIO_SA_TX_COMMAND__A, data & (~SIO_SA_TX_COMMAND_TX_ENABLE__M) );
		 */
	default:
		return -EINVAL;
	}
	/*  Write magic word to enable pdr reg write               */
	rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

static int scu_command(struct i2c_device_addr *dev_addr, struct drxjscu_cmd *cmd)
{
	int rc;
	u32 start_time = 0;
	u16 cur_cmd = 0;

	/* Check param */
	if (cmd == NULL)
		return -EINVAL;

	/* Wait until SCU command interface is ready to receive command */
	rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_COMMAND__A, &cur_cmd, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (cur_cmd != DRX_SCU_READY)
		return -EIO;

	switch (cmd->parameter_len) {
	case 5:
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_PARAM_4__A, *(cmd->parameter + 4), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* fallthrough */
	case 4:
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_PARAM_3__A, *(cmd->parameter + 3), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* fallthrough */
	case 3:
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_PARAM_2__A, *(cmd->parameter + 2), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* fallthrough */
	case 2:
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_PARAM_1__A, *(cmd->parameter + 1), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* fallthrough */
	case 1:
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_PARAM_0__A, *(cmd->parameter + 0), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* fallthrough */
	case 0:
		/* do nothing */
		break;
	default:
		/* this number of parameters is not supported */
		return -EIO;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_COMMAND__A, cmd->command, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Wait until SCU has processed command */
	start_time = drxbsp_hst_clock();
	do {
		rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_COMMAND__A, &cur_cmd, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} while (!(cur_cmd == DRX_SCU_READY)
		 && ((drxbsp_hst_clock() - start_time) < DRXJ_MAX_WAITTIME));

	if (cur_cmd != DRX_SCU_READY)
		return -EIO;

	/* read results */
	if ((cmd->result_len > 0) && (cmd->result != NULL)) {
		s16 err;

		switch (cmd->result_len) {
		case 4:
			rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_PARAM_3__A, cmd->result + 3, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* fallthrough */
		case 3:
			rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_PARAM_2__A, cmd->result + 2, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* fallthrough */
		case 2:
			rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_PARAM_1__A, cmd->result + 1, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* fallthrough */
		case 1:
			rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_PARAM_0__A, cmd->result + 0, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* fallthrough */
		case 0:
			/* do nothing */
			break;
		default:
			/* this number of parameters is not supported */
			return -EIO;
		}

		/* Check if an error was reported by SCU */
		err = cmd->result[0];

		/* check a few fixed error codes */
		if ((err == (s16) SCU_RAM_PARAM_0_RESULT_UNKSTD)
		    || (err == (s16) SCU_RAM_PARAM_0_RESULT_UNKCMD)
		    || (err == (s16) SCU_RAM_PARAM_0_RESULT_INVPAR)
		    || (err == (s16) SCU_RAM_PARAM_0_RESULT_SIZE)
		    ) {
			return -EINVAL;
		}
		/* here it is assumed that negative means error, and positive no error */
		else if (err < 0)
			return -EIO;
		else
			return 0;
	}

	return 0;

rw_error:
	return -EIO;
}

/**
* \fn int DRXJ_DAP_SCUAtomicReadWriteBlock()
* \brief Basic access routine for SCU atomic read or write access
* \param dev_addr  pointer to i2c dev address
* \param addr     destination/source address
* \param datasize size of data buffer in bytes
* \param data     pointer to data buffer
* \return int
* \retval 0 Succes
* \retval -EIO Timeout, I2C error, illegal bank
*
*/
#define ADDR_AT_SCU_SPACE(x) ((x - 0x82E000) * 2)
static
int drxj_dap_scu_atomic_read_write_block(struct i2c_device_addr *dev_addr, u32 addr, u16 datasize,	/* max 30 bytes because the limit of SCU parameter */
					      u8 *data, bool read_flag)
{
	struct drxjscu_cmd scu_cmd;
	int rc;
	u16 set_param_parameters[15];
	u16 cmd_result[15];

	/* Parameter check */
	if (!data || !dev_addr || (datasize % 2) || ((datasize / 2) > 16))
		return -EINVAL;

	set_param_parameters[1] = (u16) ADDR_AT_SCU_SPACE(addr);
	if (read_flag) {		/* read */
		set_param_parameters[0] = ((~(0x0080)) & datasize);
		scu_cmd.parameter_len = 2;
		scu_cmd.result_len = datasize / 2 + 2;
	} else {
		int i = 0;

		set_param_parameters[0] = 0x0080 | datasize;
		for (i = 0; i < (datasize / 2); i++) {
			set_param_parameters[i + 2] =
			    (data[2 * i] | (data[(2 * i) + 1] << 8));
		}
		scu_cmd.parameter_len = datasize / 2 + 2;
		scu_cmd.result_len = 1;
	}

	scu_cmd.command =
	    SCU_RAM_COMMAND_STANDARD_TOP |
	    SCU_RAM_COMMAND_CMD_AUX_SCU_ATOMIC_ACCESS;
	scu_cmd.result = cmd_result;
	scu_cmd.parameter = set_param_parameters;
	rc = scu_command(dev_addr, &scu_cmd);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (read_flag) {
		int i = 0;
		/* read data from buffer */
		for (i = 0; i < (datasize / 2); i++) {
			data[2 * i] = (u8) (scu_cmd.result[i + 2] & 0xFF);
			data[(2 * i) + 1] = (u8) (scu_cmd.result[i + 2] >> 8);
		}
	}

	return 0;

rw_error:
	return -EIO;

}

/*============================================================================*/

/**
* \fn int DRXJ_DAP_AtomicReadReg16()
* \brief Atomic read of 16 bits words
*/
static
int drxj_dap_scu_atomic_read_reg16(struct i2c_device_addr *dev_addr,
					 u32 addr,
					 u16 *data, u32 flags)
{
	u8 buf[2];
	int rc = -EIO;
	u16 word = 0;

	if (!data)
		return -EINVAL;

	rc = drxj_dap_scu_atomic_read_write_block(dev_addr, addr, 2, buf, true);
	if (rc < 0)
		return rc;

	word = (u16) (buf[0] + (buf[1] << 8));

	*data = word;

	return rc;
}

/*============================================================================*/
/**
* \fn int drxj_dap_scu_atomic_write_reg16()
* \brief Atomic read of 16 bits words
*/
static
int drxj_dap_scu_atomic_write_reg16(struct i2c_device_addr *dev_addr,
					  u32 addr,
					  u16 data, u32 flags)
{
	u8 buf[2];
	int rc = -EIO;

	buf[0] = (u8) (data & 0xff);
	buf[1] = (u8) ((data >> 8) & 0xff);

	rc = drxj_dap_scu_atomic_read_write_block(dev_addr, addr, 2, buf, false);

	return rc;
}

static int
ctrl_i2c_write_read(struct drx_demod_instance *demod, struct drxi2c_data *i2c_data)
{
	return -ENOTSUPP;
}

/* -------------------------------------------------------------------------- */
/**
* \brief Measure result of ADC synchronisation
* \param demod demod instance
* \param count (returned) count
* \return int.
* \retval 0    Success
* \retval -EIO Failure: I2C error
*
*/
static int adc_sync_measurement(struct drx_demod_instance *demod, u16 *count)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	u16 data = 0;

	dev_addr = demod->my_i2c_dev_addr;

	/* Start measurement */
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_COMM_EXEC__A, IQM_AF_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_START_LOCK__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Wait at least 3*128*(1/sysclk) <<< 1 millisec */
	rc = drxbsp_hst_sleep(1);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	*count = 0;
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_PHASE0__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (data == 127)
		*count = *count + 1;
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_PHASE1__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (data == 127)
		*count = *count + 1;
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_PHASE2__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (data == 127)
		*count = *count + 1;

	return 0;
rw_error:
	return -EIO;
}

/**
* \brief Synchronize analog and digital clock domains
* \param demod demod instance
* \return int.
* \retval 0    Success
* \retval -EIO Failure: I2C error or failure to synchronize
*
* An IQM reset will also reset the results of this synchronization.
* After an IQM reset this routine needs to be called again.
*
*/

static int adc_synchronization(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	u16 count = 0;

	dev_addr = demod->my_i2c_dev_addr;

	rc = adc_sync_measurement(demod, &count);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (count == 1) {
		/* Try sampling on a diffrent edge */
		u16 clk_neg = 0;

		rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_CLKNEG__A, &clk_neg, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		clk_neg ^= IQM_AF_CLKNEG_CLKNEGDATA__M;
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_CLKNEG__A, clk_neg, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = adc_sync_measurement(demod, &count);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* TODO: implement fallback scenarios */
	if (count < 2)
		return -EIO;

	return 0;
rw_error:
	return -EIO;
}

/**
* \brief Configure IQM AF registers
* \param demod instance of demodulator.
* \param active
* \return int.
*/
static int iqm_set_af(struct drx_demod_instance *demod, bool active)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	u16 data = 0;

	/* Configure IQM */
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_STDBY__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (!active)
		data &= ((~IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_PD_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE));
	else
		data |= (IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE | IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE | IQM_AF_STDBY_STDBY_PD_A2_ACTIVE | IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE | IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE);
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_STDBY__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
static int
ctrl_set_cfg_atv_output(struct drx_demod_instance *demod, struct drxj_cfg_atv_output *output_cfg);

/**
* \brief set configuration of pin-safe mode
* \param demod instance of demodulator.
* \param enable boolean; true: activate pin-safe mode, false: de-activate p.s.m.
* \return int.
*/
static int
ctrl_set_cfg_pdr_safe_mode(struct drx_demod_instance *demod, bool *enable)
{
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	int rc;

	if (enable == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/*  Write magic word to enable pdr reg write  */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (*enable) {
		bool bridge_enabled = false;

		/* MPEG pins to input */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MSTRT_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MERR_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MCLK_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MVAL_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD0_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD1_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD2_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD3_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD4_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD5_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD6_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_MD7_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* PD_I2C_SDA2 Bridge off, Port2 Inactive
		   PD_I2C_SCL2 Bridge off, Port2 Inactive */
		rc = ctrl_i2c_bridge(demod, &bridge_enabled);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2C_SDA2_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2C_SCL2_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/*  PD_GPIO     Store and set to input
		   PD_VSYNC    Store and set to input
		   PD_SMA_RX   Store and set to input
		   PD_SMA_TX   Store and set to input */
		rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_PDR_GPIO_CFG__A, &ext_attr->pdr_safe_restore_val_gpio, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_PDR_VSYNC_CFG__A, &ext_attr->pdr_safe_restore_val_v_sync, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_PDR_SMA_RX_CFG__A, &ext_attr->pdr_safe_restore_val_sma_rx, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_PDR_SMA_TX_CFG__A, &ext_attr->pdr_safe_restore_val_sma_tx, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_GPIO_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_VSYNC_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_SMA_RX_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_SMA_TX_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/*  PD_RF_AGC   Analog DAC outputs, cannot be set to input or tristate!
		   PD_IF_AGC   Analog DAC outputs, cannot be set to input or tristate! */
		rc = iqm_set_af(demod, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/*  PD_CVBS     Analog DAC output, standby mode
		   PD_SIF      Analog DAC output, standby mode */
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STDBY__A, (ATV_TOP_STDBY_SIF_STDBY_STANDBY & (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE)), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/*  PD_I2S_CL   Input
		   PD_I2S_DA   Input
		   PD_I2S_WS   Input */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2S_CL_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2S_DA_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2S_WS_CFG__A, DRXJ_PIN_SAFE_MODE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		/* No need to restore MPEG pins;
		   is done in SetStandard/SetChannel */

		/* PD_I2C_SDA2 Port2 active
		   PD_I2C_SCL2 Port2 active */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2C_SDA2_CFG__A, SIO_PDR_I2C_SDA2_CFG__PRE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2C_SCL2_CFG__A, SIO_PDR_I2C_SCL2_CFG__PRE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/*  PD_GPIO     Restore
		   PD_VSYNC    Restore
		   PD_SMA_RX   Restore
		   PD_SMA_TX   Restore */
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_GPIO_CFG__A, ext_attr->pdr_safe_restore_val_gpio, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_VSYNC_CFG__A, ext_attr->pdr_safe_restore_val_v_sync, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_SMA_RX_CFG__A, ext_attr->pdr_safe_restore_val_sma_rx, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_SMA_TX_CFG__A, ext_attr->pdr_safe_restore_val_sma_tx, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/*  PD_RF_AGC, PD_IF_AGC
		   No need to restore; will be restored in SetStandard/SetChannel */

		/*  PD_CVBS, PD_SIF
		   No need to restore; will be restored in SetStandard/SetChannel */

		/*  PD_I2S_CL, PD_I2S_DA, PD_I2S_WS
		   Should be restored via DRX_CTRL_SET_AUD */
	}

	/*  Write magic word to disable pdr reg write */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->pdr_safe_mode = *enable;

	return 0;

rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */

/**
* \brief get configuration of pin-safe mode
* \param demod instance of demodulator.
* \param enable boolean indicating whether pin-safe mode is active
* \return int.
*/
static int
ctrl_get_cfg_pdr_safe_mode(struct drx_demod_instance *demod, bool *enabled)
{
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;

	if (enabled == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	*enabled = ext_attr->pdr_safe_mode;

	return 0;
}

/**
* \brief Verifies whether microcode can be loaded.
* \param demod Demodulator instance.
* \return int.
*/
static int ctrl_validate_u_code(struct drx_demod_instance *demod)
{
	u32 mc_dev, mc_patch;
	u16 ver_type;

	/* Check device.
	 *  Disallow microcode if:
	 *   - MC has version record AND
	 *   - device ID in version record is not 0 AND
	 *   - product ID in version record's device ID does not
	 *     match DRXJ1 product IDs - 0x393 or 0x394
	 */
	ver_type = DRX_ATTR_MCRECORD(demod).aux_type;
	mc_dev = DRX_ATTR_MCRECORD(demod).mc_dev_type;
	mc_patch = DRX_ATTR_MCRECORD(demod).mc_base_version;

	if (DRX_ISMCVERTYPE(ver_type)) {
		if ((mc_dev != 0) &&
		    (((mc_dev >> 16) & 0xFFF) != 0x393) &&
		    (((mc_dev >> 16) & 0xFFF) != 0x394)) {
			/* Microcode is marked for another device - error */
			return -EINVAL;
		} else if (mc_patch != 0) {
			/* Patch not allowed because there is no ROM */
			return -EINVAL;
		}
	}

	/* Everything else: OK */
	return 0;
}

/*============================================================================*/
/*==                      END AUXILIARY FUNCTIONS                           ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                8VSB & QAM COMMON DATAPATH FUNCTIONS                    ==*/
/*============================================================================*/
/*============================================================================*/
/**
* \fn int init_agc ()
* \brief Initialize AGC for all standards.
* \param demod instance of demodulator.
* \param channel pointer to channel data.
* \return int.
*/
static int init_agc(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drx_common_attr *common_attr = NULL;
	struct drxj_data *ext_attr = NULL;
	struct drxj_cfg_agc *p_agc_rf_settings = NULL;
	struct drxj_cfg_agc *p_agc_if_settings = NULL;
	int rc;
	u16 ingain_tgt_max = 0;
	u16 clp_dir_to = 0;
	u16 sns_sum_max = 0;
	u16 clp_sum_max = 0;
	u16 sns_dir_to = 0;
	u16 ki_innergain_min = 0;
	u16 agc_ki = 0;
	u16 ki_max = 0;
	u16 if_iaccu_hi_tgt_min = 0;
	u16 data = 0;
	u16 agc_ki_dgain = 0;
	u16 ki_min = 0;
	u16 clp_ctrl_mode = 0;
	u16 agc_rf = 0;
	u16 agc_if = 0;

	dev_addr = demod->my_i2c_dev_addr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	switch (ext_attr->standard) {
	case DRX_STANDARD_8VSB:
		clp_sum_max = 1023;
		clp_dir_to = (u16) (-9);
		sns_sum_max = 1023;
		sns_dir_to = (u16) (-9);
		ki_innergain_min = (u16) (-32768);
		ki_max = 0x032C;
		agc_ki_dgain = 0xC;
		if_iaccu_hi_tgt_min = 2047;
		ki_min = 0x0117;
		ingain_tgt_max = 16383;
		clp_ctrl_mode = 0;
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_MINGAIN__A, 0x7fff, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_MAXGAIN__A, 0x0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_SUM__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_CYCCNT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_DIR_WD__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_DIR_STP__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_SUM__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_CYCCNT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_DIR_WD__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_DIR_STP__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_INGAIN__A, 1024, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_VSB_AGC_POW_TGT__A, 22600, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_INGAIN_TGT__A, 13200, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		p_agc_if_settings = &(ext_attr->vsb_if_agc_cfg);
		p_agc_rf_settings = &(ext_attr->vsb_rf_agc_cfg);
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_C:
	case DRX_STANDARD_ITU_B:
		ingain_tgt_max = 5119;
		clp_sum_max = 1023;
		clp_dir_to = (u16) (-5);
		sns_sum_max = 127;
		sns_dir_to = (u16) (-3);
		ki_innergain_min = 0;
		ki_max = 0x0657;
		if_iaccu_hi_tgt_min = 2047;
		agc_ki_dgain = 0x7;
		ki_min = 0x0117;
		clp_ctrl_mode = 0;
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_MINGAIN__A, 0x7fff, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_MAXGAIN__A, 0x0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_SUM__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_CYCCNT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_DIR_WD__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_DIR_STP__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_SUM__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_CYCCNT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_DIR_WD__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_DIR_STP__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		p_agc_if_settings = &(ext_attr->qam_if_agc_cfg);
		p_agc_rf_settings = &(ext_attr->qam_rf_agc_cfg);
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_INGAIN_TGT__A, p_agc_if_settings->top, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_AGC_KI__A, &agc_ki, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		agc_ki &= 0xf000;
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI__A, agc_ki, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_FM:
		clp_sum_max = 1023;
		sns_sum_max = 1023;
		ki_innergain_min = (u16) (-32768);
		if_iaccu_hi_tgt_min = 2047;
		agc_ki_dgain = 0x7;
		ki_min = 0x0225;
		ki_max = 0x0547;
		clp_dir_to = (u16) (-9);
		sns_dir_to = (u16) (-9);
		ingain_tgt_max = 9000;
		clp_ctrl_mode = 1;
		p_agc_if_settings = &(ext_attr->atv_if_agc_cfg);
		p_agc_rf_settings = &(ext_attr->atv_rf_agc_cfg);
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_INGAIN_TGT__A, p_agc_if_settings->top, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
		clp_sum_max = 1023;
		sns_sum_max = 1023;
		ki_innergain_min = (u16) (-32768);
		if_iaccu_hi_tgt_min = 2047;
		agc_ki_dgain = 0x7;
		ki_min = 0x0225;
		ki_max = 0x0547;
		clp_dir_to = (u16) (-9);
		ingain_tgt_max = 9000;
		p_agc_if_settings = &(ext_attr->atv_if_agc_cfg);
		p_agc_rf_settings = &(ext_attr->atv_rf_agc_cfg);
		sns_dir_to = (u16) (-9);
		clp_ctrl_mode = 1;
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_INGAIN_TGT__A, p_agc_if_settings->top, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
		clp_sum_max = 1023;
		sns_sum_max = 1023;
		ki_innergain_min = (u16) (-32768);
		if_iaccu_hi_tgt_min = 2047;
		agc_ki_dgain = 0x7;
		ki_min = 0x0225;
		ki_max = 0x0547;
		clp_dir_to = (u16) (-9);
		sns_dir_to = (u16) (-9);
		ingain_tgt_max = 9000;
		clp_ctrl_mode = 1;
		p_agc_if_settings = &(ext_attr->atv_if_agc_cfg);
		p_agc_rf_settings = &(ext_attr->atv_rf_agc_cfg);
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_INGAIN_TGT__A, p_agc_if_settings->top, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
	default:
		return -EINVAL;
	}

	/* for new AGC interface */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_INGAIN_TGT_MIN__A, p_agc_if_settings->top, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_INGAIN__A, p_agc_if_settings->top, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* Gain fed from inner to outer AGC */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_INGAIN_TGT_MAX__A, ingain_tgt_max, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_IF_IACCU_HI_TGT_MIN__A, if_iaccu_hi_tgt_min, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_IF_IACCU_HI__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* set to p_agc_settings->top before */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_IF_IACCU_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_RF_IACCU_HI__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_RF_IACCU_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_RF_MAX__A, 32767, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_SUM_MAX__A, clp_sum_max, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_SUM_MAX__A, sns_sum_max, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_INNERGAIN_MIN__A, ki_innergain_min, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_FAST_SNS_CTRL_DELAY__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_CYCLEN__A, 500, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_CYCLEN__A, 500, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_MAXMINGAIN_TH__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_MIN__A, ki_min, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_MAX__A, ki_max, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI_RED__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_SUM_MIN__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_CYCLEN__A, 500, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_DIR_TO__A, clp_dir_to, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_SUM_MIN__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_SNS_DIR_TO__A, sns_dir_to, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_FAST_CLP_CTRL_DELAY__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_CLP_CTRL_MODE__A, clp_ctrl_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	agc_rf = 0x800 + p_agc_rf_settings->cut_off_current;
	if (common_attr->tuner_rf_agc_pol == true)
		agc_rf = 0x87ff - agc_rf;

	agc_if = 0x800;
	if (common_attr->tuner_if_agc_pol == true)
		agc_rf = 0x87ff - agc_rf;

	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_AGC_RF__A, agc_rf, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_AGC_IF__A, agc_if, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Set/restore Ki DGAIN factor */
	rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_AGC_KI__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	data &= ~SCU_RAM_AGC_KI_DGAIN__M;
	data |= (agc_ki_dgain << SCU_RAM_AGC_KI_DGAIN__B);
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_AGC_KI__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int set_frequency ()
* \brief Set frequency shift.
* \param demod instance of demodulator.
* \param channel pointer to channel data.
* \param tuner_freq_offset residual frequency from tuner.
* \return int.
*/
static int
set_frequency(struct drx_demod_instance *demod,
	      struct drx_channel *channel, s32 tuner_freq_offset)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drxj_data *ext_attr = demod->my_ext_attr;
	int rc;
	s32 sampling_frequency = 0;
	s32 frequency_shift = 0;
	s32 if_freq_actual = 0;
	s32 rf_freq_residual = -1 * tuner_freq_offset;
	s32 adc_freq = 0;
	s32 intermediate_freq = 0;
	u32 iqm_fs_rate_ofs = 0;
	bool adc_flip = true;
	bool select_pos_image = false;
	bool rf_mirror;
	bool tuner_mirror;
	bool image_to_select = true;
	s32 fm_frequency_shift = 0;

	rf_mirror = (ext_attr->mirror == DRX_MIRROR_YES) ? true : false;
	tuner_mirror = demod->my_common_attr->mirror_freq_spect ? false : true;
	/*
	   Program frequency shifter
	   No need to account for mirroring on RF
	 */
	switch (ext_attr->standard) {
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_C:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_8VSB:
		select_pos_image = true;
		break;
	case DRX_STANDARD_FM:
		/* After IQM FS sound carrier must appear at 4 Mhz in spect.
		   Sound carrier is already 3Mhz above centre frequency due
		   to tuner setting so now add an extra shift of 1MHz... */
		fm_frequency_shift = 1000;
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:
		select_pos_image = false;
		break;
	default:
		return -EINVAL;
	}
	intermediate_freq = demod->my_common_attr->intermediate_freq;
	sampling_frequency = demod->my_common_attr->sys_clock_freq / 3;
	if (tuner_mirror)
		if_freq_actual = intermediate_freq + rf_freq_residual + fm_frequency_shift;
	else
		if_freq_actual = intermediate_freq - rf_freq_residual - fm_frequency_shift;
	if (if_freq_actual > sampling_frequency / 2) {
		/* adc mirrors */
		adc_freq = sampling_frequency - if_freq_actual;
		adc_flip = true;
	} else {
		/* adc doesn't mirror */
		adc_freq = if_freq_actual;
		adc_flip = false;
	}

	frequency_shift = adc_freq;
	image_to_select =
	    (bool) (rf_mirror ^ tuner_mirror ^ adc_flip ^ select_pos_image);
	iqm_fs_rate_ofs = frac28(frequency_shift, sampling_frequency);

	if (image_to_select)
		iqm_fs_rate_ofs = ~iqm_fs_rate_ofs + 1;

	/* Program frequency shifter with tuner offset compensation */
	/* frequency_shift += tuner_freq_offset; TODO */
	rc = DRXJ_DAP.write_reg32func(dev_addr, IQM_FS_RATE_OFS_LO__A, iqm_fs_rate_ofs, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->iqm_fs_rate_ofs = iqm_fs_rate_ofs;
	ext_attr->pos_image = (bool) (rf_mirror ^ tuner_mirror ^ select_pos_image);

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int get_sig_strength()
* \brief Retrieve signal strength for VSB and QAM.
* \param demod Pointer to demod instance
* \param u16-t Pointer to signal strength data; range 0, .. , 100.
* \return int.
* \retval 0 sig_strength contains valid data.
* \retval -EINVAL sig_strength is NULL.
* \retval -EIO Erroneous data, sig_strength contains invalid data.
*/
#define DRXJ_AGC_TOP    0x2800
#define DRXJ_AGC_SNS    0x1600
#define DRXJ_RFAGC_MAX  0x3fff
#define DRXJ_RFAGC_MIN  0x800

static int get_sig_strength(struct drx_demod_instance *demod, u16 *sig_strength)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	u16 rf_gain = 0;
	u16 if_gain = 0;
	u16 if_agc_sns = 0;
	u16 if_agc_top = 0;
	u16 rf_agc_max = 0;
	u16 rf_agc_min = 0;

	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_AGC_IF__A, &if_gain, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if_gain &= IQM_AF_AGC_IF__M;
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_AGC_RF__A, &rf_gain, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rf_gain &= IQM_AF_AGC_RF__M;

	if_agc_sns = DRXJ_AGC_SNS;
	if_agc_top = DRXJ_AGC_TOP;
	rf_agc_max = DRXJ_RFAGC_MAX;
	rf_agc_min = DRXJ_RFAGC_MIN;

	if (if_gain > if_agc_top) {
		if (rf_gain > rf_agc_max)
			*sig_strength = 100;
		else if (rf_gain > rf_agc_min) {
			if (rf_agc_max == rf_agc_min) {
				pr_err("error: rf_agc_max == rf_agc_min\n");
				return -EIO;
			}
			*sig_strength =
			    75 + 25 * (rf_gain - rf_agc_min) / (rf_agc_max -
							     rf_agc_min);
		} else
			*sig_strength = 75;
	} else if (if_gain > if_agc_sns) {
		if (if_agc_top == if_agc_sns) {
			pr_err("error: if_agc_top == if_agc_sns\n");
			return -EIO;
		}
		*sig_strength =
		    20 + 55 * (if_gain - if_agc_sns) / (if_agc_top - if_agc_sns);
	} else {
		if (!if_agc_sns) {
			pr_err("error: if_agc_sns is zero!\n");
			return -EIO;
		}
		*sig_strength = (20 * if_gain / if_agc_sns);
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int get_acc_pkt_err()
* \brief Retrieve signal strength for VSB and QAM.
* \param demod Pointer to demod instance
* \param packet_err Pointer to packet error
* \return int.
* \retval 0 sig_strength contains valid data.
* \retval -EINVAL sig_strength is NULL.
* \retval -EIO Erroneous data, sig_strength contains invalid data.
*/
#ifdef DRXJ_SIGNAL_ACCUM_ERR
static int get_acc_pkt_err(struct drx_demod_instance *demod, u16 *packet_err)
{
	int rc;
	static u16 pkt_err;
	static u16 last_pkt_err;
	u16 data = 0;
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	dev_addr = demod->my_i2c_dev_addr;

	rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (ext_attr->reset_pkt_err_acc) {
		last_pkt_err = data;
		pkt_err = 0;
		ext_attr->reset_pkt_err_acc = false;
	}

	if (data < last_pkt_err) {
		pkt_err += 0xffff - last_pkt_err;
		pkt_err += data;
	} else {
		pkt_err += (data - last_pkt_err);
	}
	*packet_err = pkt_err;
	last_pkt_err = data;

	return 0;
rw_error:
	return -EIO;
}
#endif

/**
* \fn int ResetAccPktErr()
* \brief Reset Accumulating packet error count.
* \param demod Pointer to demod instance
* \return int.
* \retval 0.
* \retval -EIO Erroneous data.
*/
static int ctrl_set_cfg_reset_pkt_err(struct drx_demod_instance *demod)
{
#ifdef DRXJ_SIGNAL_ACCUM_ERR
	struct drxj_data *ext_attr = NULL;
	int rc;
	u16 packet_error = 0;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	ext_attr->reset_pkt_err_acc = true;
	/* call to reset counter */
	rc = get_acc_pkt_err(demod, &packet_error);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
#endif
	return -EIO;
}

/**
* \fn static short get_str_freq_offset()
* \brief Get symbol rate offset in QAM & 8VSB mode
* \return Error code
*/
static int get_str_freq_offset(struct drx_demod_instance *demod, s32 *str_freq)
{
	int rc;
	u32 symbol_frequency_ratio = 0;
	u32 symbol_nom_frequency_ratio = 0;

	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drxj_data *ext_attr = demod->my_ext_attr;

	rc = drxj_dap_atomic_read_reg32(dev_addr, IQM_RC_RATE_LO__A, &symbol_frequency_ratio, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	symbol_nom_frequency_ratio = ext_attr->iqm_rc_rate_ofs;

	if (symbol_frequency_ratio > symbol_nom_frequency_ratio)
		*str_freq =
		    -1 *
		    frac_times1e6((symbol_frequency_ratio -
				  symbol_nom_frequency_ratio),
				 (symbol_frequency_ratio + (1 << 23)));
	else
		*str_freq =
		    frac_times1e6((symbol_nom_frequency_ratio -
				  symbol_frequency_ratio),
				 (symbol_frequency_ratio + (1 << 23)));

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn static short get_ctl_freq_offset
* \brief Get the value of ctl_freq in QAM & ATSC mode
* \return Error code
*/
static int get_ctl_freq_offset(struct drx_demod_instance *demod, s32 *ctl_freq)
{
	s32 sampling_frequency = 0;
	s32 current_frequency = 0;
	s32 nominal_frequency = 0;
	s32 carrier_frequency_shift = 0;
	s32 sign = 1;
	u32 data64hi = 0;
	u32 data64lo = 0;
	struct drxj_data *ext_attr = NULL;
	struct drx_common_attr *common_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	int rc;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;

	sampling_frequency = common_attr->sys_clock_freq / 3;

	/* both registers are sign extended */
	nominal_frequency = ext_attr->iqm_fs_rate_ofs;
	rc = drxj_dap_atomic_read_reg32(dev_addr, IQM_FS_RATE_LO__A, (u32 *)&current_frequency, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (ext_attr->pos_image) {
		/* negative image */
		carrier_frequency_shift = nominal_frequency - current_frequency;
	} else {
		/* positive image */
		carrier_frequency_shift = current_frequency - nominal_frequency;
	}

	/* carrier Frequency Shift In Hz */
	if (carrier_frequency_shift < 0) {
		sign = -1;
		carrier_frequency_shift *= sign;
	}

	/* *ctl_freq = carrier_frequency_shift * 50.625e6 / (1 << 28); */
	mult32(carrier_frequency_shift, sampling_frequency, &data64hi, &data64lo);
	*ctl_freq =
	    (s32) ((((data64lo >> 28) & 0xf) | (data64hi << 4)) * sign);

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int set_agc_rf ()
* \brief Configure RF AGC
* \param demod instance of demodulator.
* \param agc_settings AGC configuration structure
* \return int.
*/
static int
set_agc_rf(struct drx_demod_instance *demod, struct drxj_cfg_agc *agc_settings, bool atomic)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	struct drxj_cfg_agc *p_agc_settings = NULL;
	struct drx_common_attr *common_attr = NULL;
	int rc;
	drx_write_reg16func_t scu_wr16 = NULL;
	drx_read_reg16func_t scu_rr16 = NULL;

	common_attr = (struct drx_common_attr *) demod->my_common_attr;
	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	if (atomic) {
		scu_rr16 = drxj_dap_scu_atomic_read_reg16;
		scu_wr16 = drxj_dap_scu_atomic_write_reg16;
	} else {
		scu_rr16 = DRXJ_DAP.read_reg16func;
		scu_wr16 = DRXJ_DAP.write_reg16func;
	}

	/* Configure AGC only if standard is currently active */
	if ((ext_attr->standard == agc_settings->standard) ||
	    (DRXJ_ISQAMSTD(ext_attr->standard) &&
	     DRXJ_ISQAMSTD(agc_settings->standard)) ||
	    (DRXJ_ISATVSTD(ext_attr->standard) &&
	     DRXJ_ISATVSTD(agc_settings->standard))) {
		u16 data = 0;

		switch (agc_settings->ctrl_mode) {
		case DRX_AGC_CTRL_AUTO:

			/* Enable RF AGC DAC */
			rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data |= IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE;
			rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_STDBY__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Enable SCU RF AGC loop */
			rc = (*scu_rr16)(dev_addr, SCU_RAM_AGC_KI__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= ~SCU_RAM_AGC_KI_RF__M;
			if (ext_attr->standard == DRX_STANDARD_8VSB)
				data |= (2 << SCU_RAM_AGC_KI_RF__B);
			else if (DRXJ_ISQAMSTD(ext_attr->standard))
				data |= (5 << SCU_RAM_AGC_KI_RF__B);
			else
				data |= (4 << SCU_RAM_AGC_KI_RF__B);

			if (common_attr->tuner_rf_agc_pol)
				data |= SCU_RAM_AGC_KI_INV_RF_POL__M;
			else
				data &= ~SCU_RAM_AGC_KI_INV_RF_POL__M;
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_KI__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Set speed ( using complementary reduction value ) */
			rc = (*scu_rr16)(dev_addr, SCU_RAM_AGC_KI_RED__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= ~SCU_RAM_AGC_KI_RED_RAGC_RED__M;
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_KI_RED__A, (~(agc_settings->speed << SCU_RAM_AGC_KI_RED_RAGC_RED__B) & SCU_RAM_AGC_KI_RED_RAGC_RED__M) | data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			if (agc_settings->standard == DRX_STANDARD_8VSB)
				p_agc_settings = &(ext_attr->vsb_if_agc_cfg);
			else if (DRXJ_ISQAMSTD(agc_settings->standard))
				p_agc_settings = &(ext_attr->qam_if_agc_cfg);
			else if (DRXJ_ISATVSTD(agc_settings->standard))
				p_agc_settings = &(ext_attr->atv_if_agc_cfg);
			else
				return -EINVAL;

			/* Set TOP, only if IF-AGC is in AUTO mode */
			if (p_agc_settings->ctrl_mode == DRX_AGC_CTRL_AUTO) {
				rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A, agc_settings->top, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_IF_IACCU_HI_TGT__A, agc_settings->top, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
			}

			/* Cut-Off current */
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_RF_IACCU_HI_CO__A, agc_settings->cut_off_current, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_AGC_CTRL_USER:

			/* Enable RF AGC DAC */
			rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data |= IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE;
			rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_STDBY__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Disable SCU RF AGC loop */
			rc = (*scu_rr16)(dev_addr, SCU_RAM_AGC_KI__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= ~SCU_RAM_AGC_KI_RF__M;
			if (common_attr->tuner_rf_agc_pol)
				data |= SCU_RAM_AGC_KI_INV_RF_POL__M;
			else
				data &= ~SCU_RAM_AGC_KI_INV_RF_POL__M;
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_KI__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Write value to output pin */
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_RF_IACCU_HI__A, agc_settings->output_level, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_AGC_CTRL_OFF:

			/* Disable RF AGC DAC */
			rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= (~IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE);
			rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_STDBY__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Disable SCU RF AGC loop */
			rc = (*scu_rr16)(dev_addr, SCU_RAM_AGC_KI__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= ~SCU_RAM_AGC_KI_RF__M;
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_KI__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		default:
			return -EINVAL;
		}		/* switch ( agcsettings->ctrl_mode ) */
	}

	/* Store rf agc settings */
	switch (agc_settings->standard) {
	case DRX_STANDARD_8VSB:
		ext_attr->vsb_rf_agc_cfg = *agc_settings;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		ext_attr->qam_rf_agc_cfg = *agc_settings;
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_FM:
		ext_attr->atv_rf_agc_cfg = *agc_settings;
		break;
#endif
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int get_agc_rf ()
* \brief get configuration of RF AGC
* \param demod instance of demodulator.
* \param agc_settings AGC configuration structure
* \return int.
*/
static int
get_agc_rf(struct drx_demod_instance *demod, struct drxj_cfg_agc *agc_settings)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
	int rc;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* Return stored AGC settings */
	standard = agc_settings->standard;
	switch (agc_settings->standard) {
	case DRX_STANDARD_8VSB:
		*agc_settings = ext_attr->vsb_rf_agc_cfg;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		*agc_settings = ext_attr->qam_rf_agc_cfg;
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_FM:
		*agc_settings = ext_attr->atv_rf_agc_cfg;
		break;
#endif
	default:
		return -EIO;
	}
	agc_settings->standard = standard;

	/* Get AGC output only if standard is currently active. */
	if ((ext_attr->standard == agc_settings->standard) ||
	    (DRXJ_ISQAMSTD(ext_attr->standard) &&
	     DRXJ_ISQAMSTD(agc_settings->standard)) ||
	    (DRXJ_ISATVSTD(ext_attr->standard) &&
	     DRXJ_ISATVSTD(agc_settings->standard))) {
		rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_AGC_RF_IACCU_HI__A, &(agc_settings->output_level), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int set_agc_if ()
* \brief Configure If AGC
* \param demod instance of demodulator.
* \param agc_settings AGC configuration structure
* \return int.
*/
static int
set_agc_if(struct drx_demod_instance *demod, struct drxj_cfg_agc *agc_settings, bool atomic)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	struct drxj_cfg_agc *p_agc_settings = NULL;
	struct drx_common_attr *common_attr = NULL;
	drx_write_reg16func_t scu_wr16 = NULL;
	drx_read_reg16func_t scu_rr16 = NULL;
	int rc;

	common_attr = (struct drx_common_attr *) demod->my_common_attr;
	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	if (atomic) {
		scu_rr16 = drxj_dap_scu_atomic_read_reg16;
		scu_wr16 = drxj_dap_scu_atomic_write_reg16;
	} else {
		scu_rr16 = DRXJ_DAP.read_reg16func;
		scu_wr16 = DRXJ_DAP.write_reg16func;
	}

	/* Configure AGC only if standard is currently active */
	if ((ext_attr->standard == agc_settings->standard) ||
	    (DRXJ_ISQAMSTD(ext_attr->standard) &&
	     DRXJ_ISQAMSTD(agc_settings->standard)) ||
	    (DRXJ_ISATVSTD(ext_attr->standard) &&
	     DRXJ_ISATVSTD(agc_settings->standard))) {
		u16 data = 0;

		switch (agc_settings->ctrl_mode) {
		case DRX_AGC_CTRL_AUTO:
			/* Enable IF AGC DAC */
			rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data |= IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE;
			rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_STDBY__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Enable SCU IF AGC loop */
			rc = (*scu_rr16)(dev_addr, SCU_RAM_AGC_KI__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= ~SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			data &= ~SCU_RAM_AGC_KI_IF__M;
			if (ext_attr->standard == DRX_STANDARD_8VSB)
				data |= (3 << SCU_RAM_AGC_KI_IF__B);
			else if (DRXJ_ISQAMSTD(ext_attr->standard))
				data |= (6 << SCU_RAM_AGC_KI_IF__B);
			else
				data |= (5 << SCU_RAM_AGC_KI_IF__B);

			if (common_attr->tuner_if_agc_pol)
				data |= SCU_RAM_AGC_KI_INV_IF_POL__M;
			else
				data &= ~SCU_RAM_AGC_KI_INV_IF_POL__M;
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_KI__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Set speed (using complementary reduction value) */
			rc = (*scu_rr16)(dev_addr, SCU_RAM_AGC_KI_RED__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= ~SCU_RAM_AGC_KI_RED_IAGC_RED__M;
			rc = (*scu_wr16) (dev_addr, SCU_RAM_AGC_KI_RED__A, (~(agc_settings->speed << SCU_RAM_AGC_KI_RED_IAGC_RED__B) & SCU_RAM_AGC_KI_RED_IAGC_RED__M) | data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			if (agc_settings->standard == DRX_STANDARD_8VSB)
				p_agc_settings = &(ext_attr->vsb_rf_agc_cfg);
			else if (DRXJ_ISQAMSTD(agc_settings->standard))
				p_agc_settings = &(ext_attr->qam_rf_agc_cfg);
			else if (DRXJ_ISATVSTD(agc_settings->standard))
				p_agc_settings = &(ext_attr->atv_rf_agc_cfg);
			else
				return -EINVAL;

			/* Restore TOP */
			if (p_agc_settings->ctrl_mode == DRX_AGC_CTRL_AUTO) {
				rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A, p_agc_settings->top, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_IF_IACCU_HI_TGT__A, p_agc_settings->top, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
			} else {
				rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A, 0, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_IF_IACCU_HI_TGT__A, 0, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
			}
			break;

		case DRX_AGC_CTRL_USER:

			/* Enable IF AGC DAC */
			rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data |= IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE;
			rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_STDBY__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Disable SCU IF AGC loop */
			rc = (*scu_rr16)(dev_addr, SCU_RAM_AGC_KI__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= ~SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			data |= SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			if (common_attr->tuner_if_agc_pol)
				data |= SCU_RAM_AGC_KI_INV_IF_POL__M;
			else
				data &= ~SCU_RAM_AGC_KI_INV_IF_POL__M;
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_KI__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Write value to output pin */
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A, agc_settings->output_level, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;

		case DRX_AGC_CTRL_OFF:

			/* Disable If AGC DAC */
			rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= (~IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE);
			rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_STDBY__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Disable SCU IF AGC loop */
			rc = (*scu_rr16)(dev_addr, SCU_RAM_AGC_KI__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= ~SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			data |= SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			rc = (*scu_wr16)(dev_addr, SCU_RAM_AGC_KI__A, data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		default:
			return -EINVAL;
		}		/* switch ( agcsettings->ctrl_mode ) */

		/* always set the top to support configurations without if-loop */
		rc = (*scu_wr16) (dev_addr, SCU_RAM_AGC_INGAIN_TGT_MIN__A, agc_settings->top, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* Store if agc settings */
	switch (agc_settings->standard) {
	case DRX_STANDARD_8VSB:
		ext_attr->vsb_if_agc_cfg = *agc_settings;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		ext_attr->qam_if_agc_cfg = *agc_settings;
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_FM:
		ext_attr->atv_if_agc_cfg = *agc_settings;
		break;
#endif
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int get_agc_if ()
* \brief get configuration of If AGC
* \param demod instance of demodulator.
* \param agc_settings AGC configuration structure
* \return int.
*/
static int
get_agc_if(struct drx_demod_instance *demod, struct drxj_cfg_agc *agc_settings)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
	int rc;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* Return stored ATV AGC settings */
	standard = agc_settings->standard;
	switch (agc_settings->standard) {
	case DRX_STANDARD_8VSB:
		*agc_settings = ext_attr->vsb_if_agc_cfg;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		*agc_settings = ext_attr->qam_if_agc_cfg;
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_FM:
		*agc_settings = ext_attr->atv_if_agc_cfg;
		break;
#endif
	default:
		return -EIO;
	}
	agc_settings->standard = standard;

	/* Get AGC output only if standard is currently active */
	if ((ext_attr->standard == agc_settings->standard) ||
	    (DRXJ_ISQAMSTD(ext_attr->standard) &&
	     DRXJ_ISQAMSTD(agc_settings->standard)) ||
	    (DRXJ_ISATVSTD(ext_attr->standard) &&
	     DRXJ_ISATVSTD(agc_settings->standard))) {
		/* read output level */
		rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_AGC_IF_IACCU_HI__A, &(agc_settings->output_level), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int set_iqm_af ()
* \brief Configure IQM AF registers
* \param demod instance of demodulator.
* \param active
* \return int.
*/
static int set_iqm_af(struct drx_demod_instance *demod, bool active)
{
	u16 data = 0;
	struct i2c_device_addr *dev_addr = NULL;
	int rc;

	dev_addr = demod->my_i2c_dev_addr;

	/* Configure IQM */
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_STDBY__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (!active)
		data &= ((~IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_PD_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE));
	else
		data |= (IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE | IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE | IQM_AF_STDBY_STDBY_PD_A2_ACTIVE | IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE | IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE);
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_STDBY__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/*==              END 8VSB & QAM COMMON DATAPATH FUNCTIONS                  ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                       8VSB DATAPATH FUNCTIONS                          ==*/
/*============================================================================*/
/*============================================================================*/

/**
* \fn int power_down_vsb ()
* \brief Powr down QAM related blocks.
* \param demod instance of demodulator.
* \param channel pointer to channel data.
* \return int.
*/
static int power_down_vsb(struct drx_demod_instance *demod, bool primary)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drxjscu_cmd cmd_scu = { /* command     */ 0,
		/* parameter_len */ 0,
		/* result_len    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	struct drx_cfg_mpeg_output cfg_mpeg_output;
	int rc;
	u16 cmd_result = 0;

	/*
	   STOP demodulator
	   reset of FEC and VSB HW
	 */
	cmd_scu.command = SCU_RAM_COMMAND_STANDARD_VSB |
	    SCU_RAM_COMMAND_CMD_DEMOD_STOP;
	cmd_scu.parameter_len = 0;
	cmd_scu.result_len = 1;
	cmd_scu.parameter = NULL;
	cmd_scu.result = &cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* stop all comm_exec */
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_COMM_EXEC__A, VSB_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (primary) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = set_iqm_af(demod, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	cfg_mpeg_output.enable_mpeg_output = false;
	rc = ctrl_set_cfg_mpeg_output(demod, &cfg_mpeg_output);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int set_vsb_leak_n_gain ()
* \brief Set ATSC demod.
* \param demod instance of demodulator.
* \return int.
*/
static int set_vsb_leak_n_gain(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;

	const u8 vsb_ffe_leak_gain_ram0[] = {
		DRXJ_16TO8(0x8),	/* FFETRAINLKRATIO1  */
		DRXJ_16TO8(0x8),	/* FFETRAINLKRATIO2  */
		DRXJ_16TO8(0x8),	/* FFETRAINLKRATIO3  */
		DRXJ_16TO8(0xf),	/* FFETRAINLKRATIO4  */
		DRXJ_16TO8(0xf),	/* FFETRAINLKRATIO5  */
		DRXJ_16TO8(0xf),	/* FFETRAINLKRATIO6  */
		DRXJ_16TO8(0xf),	/* FFETRAINLKRATIO7  */
		DRXJ_16TO8(0xf),	/* FFETRAINLKRATIO8  */
		DRXJ_16TO8(0xf),	/* FFETRAINLKRATIO9  */
		DRXJ_16TO8(0x8),	/* FFETRAINLKRATIO10  */
		DRXJ_16TO8(0x8),	/* FFETRAINLKRATIO11 */
		DRXJ_16TO8(0x8),	/* FFETRAINLKRATIO12 */
		DRXJ_16TO8(0x10),	/* FFERCA1TRAINLKRATIO1 */
		DRXJ_16TO8(0x10),	/* FFERCA1TRAINLKRATIO2 */
		DRXJ_16TO8(0x10),	/* FFERCA1TRAINLKRATIO3 */
		DRXJ_16TO8(0x20),	/* FFERCA1TRAINLKRATIO4 */
		DRXJ_16TO8(0x20),	/* FFERCA1TRAINLKRATIO5 */
		DRXJ_16TO8(0x20),	/* FFERCA1TRAINLKRATIO6 */
		DRXJ_16TO8(0x20),	/* FFERCA1TRAINLKRATIO7 */
		DRXJ_16TO8(0x20),	/* FFERCA1TRAINLKRATIO8 */
		DRXJ_16TO8(0x20),	/* FFERCA1TRAINLKRATIO9 */
		DRXJ_16TO8(0x10),	/* FFERCA1TRAINLKRATIO10 */
		DRXJ_16TO8(0x10),	/* FFERCA1TRAINLKRATIO11 */
		DRXJ_16TO8(0x10),	/* FFERCA1TRAINLKRATIO12 */
		DRXJ_16TO8(0x10),	/* FFERCA1DATALKRATIO1 */
		DRXJ_16TO8(0x10),	/* FFERCA1DATALKRATIO2 */
		DRXJ_16TO8(0x10),	/* FFERCA1DATALKRATIO3 */
		DRXJ_16TO8(0x20),	/* FFERCA1DATALKRATIO4 */
		DRXJ_16TO8(0x20),	/* FFERCA1DATALKRATIO5 */
		DRXJ_16TO8(0x20),	/* FFERCA1DATALKRATIO6 */
		DRXJ_16TO8(0x20),	/* FFERCA1DATALKRATIO7 */
		DRXJ_16TO8(0x20),	/* FFERCA1DATALKRATIO8 */
		DRXJ_16TO8(0x20),	/* FFERCA1DATALKRATIO9 */
		DRXJ_16TO8(0x10),	/* FFERCA1DATALKRATIO10 */
		DRXJ_16TO8(0x10),	/* FFERCA1DATALKRATIO11 */
		DRXJ_16TO8(0x10),	/* FFERCA1DATALKRATIO12 */
		DRXJ_16TO8(0x10),	/* FFERCA2TRAINLKRATIO1 */
		DRXJ_16TO8(0x10),	/* FFERCA2TRAINLKRATIO2 */
		DRXJ_16TO8(0x10),	/* FFERCA2TRAINLKRATIO3 */
		DRXJ_16TO8(0x20),	/* FFERCA2TRAINLKRATIO4 */
		DRXJ_16TO8(0x20),	/* FFERCA2TRAINLKRATIO5 */
		DRXJ_16TO8(0x20),	/* FFERCA2TRAINLKRATIO6 */
		DRXJ_16TO8(0x20),	/* FFERCA2TRAINLKRATIO7 */
		DRXJ_16TO8(0x20),	/* FFERCA2TRAINLKRATIO8 */
		DRXJ_16TO8(0x20),	/* FFERCA2TRAINLKRATIO9 */
		DRXJ_16TO8(0x10),	/* FFERCA2TRAINLKRATIO10 */
		DRXJ_16TO8(0x10),	/* FFERCA2TRAINLKRATIO11 */
		DRXJ_16TO8(0x10),	/* FFERCA2TRAINLKRATIO12 */
		DRXJ_16TO8(0x10),	/* FFERCA2DATALKRATIO1 */
		DRXJ_16TO8(0x10),	/* FFERCA2DATALKRATIO2 */
		DRXJ_16TO8(0x10),	/* FFERCA2DATALKRATIO3 */
		DRXJ_16TO8(0x20),	/* FFERCA2DATALKRATIO4 */
		DRXJ_16TO8(0x20),	/* FFERCA2DATALKRATIO5 */
		DRXJ_16TO8(0x20),	/* FFERCA2DATALKRATIO6 */
		DRXJ_16TO8(0x20),	/* FFERCA2DATALKRATIO7 */
		DRXJ_16TO8(0x20),	/* FFERCA2DATALKRATIO8 */
		DRXJ_16TO8(0x20),	/* FFERCA2DATALKRATIO9 */
		DRXJ_16TO8(0x10),	/* FFERCA2DATALKRATIO10 */
		DRXJ_16TO8(0x10),	/* FFERCA2DATALKRATIO11 */
		DRXJ_16TO8(0x10),	/* FFERCA2DATALKRATIO12 */
		DRXJ_16TO8(0x07),	/* FFEDDM1TRAINLKRATIO1 */
		DRXJ_16TO8(0x07),	/* FFEDDM1TRAINLKRATIO2 */
		DRXJ_16TO8(0x07),	/* FFEDDM1TRAINLKRATIO3 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1TRAINLKRATIO4 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1TRAINLKRATIO5 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1TRAINLKRATIO6 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1TRAINLKRATIO7 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1TRAINLKRATIO8 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1TRAINLKRATIO9 */
		DRXJ_16TO8(0x07),	/* FFEDDM1TRAINLKRATIO10 */
		DRXJ_16TO8(0x07),	/* FFEDDM1TRAINLKRATIO11 */
		DRXJ_16TO8(0x07),	/* FFEDDM1TRAINLKRATIO12 */
		DRXJ_16TO8(0x07),	/* FFEDDM1DATALKRATIO1 */
		DRXJ_16TO8(0x07),	/* FFEDDM1DATALKRATIO2 */
		DRXJ_16TO8(0x07),	/* FFEDDM1DATALKRATIO3 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1DATALKRATIO4 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1DATALKRATIO5 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1DATALKRATIO6 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1DATALKRATIO7 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1DATALKRATIO8 */
		DRXJ_16TO8(0x0e),	/* FFEDDM1DATALKRATIO9 */
		DRXJ_16TO8(0x07),	/* FFEDDM1DATALKRATIO10 */
		DRXJ_16TO8(0x07),	/* FFEDDM1DATALKRATIO11 */
		DRXJ_16TO8(0x07),	/* FFEDDM1DATALKRATIO12 */
		DRXJ_16TO8(0x06),	/* FFEDDM2TRAINLKRATIO1 */
		DRXJ_16TO8(0x06),	/* FFEDDM2TRAINLKRATIO2 */
		DRXJ_16TO8(0x06),	/* FFEDDM2TRAINLKRATIO3 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2TRAINLKRATIO4 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2TRAINLKRATIO5 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2TRAINLKRATIO6 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2TRAINLKRATIO7 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2TRAINLKRATIO8 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2TRAINLKRATIO9 */
		DRXJ_16TO8(0x06),	/* FFEDDM2TRAINLKRATIO10 */
		DRXJ_16TO8(0x06),	/* FFEDDM2TRAINLKRATIO11 */
		DRXJ_16TO8(0x06),	/* FFEDDM2TRAINLKRATIO12 */
		DRXJ_16TO8(0x06),	/* FFEDDM2DATALKRATIO1 */
		DRXJ_16TO8(0x06),	/* FFEDDM2DATALKRATIO2 */
		DRXJ_16TO8(0x06),	/* FFEDDM2DATALKRATIO3 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2DATALKRATIO4 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2DATALKRATIO5 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2DATALKRATIO6 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2DATALKRATIO7 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2DATALKRATIO8 */
		DRXJ_16TO8(0x0c),	/* FFEDDM2DATALKRATIO9 */
		DRXJ_16TO8(0x06),	/* FFEDDM2DATALKRATIO10 */
		DRXJ_16TO8(0x06),	/* FFEDDM2DATALKRATIO11 */
		DRXJ_16TO8(0x06),	/* FFEDDM2DATALKRATIO12 */
		DRXJ_16TO8(0x2020),	/* FIRTRAINGAIN1 */
		DRXJ_16TO8(0x2020),	/* FIRTRAINGAIN2 */
		DRXJ_16TO8(0x2020),	/* FIRTRAINGAIN3 */
		DRXJ_16TO8(0x4040),	/* FIRTRAINGAIN4 */
		DRXJ_16TO8(0x4040),	/* FIRTRAINGAIN5 */
		DRXJ_16TO8(0x4040),	/* FIRTRAINGAIN6 */
		DRXJ_16TO8(0x4040),	/* FIRTRAINGAIN7 */
		DRXJ_16TO8(0x4040),	/* FIRTRAINGAIN8 */
		DRXJ_16TO8(0x4040),	/* FIRTRAINGAIN9 */
		DRXJ_16TO8(0x2020),	/* FIRTRAINGAIN10 */
		DRXJ_16TO8(0x2020),	/* FIRTRAINGAIN11 */
		DRXJ_16TO8(0x2020),	/* FIRTRAINGAIN12 */
		DRXJ_16TO8(0x0808),	/* FIRRCA1GAIN1 */
		DRXJ_16TO8(0x0808),	/* FIRRCA1GAIN2 */
		DRXJ_16TO8(0x0808),	/* FIRRCA1GAIN3 */
		DRXJ_16TO8(0x1010),	/* FIRRCA1GAIN4 */
		DRXJ_16TO8(0x1010),	/* FIRRCA1GAIN5 */
		DRXJ_16TO8(0x1010),	/* FIRRCA1GAIN6 */
		DRXJ_16TO8(0x1010),	/* FIRRCA1GAIN7 */
		DRXJ_16TO8(0x1010)	/* FIRRCA1GAIN8 */
	};

	const u8 vsb_ffe_leak_gain_ram1[] = {
		DRXJ_16TO8(0x1010),	/* FIRRCA1GAIN9 */
		DRXJ_16TO8(0x0808),	/* FIRRCA1GAIN10 */
		DRXJ_16TO8(0x0808),	/* FIRRCA1GAIN11 */
		DRXJ_16TO8(0x0808),	/* FIRRCA1GAIN12 */
		DRXJ_16TO8(0x0808),	/* FIRRCA2GAIN1 */
		DRXJ_16TO8(0x0808),	/* FIRRCA2GAIN2 */
		DRXJ_16TO8(0x0808),	/* FIRRCA2GAIN3 */
		DRXJ_16TO8(0x1010),	/* FIRRCA2GAIN4 */
		DRXJ_16TO8(0x1010),	/* FIRRCA2GAIN5 */
		DRXJ_16TO8(0x1010),	/* FIRRCA2GAIN6 */
		DRXJ_16TO8(0x1010),	/* FIRRCA2GAIN7 */
		DRXJ_16TO8(0x1010),	/* FIRRCA2GAIN8 */
		DRXJ_16TO8(0x1010),	/* FIRRCA2GAIN9 */
		DRXJ_16TO8(0x0808),	/* FIRRCA2GAIN10 */
		DRXJ_16TO8(0x0808),	/* FIRRCA2GAIN11 */
		DRXJ_16TO8(0x0808),	/* FIRRCA2GAIN12 */
		DRXJ_16TO8(0x0303),	/* FIRDDM1GAIN1 */
		DRXJ_16TO8(0x0303),	/* FIRDDM1GAIN2 */
		DRXJ_16TO8(0x0303),	/* FIRDDM1GAIN3 */
		DRXJ_16TO8(0x0606),	/* FIRDDM1GAIN4 */
		DRXJ_16TO8(0x0606),	/* FIRDDM1GAIN5 */
		DRXJ_16TO8(0x0606),	/* FIRDDM1GAIN6 */
		DRXJ_16TO8(0x0606),	/* FIRDDM1GAIN7 */
		DRXJ_16TO8(0x0606),	/* FIRDDM1GAIN8 */
		DRXJ_16TO8(0x0606),	/* FIRDDM1GAIN9 */
		DRXJ_16TO8(0x0303),	/* FIRDDM1GAIN10 */
		DRXJ_16TO8(0x0303),	/* FIRDDM1GAIN11 */
		DRXJ_16TO8(0x0303),	/* FIRDDM1GAIN12 */
		DRXJ_16TO8(0x0303),	/* FIRDDM2GAIN1 */
		DRXJ_16TO8(0x0303),	/* FIRDDM2GAIN2 */
		DRXJ_16TO8(0x0303),	/* FIRDDM2GAIN3 */
		DRXJ_16TO8(0x0505),	/* FIRDDM2GAIN4 */
		DRXJ_16TO8(0x0505),	/* FIRDDM2GAIN5 */
		DRXJ_16TO8(0x0505),	/* FIRDDM2GAIN6 */
		DRXJ_16TO8(0x0505),	/* FIRDDM2GAIN7 */
		DRXJ_16TO8(0x0505),	/* FIRDDM2GAIN8 */
		DRXJ_16TO8(0x0505),	/* FIRDDM2GAIN9 */
		DRXJ_16TO8(0x0303),	/* FIRDDM2GAIN10 */
		DRXJ_16TO8(0x0303),	/* FIRDDM2GAIN11 */
		DRXJ_16TO8(0x0303),	/* FIRDDM2GAIN12 */
		DRXJ_16TO8(0x001f),	/* DFETRAINLKRATIO */
		DRXJ_16TO8(0x01ff),	/* DFERCA1TRAINLKRATIO */
		DRXJ_16TO8(0x01ff),	/* DFERCA1DATALKRATIO */
		DRXJ_16TO8(0x004f),	/* DFERCA2TRAINLKRATIO */
		DRXJ_16TO8(0x004f),	/* DFERCA2DATALKRATIO */
		DRXJ_16TO8(0x01ff),	/* DFEDDM1TRAINLKRATIO */
		DRXJ_16TO8(0x01ff),	/* DFEDDM1DATALKRATIO */
		DRXJ_16TO8(0x0352),	/* DFEDDM2TRAINLKRATIO */
		DRXJ_16TO8(0x0352),	/* DFEDDM2DATALKRATIO */
		DRXJ_16TO8(0x0000),	/* DFETRAINGAIN */
		DRXJ_16TO8(0x2020),	/* DFERCA1GAIN */
		DRXJ_16TO8(0x1010),	/* DFERCA2GAIN */
		DRXJ_16TO8(0x1818),	/* DFEDDM1GAIN */
		DRXJ_16TO8(0x1212)	/* DFEDDM2GAIN */
	};

	dev_addr = demod->my_i2c_dev_addr;
	rc = DRXJ_DAP.write_block_func(dev_addr, VSB_SYSCTRL_RAM0_FFETRAINLKRATIO1__A, sizeof(vsb_ffe_leak_gain_ram0), ((u8 *)vsb_ffe_leak_gain_ram0), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_block_func(dev_addr, VSB_SYSCTRL_RAM1_FIRRCA1GAIN9__A, sizeof(vsb_ffe_leak_gain_ram1), ((u8 *)vsb_ffe_leak_gain_ram1), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int set_vsb()
* \brief Set 8VSB demod.
* \param demod instance of demodulator.
* \return int.
*
*/
static int set_vsb(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	struct drx_common_attr *common_attr = NULL;
	struct drxjscu_cmd cmd_scu;
	struct drxj_data *ext_attr = NULL;
	u16 cmd_result = 0;
	u16 cmd_param = 0;
	const u8 vsb_taps_re[] = {
		DRXJ_16TO8(-2),	/* re0  */
		DRXJ_16TO8(4),	/* re1  */
		DRXJ_16TO8(1),	/* re2  */
		DRXJ_16TO8(-4),	/* re3  */
		DRXJ_16TO8(1),	/* re4  */
		DRXJ_16TO8(4),	/* re5  */
		DRXJ_16TO8(-3),	/* re6  */
		DRXJ_16TO8(-3),	/* re7  */
		DRXJ_16TO8(6),	/* re8  */
		DRXJ_16TO8(1),	/* re9  */
		DRXJ_16TO8(-9),	/* re10 */
		DRXJ_16TO8(3),	/* re11 */
		DRXJ_16TO8(12),	/* re12 */
		DRXJ_16TO8(-9),	/* re13 */
		DRXJ_16TO8(-15),	/* re14 */
		DRXJ_16TO8(17),	/* re15 */
		DRXJ_16TO8(19),	/* re16 */
		DRXJ_16TO8(-29),	/* re17 */
		DRXJ_16TO8(-22),	/* re18 */
		DRXJ_16TO8(45),	/* re19 */
		DRXJ_16TO8(25),	/* re20 */
		DRXJ_16TO8(-70),	/* re21 */
		DRXJ_16TO8(-28),	/* re22 */
		DRXJ_16TO8(111),	/* re23 */
		DRXJ_16TO8(30),	/* re24 */
		DRXJ_16TO8(-201),	/* re25 */
		DRXJ_16TO8(-31),	/* re26 */
		DRXJ_16TO8(629)	/* re27 */
	};

	dev_addr = demod->my_i2c_dev_addr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* stop all comm_exec */
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_COMM_EXEC__A, VSB_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* reset demodulator */
	cmd_scu.command = SCU_RAM_COMMAND_STANDARD_VSB
	    | SCU_RAM_COMMAND_CMD_DEMOD_RESET;
	cmd_scu.parameter_len = 0;
	cmd_scu.result_len = 1;
	cmd_scu.parameter = NULL;
	cmd_scu.result = &cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_DCF_BYPASS__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FS_ADJ_SEL__A, IQM_FS_ADJ_SEL_B_VSB, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_ADJ_SEL__A, IQM_RC_ADJ_SEL_B_VSB, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->iqm_rc_rate_ofs = 0x00AD0D79;
	rc = DRXJ_DAP.write_reg32func(dev_addr, IQM_RC_RATE_OFS_LO__A, ext_attr->iqm_rc_rate_ofs, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_CFAGC_GAINSHIFT__A, 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_CYGN1TRK__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_CROUT_ENA__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_STRETCH__A, 28, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_ACTIVE__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_SYMMETRIC__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_MIDTAP__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_OUT_ENA__A, IQM_CF_OUT_ENA_VSB__M, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_SCALE__A, 1393, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_SCALE_SH__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_POW_MEAS_LEN__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(vsb_taps_re), ((u8 *)vsb_taps_re), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(vsb_taps_re), ((u8 *)vsb_taps_re), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_BNTHRESH__A, 330, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* set higher threshold */
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_CLPLASTNUM__A, 90, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* burst detection on   */
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_SNRTH_RCA1__A, 0x0042, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* drop thresholds by 1 dB */
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_SNRTH_RCA2__A, 0x0053, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* drop thresholds by 2 dB */
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_EQCTRL__A, 0x1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* cma on               */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_GPIO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* GPIO               */

	/* Initialize the FEC Subsystem */
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_TOP_ANNEX__A, FEC_TOP_ANNEX_D, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	{
		u16 fec_oc_snc_mode = 0;
		rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_SNC_MODE__A, &fec_oc_snc_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* output data even when not locked */
		rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_SNC_MODE__A, fec_oc_snc_mode | FEC_OC_SNC_MODE_UNLOCK_ENABLE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* set clip */
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_CLP_LEN__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_CLP_TH__A, 470, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_SNS_LEN__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_SNRTH_PT__A, 0xD4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* no transparent, no A&C framing; parity is set in mpegoutput */
	{
		u16 fec_oc_reg_mode = 0;
		rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_MODE__A, &fec_oc_reg_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_MODE__A, fec_oc_reg_mode & (~(FEC_OC_MODE_TRANSPARENT__M | FEC_OC_MODE_CLEAR__M | FEC_OC_MODE_RETAIN_FRAMING__M)), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_DI_TIMEOUT_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* timeout counter for restarting */
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_DI_TIMEOUT_HI__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_RS_MODE__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* bypass disabled */
	/* initialize RS packet error measurement parameters */
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_RS_MEASUREMENT_PERIOD__A, FEC_RS_MEASUREMENT_PERIOD, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_RS_MEASUREMENT_PRESCALE__A, FEC_RS_MEASUREMENT_PRESCALE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* init measurement period of MER/SER */
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_MEASUREMENT_PERIOD__A, VSB_TOP_MEASUREMENT_PERIOD, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg32func(dev_addr, SCU_RAM_FEC_ACCUM_CW_CORRECTED_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_FEC_MEAS_COUNT__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_CKGN1TRK__A, 128, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* B-Input to ADC, PGA+filter in standby */
	if (!ext_attr->has_lna) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_AMUX__A, 0x02, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* turn on IQMAF. It has to be in front of setAgc**() */
	rc = set_iqm_af(demod, true);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = adc_synchronization(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = init_agc(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = set_agc_if(demod, &(ext_attr->vsb_if_agc_cfg), false);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = set_agc_rf(demod, &(ext_attr->vsb_rf_agc_cfg), false);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	{
		/* TODO fix this, store a struct drxj_cfg_afe_gain structure in struct drxj_data instead
		   of only the gain */
		struct drxj_cfg_afe_gain vsb_pga_cfg = { DRX_STANDARD_8VSB, 0 };

		vsb_pga_cfg.gain = ext_attr->vsb_pga_cfg;
		rc = ctrl_set_cfg_afe_gain(demod, &vsb_pga_cfg);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
	rc = ctrl_set_cfg_pre_saw(demod, &(ext_attr->vsb_pre_saw_cfg));
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Mpeg output has to be in front of FEC active */
	rc = set_mpegtei_handling(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = bit_reverse_mpeg_output(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = set_mpeg_start_width(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	{
		/* TODO: move to set_standard after hardware reset value problem is solved */
		/* Configure initial MPEG output */
		struct drx_cfg_mpeg_output cfg_mpeg_output;
		cfg_mpeg_output.enable_mpeg_output = true;
		cfg_mpeg_output.insert_rs_byte = common_attr->mpeg_cfg.insert_rs_byte;
		cfg_mpeg_output.enable_parallel =
		    common_attr->mpeg_cfg.enable_parallel;
		cfg_mpeg_output.invert_data = common_attr->mpeg_cfg.invert_data;
		cfg_mpeg_output.invert_err = common_attr->mpeg_cfg.invert_err;
		cfg_mpeg_output.invert_str = common_attr->mpeg_cfg.invert_str;
		cfg_mpeg_output.invert_val = common_attr->mpeg_cfg.invert_val;
		cfg_mpeg_output.invert_clk = common_attr->mpeg_cfg.invert_clk;
		cfg_mpeg_output.static_clk = common_attr->mpeg_cfg.static_clk;
		cfg_mpeg_output.bitrate = common_attr->mpeg_cfg.bitrate;
		rc = ctrl_set_cfg_mpeg_output(demod, &cfg_mpeg_output);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* TBD: what parameters should be set */
	cmd_param = 0x00;	/* Default mode AGC on, etc */
	cmd_scu.command = SCU_RAM_COMMAND_STANDARD_VSB
	    | SCU_RAM_COMMAND_CMD_DEMOD_SET_PARAM;
	cmd_scu.parameter_len = 1;
	cmd_scu.result_len = 1;
	cmd_scu.parameter = &cmd_param;
	cmd_scu.result = &cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_BEAGC_GAINSHIFT__A, 0x0004, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_SNRTH_PT__A, 0x00D2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_SYSSMTRNCTRL__A, VSB_TOP_SYSSMTRNCTRL__PRE | VSB_TOP_SYSSMTRNCTRL_NCOTIMEOUTCNTEN__M, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_BEDETCTRL__A, 0x142, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_LBAGCREFLVL__A, 640, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_CYGN1ACQ__A, 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_CYGN1TRK__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_CYGN2TRK__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* start demodulator */
	cmd_scu.command = SCU_RAM_COMMAND_STANDARD_VSB
	    | SCU_RAM_COMMAND_CMD_DEMOD_START;
	cmd_scu.parameter_len = 0;
	cmd_scu.result_len = 1;
	cmd_scu.parameter = NULL;
	cmd_scu.result = &cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_COMM_EXEC__A, VSB_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn static short get_vsb_post_rs_pck_err(struct i2c_device_addr *dev_addr, u16 *PckErrs)
* \brief Get the values of packet error in 8VSB mode
* \return Error code
*/
static int get_vsb_post_rs_pck_err(struct i2c_device_addr *dev_addr, u16 *pck_errs)
{
	int rc;
	u16 data = 0;
	u16 period = 0;
	u16 prescale = 0;
	u16 packet_errors_mant = 0;
	u16 packet_errors_exp = 0;

	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_RS_NR_FAILURES__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	packet_errors_mant = data & FEC_RS_NR_FAILURES_FIXED_MANT__M;
	packet_errors_exp = (data & FEC_RS_NR_FAILURES_EXP__M)
	    >> FEC_RS_NR_FAILURES_EXP__B;
	period = FEC_RS_MEASUREMENT_PERIOD;
	prescale = FEC_RS_MEASUREMENT_PRESCALE;
	/* packet error rate = (error packet number) per second */
	/* 77.3 us is time for per packet */
	if (period * prescale == 0) {
		pr_err("error: period and/or prescale is zero!\n");
		return -EIO;
	}
	*pck_errs =
	    (u16) frac_times1e6(packet_errors_mant * (1 << packet_errors_exp),
				 (period * prescale * 77));

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn static short GetVSBBer(struct i2c_device_addr *dev_addr, u32 *ber)
* \brief Get the values of ber in VSB mode
* \return Error code
*/
static int get_vs_bpost_viterbi_ber(struct i2c_device_addr *dev_addr, u32 *ber)
{
	int rc;
	u16 data = 0;
	u16 period = 0;
	u16 prescale = 0;
	u16 bit_errors_mant = 0;
	u16 bit_errors_exp = 0;

	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_RS_NR_BIT_ERRORS__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	period = FEC_RS_MEASUREMENT_PERIOD;
	prescale = FEC_RS_MEASUREMENT_PRESCALE;

	bit_errors_mant = data & FEC_RS_NR_BIT_ERRORS_FIXED_MANT__M;
	bit_errors_exp = (data & FEC_RS_NR_BIT_ERRORS_EXP__M)
	    >> FEC_RS_NR_BIT_ERRORS_EXP__B;

	if (((bit_errors_mant << bit_errors_exp) >> 3) > 68700)
		*ber = 26570;
	else {
		if (period * prescale == 0) {
			pr_err("error: period and/or prescale is zero!\n");
			return -EIO;
		}
		*ber =
		    frac_times1e6(bit_errors_mant <<
				 ((bit_errors_exp >
				   2) ? (bit_errors_exp - 3) : bit_errors_exp),
				 period * prescale * 207 *
				 ((bit_errors_exp > 2) ? 1 : 8));
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn static short get_vs_bpre_viterbi_ber(struct i2c_device_addr *dev_addr, u32 *ber)
* \brief Get the values of ber in VSB mode
* \return Error code
*/
static int get_vs_bpre_viterbi_ber(struct i2c_device_addr *dev_addr, u32 *ber)
{
	u16 data = 0;
	int rc;

	rc = DRXJ_DAP.read_reg16func(dev_addr, VSB_TOP_NR_SYM_ERRS__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	*ber =
	    frac_times1e6(data,
			 VSB_TOP_MEASUREMENT_PERIOD * SYMBOLS_PER_SEGMENT);

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn static short get_vsb_symb_err(struct i2c_device_addr *dev_addr, u32 *ber)
* \brief Get the values of ber in VSB mode
* \return Error code
*/
static int get_vsb_symb_err(struct i2c_device_addr *dev_addr, u32 *ser)
{
	int rc;
	u16 data = 0;
	u16 period = 0;
	u16 prescale = 0;
	u16 symb_errors_mant = 0;
	u16 symb_errors_exp = 0;

	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_RS_NR_SYMBOL_ERRORS__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	period = FEC_RS_MEASUREMENT_PERIOD;
	prescale = FEC_RS_MEASUREMENT_PRESCALE;

	symb_errors_mant = data & FEC_RS_NR_SYMBOL_ERRORS_FIXED_MANT__M;
	symb_errors_exp = (data & FEC_RS_NR_SYMBOL_ERRORS_EXP__M)
	    >> FEC_RS_NR_SYMBOL_ERRORS_EXP__B;

	if (period * prescale == 0) {
		pr_err("error: period and/or prescale is zero!\n");
		return -EIO;
	}
	*ser = (u32) frac_times1e6((symb_errors_mant << symb_errors_exp) * 1000,
				    (period * prescale * 77318));

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn static int get_vsbmer(struct i2c_device_addr *dev_addr, u16 *mer)
* \brief Get the values of MER
* \return Error code
*/
static int get_vsbmer(struct i2c_device_addr *dev_addr, u16 *mer)
{
	int rc;
	u16 data_hi = 0;

	rc = DRXJ_DAP.read_reg16func(dev_addr, VSB_TOP_ERR_ENERGY_H__A, &data_hi, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	*mer =
	    (u16) (log1_times100(21504) - log1_times100((data_hi << 6) / 52));

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \fn int ctrl_get_vsb_constel()
* \brief Retreive a VSB constellation point via I2C.
* \param demod Pointer to demodulator instance.
* \param complex_nr Pointer to the structure in which to store the
		   constellation point.
* \return int.
*/
static int
ctrl_get_vsb_constel(struct drx_demod_instance *demod, struct drx_complex *complex_nr)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
				       /**< device address                    */
	u16 vsb_top_comm_mb = 0;	       /**< VSB SL MB configuration           */
	u16 vsb_top_comm_mb_init = 0;    /**< VSB SL MB intial configuration    */
	u16 re = 0;		       /**< constellation Re part             */
	u32 data = 0;

	/* read device info */
	dev_addr = demod->my_i2c_dev_addr;

	/* TODO: */
	/* Monitor bus grabbing is an open external interface issue  */
	/* Needs to be checked when external interface PG is updated */

	/* Configure MB (Monitor bus) */
	rc = DRXJ_DAP.read_reg16func(dev_addr, VSB_TOP_COMM_MB__A, &vsb_top_comm_mb_init, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* set observe flag & MB mux */
	vsb_top_comm_mb = (vsb_top_comm_mb_init |
			VSB_TOP_COMM_MB_OBS_OBS_ON |
			VSB_TOP_COMM_MB_MUX_OBS_VSB_TCMEQ_2);
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_COMM_MB__A, vsb_top_comm_mb, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Enable MB grabber in the FEC OC */
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_OCR_MODE__A, FEC_OC_OCR_MODE_GRAB_ENABLE__M, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Disable MB grabber in the FEC OC */
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_OCR_MODE__A, 0x0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* read data */
	rc = DRXJ_DAP.read_reg32func(dev_addr, FEC_OC_OCR_GRAB_RD1__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	re = (u16) (((data >> 10) & 0x300) | ((data >> 2) & 0xff));
	if (re & 0x0200)
		re |= 0xfc00;
	complex_nr->re = re;
	complex_nr->im = 0;

	/* Restore MB (Monitor bus) */
	rc = DRXJ_DAP.write_reg16func(dev_addr, VSB_TOP_COMM_MB__A, vsb_top_comm_mb_init, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/*==                     END 8VSB DATAPATH FUNCTIONS                        ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                       QAM DATAPATH FUNCTIONS                           ==*/
/*============================================================================*/
/*============================================================================*/

/**
* \fn int power_down_qam ()
* \brief Powr down QAM related blocks.
* \param demod instance of demodulator.
* \param channel pointer to channel data.
* \return int.
*/
static int power_down_qam(struct drx_demod_instance *demod, bool primary)
{
	struct drxjscu_cmd cmd_scu = { /* command      */ 0,
		/* parameter_len */ 0,
		/* result_len    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	int rc;
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drx_cfg_mpeg_output cfg_mpeg_output;
	u16 cmd_result = 0;

	/*
	   STOP demodulator
	   resets IQM, QAM and FEC HW blocks
	 */
	/* stop all comm_exec */
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_COMM_EXEC__A, QAM_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	cmd_scu.command = SCU_RAM_COMMAND_STANDARD_QAM |
	    SCU_RAM_COMMAND_CMD_DEMOD_STOP;
	cmd_scu.parameter_len = 0;
	cmd_scu.result_len = 1;
	cmd_scu.parameter = NULL;
	cmd_scu.result = &cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (primary) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = set_iqm_af(demod, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	cfg_mpeg_output.enable_mpeg_output = false;
	rc = ctrl_set_cfg_mpeg_output(demod, &cfg_mpeg_output);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int set_qam_measurement ()
* \brief Setup of the QAM Measuremnt intervals for signal quality
* \param demod instance of demod.
* \param constellation current constellation.
* \return int.
*
*  NOTE:
*  Take into account that for certain settings the errorcounters can overflow.
*  The implementation does not check this.
*
*  TODO: overriding the ext_attr->fec_bits_desired by constellation dependent
*  constants to get a measurement period of approx. 1 sec. Remove fec_bits_desired
*  field ?
*
*/
#ifndef DRXJ_VSB_ONLY
static int
set_qam_measurement(struct drx_demod_instance *demod,
		    enum drx_modulation constellation, u32 symbol_rate)
{
	struct i2c_device_addr *dev_addr = NULL;	/* device address for I2C writes */
	struct drxj_data *ext_attr = NULL;	/* Global data container for DRXJ specif data */
	int rc;
	u32 fec_bits_desired = 0;	/* BER accounting period */
	u16 fec_rs_plen = 0;	/* defines RS BER measurement period */
	u16 fec_rs_prescale = 0;	/* ReedSolomon Measurement Prescale */
	u32 fec_rs_period = 0;	/* Value for corresponding I2C register */
	u32 fec_rs_bit_cnt = 0;	/* Actual precise amount of bits */
	u32 fec_oc_snc_fail_period = 0;	/* Value for corresponding I2C register */
	u32 qam_vd_period = 0;	/* Value for corresponding I2C register */
	u32 qam_vd_bit_cnt = 0;	/* Actual precise amount of bits */
	u16 fec_vd_plen = 0;	/* no of trellis symbols: VD SER measur period */
	u16 qam_vd_prescale = 0;	/* Viterbi Measurement Prescale */

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	fec_bits_desired = ext_attr->fec_bits_desired;
	fec_rs_prescale = ext_attr->fec_rs_prescale;

	switch (constellation) {
	case DRX_CONSTELLATION_QAM16:
		fec_bits_desired = 4 * symbol_rate;
		break;
	case DRX_CONSTELLATION_QAM32:
		fec_bits_desired = 5 * symbol_rate;
		break;
	case DRX_CONSTELLATION_QAM64:
		fec_bits_desired = 6 * symbol_rate;
		break;
	case DRX_CONSTELLATION_QAM128:
		fec_bits_desired = 7 * symbol_rate;
		break;
	case DRX_CONSTELLATION_QAM256:
		fec_bits_desired = 8 * symbol_rate;
		break;
	default:
		return -EINVAL;
	}

	/* Parameters for Reed-Solomon Decoder */
	/* fecrs_period = (int)ceil(FEC_BITS_DESIRED/(fecrs_prescale*plen)) */
	/* rs_bit_cnt   = fecrs_period*fecrs_prescale*plen                  */
	/*     result is within 32 bit arithmetic ->                        */
	/*     no need for mult or frac functions                           */

	/* TODO: use constant instead of calculation and remove the fec_rs_plen in ext_attr */
	switch (ext_attr->standard) {
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_C:
		fec_rs_plen = 204 * 8;
		break;
	case DRX_STANDARD_ITU_B:
		fec_rs_plen = 128 * 7;
		break;
	default:
		return -EINVAL;
	}

	ext_attr->fec_rs_plen = fec_rs_plen;	/* for getSigQual */
	fec_rs_bit_cnt = fec_rs_prescale * fec_rs_plen;	/* temp storage   */
	if (fec_rs_bit_cnt == 0) {
		pr_err("error: fec_rs_bit_cnt is zero!\n");
		return -EIO;
	}
	fec_rs_period = fec_bits_desired / fec_rs_bit_cnt + 1;	/* ceil */
	if (ext_attr->standard != DRX_STANDARD_ITU_B)
		fec_oc_snc_fail_period = fec_rs_period;

	/* limit to max 16 bit value (I2C register width) if needed */
	if (fec_rs_period > 0xFFFF)
		fec_rs_period = 0xFFFF;

	/* write corresponding registers */
	switch (ext_attr->standard) {
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_C:
		break;
	case DRX_STANDARD_ITU_B:
		switch (constellation) {
		case DRX_CONSTELLATION_QAM64:
			fec_rs_period = 31581;
			fec_oc_snc_fail_period = 17932;
			break;
		case DRX_CONSTELLATION_QAM256:
			fec_rs_period = 45446;
			fec_oc_snc_fail_period = 25805;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_SNC_FAIL_PERIOD__A, (u16)fec_oc_snc_fail_period, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_RS_MEASUREMENT_PERIOD__A, (u16)fec_rs_period, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_RS_MEASUREMENT_PRESCALE__A, fec_rs_prescale, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->fec_rs_period = (u16) fec_rs_period;
	ext_attr->fec_rs_prescale = fec_rs_prescale;
	rc = DRXJ_DAP.write_reg32func(dev_addr, SCU_RAM_FEC_ACCUM_CW_CORRECTED_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_FEC_MEAS_COUNT__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (ext_attr->standard == DRX_STANDARD_ITU_B) {
		/* Parameters for Viterbi Decoder */
		/* qamvd_period = (int)ceil(FEC_BITS_DESIRED/                      */
		/*                    (qamvd_prescale*plen*(qam_constellation+1))) */
		/* vd_bit_cnt   = qamvd_period*qamvd_prescale*plen                 */
		/*     result is within 32 bit arithmetic ->                       */
		/*     no need for mult or frac functions                          */

		/* a(8 bit) * b(8 bit) = 16 bit result => mult32 not needed */
		fec_vd_plen = ext_attr->fec_vd_plen;
		qam_vd_prescale = ext_attr->qam_vd_prescale;
		qam_vd_bit_cnt = qam_vd_prescale * fec_vd_plen;	/* temp storage */

		switch (constellation) {
		case DRX_CONSTELLATION_QAM64:
			/* a(16 bit) * b(4 bit) = 20 bit result => mult32 not needed */
			qam_vd_period =
			    qam_vd_bit_cnt * (QAM_TOP_CONSTELLATION_QAM64 + 1)
			    * (QAM_TOP_CONSTELLATION_QAM64 + 1);
			break;
		case DRX_CONSTELLATION_QAM256:
			/* a(16 bit) * b(5 bit) = 21 bit result => mult32 not needed */
			qam_vd_period =
			    qam_vd_bit_cnt * (QAM_TOP_CONSTELLATION_QAM256 + 1)
			    * (QAM_TOP_CONSTELLATION_QAM256 + 1);
			break;
		default:
			return -EINVAL;
		}
		if (qam_vd_period == 0) {
			pr_err("error: qam_vd_period is zero!\n");
			return -EIO;
		}
		qam_vd_period = fec_bits_desired / qam_vd_period;
		/* limit to max 16 bit value (I2C register width) if needed */
		if (qam_vd_period > 0xFFFF)
			qam_vd_period = 0xFFFF;

		/* a(16 bit) * b(16 bit) = 32 bit result => mult32 not needed */
		qam_vd_bit_cnt *= qam_vd_period;

		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_VD_MEASUREMENT_PERIOD__A, (u16)qam_vd_period, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_VD_MEASUREMENT_PRESCALE__A, qam_vd_prescale, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->qam_vd_period = (u16) qam_vd_period;
		ext_attr->qam_vd_prescale = qam_vd_prescale;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int set_qam16 ()
* \brief QAM16 specific setup
* \param demod instance of demod.
* \return int.
*/
static int set_qam16(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	const u8 qam_dq_qual_fun[] = {
		DRXJ_16TO8(2),	/* fun0  */
		DRXJ_16TO8(2),	/* fun1  */
		DRXJ_16TO8(2),	/* fun2  */
		DRXJ_16TO8(2),	/* fun3  */
		DRXJ_16TO8(3),	/* fun4  */
		DRXJ_16TO8(3),	/* fun5  */
	};
	const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(13517),	/* RAD0  */
		DRXJ_16TO8(13517),	/* RAD1  */
		DRXJ_16TO8(13517),	/* RAD2  */
		DRXJ_16TO8(13517),	/* RAD3  */
		DRXJ_16TO8(13517),	/* RAD4  */
		DRXJ_16TO8(13517),	/* RAD5  */
	};

	rc = DRXJ_DAP.write_block_func(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_block_func(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 140, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 120, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 230, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 95, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 105, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 56, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 220, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 25, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 6, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16)(-24), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16)(-65), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-127), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 10, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 240, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 40960, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int set_qam32 ()
* \brief QAM32 specific setup
* \param demod instance of demod.
* \return int.
*/
static int set_qam32(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	const u8 qam_dq_qual_fun[] = {
		DRXJ_16TO8(3),	/* fun0  */
		DRXJ_16TO8(3),	/* fun1  */
		DRXJ_16TO8(3),	/* fun2  */
		DRXJ_16TO8(3),	/* fun3  */
		DRXJ_16TO8(4),	/* fun4  */
		DRXJ_16TO8(4),	/* fun5  */
	};
	const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(6707),	/* RAD0  */
		DRXJ_16TO8(6707),	/* RAD1  */
		DRXJ_16TO8(6707),	/* RAD2  */
		DRXJ_16TO8(6707),	/* RAD3  */
		DRXJ_16TO8(6707),	/* RAD4  */
		DRXJ_16TO8(6707),	/* RAD5  */
	};

	rc = DRXJ_DAP.write_block_func(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_block_func(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 90, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 170, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 56, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 140, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, (u16)(-16), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16)(-26), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16)(-56), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-86), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 10, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 176, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 20480, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int set_qam64 ()
* \brief QAM64 specific setup
* \param demod instance of demod.
* \return int.
*/
static int set_qam64(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	const u8 qam_dq_qual_fun[] = {	/* this is hw reset value. no necessary to re-write */
		DRXJ_16TO8(4),	/* fun0  */
		DRXJ_16TO8(4),	/* fun1  */
		DRXJ_16TO8(4),	/* fun2  */
		DRXJ_16TO8(4),	/* fun3  */
		DRXJ_16TO8(6),	/* fun4  */
		DRXJ_16TO8(6),	/* fun5  */
	};
	const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(13336),	/* RAD0  */
		DRXJ_16TO8(12618),	/* RAD1  */
		DRXJ_16TO8(11988),	/* RAD2  */
		DRXJ_16TO8(13809),	/* RAD3  */
		DRXJ_16TO8(13809),	/* RAD4  */
		DRXJ_16TO8(15609),	/* RAD5  */
	};

	rc = DRXJ_DAP.write_block_func(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_block_func(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 105, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 60, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 195, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 84, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 141, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 7, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16)(-15), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16)(-45), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-80), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 30, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 48, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 160, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 43008, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int set_qam128 ()
* \brief QAM128 specific setup
* \param demod: instance of demod.
* \return int.
*/
static int set_qam128(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	const u8 qam_dq_qual_fun[] = {
		DRXJ_16TO8(6),	/* fun0  */
		DRXJ_16TO8(6),	/* fun1  */
		DRXJ_16TO8(6),	/* fun2  */
		DRXJ_16TO8(6),	/* fun3  */
		DRXJ_16TO8(9),	/* fun4  */
		DRXJ_16TO8(9),	/* fun5  */
	};
	const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(6164),	/* RAD0  */
		DRXJ_16TO8(6598),	/* RAD1  */
		DRXJ_16TO8(6394),	/* RAD2  */
		DRXJ_16TO8(6409),	/* RAD3  */
		DRXJ_16TO8(6656),	/* RAD4  */
		DRXJ_16TO8(7238),	/* RAD5  */
	};

	rc = DRXJ_DAP.write_block_func(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_block_func(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 60, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 140, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 65, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16)(-1), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-23), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 144, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 20992, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int set_qam256 ()
* \brief QAM256 specific setup
* \param demod: instance of demod.
* \return int.
*/
static int set_qam256(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	const u8 qam_dq_qual_fun[] = {
		DRXJ_16TO8(8),	/* fun0  */
		DRXJ_16TO8(8),	/* fun1  */
		DRXJ_16TO8(8),	/* fun2  */
		DRXJ_16TO8(8),	/* fun3  */
		DRXJ_16TO8(12),	/* fun4  */
		DRXJ_16TO8(12),	/* fun5  */
	};
	const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(12345),	/* RAD0  */
		DRXJ_16TO8(12345),	/* RAD1  */
		DRXJ_16TO8(13626),	/* RAD2  */
		DRXJ_16TO8(12931),	/* RAD3  */
		DRXJ_16TO8(14719),	/* RAD4  */
		DRXJ_16TO8(15356),	/* RAD5  */
	};

	rc = DRXJ_DAP.write_block_func(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_block_func(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 60, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 150, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 110, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 74, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 18, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 13, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, 7, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 25, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 48, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 43520, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
#define QAM_SET_OP_ALL 0x1
#define QAM_SET_OP_CONSTELLATION 0x2
#define QAM_SET_OP_SPECTRUM 0X4

/**
* \fn int set_qam ()
* \brief Set QAM demod.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \return int.
*/
static int
set_qam(struct drx_demod_instance *demod,
	struct drx_channel *channel, s32 tuner_freq_offset, u32 op)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	struct drx_common_attr *common_attr = NULL;
	int rc;
	u32 adc_frequency = 0;
	u32 iqm_rc_rate = 0;
	u16 cmd_result = 0;
	u16 lc_symbol_freq = 0;
	u16 iqm_rc_stretch = 0;
	u16 set_env_parameters = 0;
	u16 set_param_parameters[2] = { 0 };
	struct drxjscu_cmd cmd_scu = { /* command      */ 0,
		/* parameter_len */ 0,
		/* result_len    */ 0,
		/* parameter    */ NULL,
		/* result       */ NULL
	};
	const u8 qam_a_taps[] = {
		DRXJ_16TO8(-1),	/* re0  */
		DRXJ_16TO8(1),	/* re1  */
		DRXJ_16TO8(1),	/* re2  */
		DRXJ_16TO8(-1),	/* re3  */
		DRXJ_16TO8(-1),	/* re4  */
		DRXJ_16TO8(2),	/* re5  */
		DRXJ_16TO8(1),	/* re6  */
		DRXJ_16TO8(-2),	/* re7  */
		DRXJ_16TO8(0),	/* re8  */
		DRXJ_16TO8(3),	/* re9  */
		DRXJ_16TO8(-1),	/* re10 */
		DRXJ_16TO8(-3),	/* re11 */
		DRXJ_16TO8(4),	/* re12 */
		DRXJ_16TO8(1),	/* re13 */
		DRXJ_16TO8(-8),	/* re14 */
		DRXJ_16TO8(4),	/* re15 */
		DRXJ_16TO8(13),	/* re16 */
		DRXJ_16TO8(-13),	/* re17 */
		DRXJ_16TO8(-19),	/* re18 */
		DRXJ_16TO8(28),	/* re19 */
		DRXJ_16TO8(25),	/* re20 */
		DRXJ_16TO8(-53),	/* re21 */
		DRXJ_16TO8(-31),	/* re22 */
		DRXJ_16TO8(96),	/* re23 */
		DRXJ_16TO8(37),	/* re24 */
		DRXJ_16TO8(-190),	/* re25 */
		DRXJ_16TO8(-40),	/* re26 */
		DRXJ_16TO8(619)	/* re27 */
	};
	const u8 qam_b64_taps[] = {
		DRXJ_16TO8(0),	/* re0  */
		DRXJ_16TO8(-2),	/* re1  */
		DRXJ_16TO8(1),	/* re2  */
		DRXJ_16TO8(2),	/* re3  */
		DRXJ_16TO8(-2),	/* re4  */
		DRXJ_16TO8(0),	/* re5  */
		DRXJ_16TO8(4),	/* re6  */
		DRXJ_16TO8(-2),	/* re7  */
		DRXJ_16TO8(-4),	/* re8  */
		DRXJ_16TO8(4),	/* re9  */
		DRXJ_16TO8(3),	/* re10 */
		DRXJ_16TO8(-6),	/* re11 */
		DRXJ_16TO8(0),	/* re12 */
		DRXJ_16TO8(6),	/* re13 */
		DRXJ_16TO8(-5),	/* re14 */
		DRXJ_16TO8(-3),	/* re15 */
		DRXJ_16TO8(11),	/* re16 */
		DRXJ_16TO8(-4),	/* re17 */
		DRXJ_16TO8(-19),	/* re18 */
		DRXJ_16TO8(19),	/* re19 */
		DRXJ_16TO8(28),	/* re20 */
		DRXJ_16TO8(-45),	/* re21 */
		DRXJ_16TO8(-36),	/* re22 */
		DRXJ_16TO8(90),	/* re23 */
		DRXJ_16TO8(42),	/* re24 */
		DRXJ_16TO8(-185),	/* re25 */
		DRXJ_16TO8(-46),	/* re26 */
		DRXJ_16TO8(614)	/* re27 */
	};
	const u8 qam_b256_taps[] = {
		DRXJ_16TO8(-2),	/* re0  */
		DRXJ_16TO8(4),	/* re1  */
		DRXJ_16TO8(1),	/* re2  */
		DRXJ_16TO8(-4),	/* re3  */
		DRXJ_16TO8(0),	/* re4  */
		DRXJ_16TO8(4),	/* re5  */
		DRXJ_16TO8(-2),	/* re6  */
		DRXJ_16TO8(-4),	/* re7  */
		DRXJ_16TO8(5),	/* re8  */
		DRXJ_16TO8(2),	/* re9  */
		DRXJ_16TO8(-8),	/* re10 */
		DRXJ_16TO8(2),	/* re11 */
		DRXJ_16TO8(11),	/* re12 */
		DRXJ_16TO8(-8),	/* re13 */
		DRXJ_16TO8(-15),	/* re14 */
		DRXJ_16TO8(16),	/* re15 */
		DRXJ_16TO8(19),	/* re16 */
		DRXJ_16TO8(-27),	/* re17 */
		DRXJ_16TO8(-22),	/* re18 */
		DRXJ_16TO8(44),	/* re19 */
		DRXJ_16TO8(26),	/* re20 */
		DRXJ_16TO8(-69),	/* re21 */
		DRXJ_16TO8(-28),	/* re22 */
		DRXJ_16TO8(110),	/* re23 */
		DRXJ_16TO8(31),	/* re24 */
		DRXJ_16TO8(-201),	/* re25 */
		DRXJ_16TO8(-32),	/* re26 */
		DRXJ_16TO8(628)	/* re27 */
	};
	const u8 qam_c_taps[] = {
		DRXJ_16TO8(-3),	/* re0  */
		DRXJ_16TO8(3),	/* re1  */
		DRXJ_16TO8(2),	/* re2  */
		DRXJ_16TO8(-4),	/* re3  */
		DRXJ_16TO8(0),	/* re4  */
		DRXJ_16TO8(4),	/* re5  */
		DRXJ_16TO8(-1),	/* re6  */
		DRXJ_16TO8(-4),	/* re7  */
		DRXJ_16TO8(3),	/* re8  */
		DRXJ_16TO8(3),	/* re9  */
		DRXJ_16TO8(-5),	/* re10 */
		DRXJ_16TO8(0),	/* re11 */
		DRXJ_16TO8(9),	/* re12 */
		DRXJ_16TO8(-4),	/* re13 */
		DRXJ_16TO8(-12),	/* re14 */
		DRXJ_16TO8(10),	/* re15 */
		DRXJ_16TO8(16),	/* re16 */
		DRXJ_16TO8(-21),	/* re17 */
		DRXJ_16TO8(-20),	/* re18 */
		DRXJ_16TO8(37),	/* re19 */
		DRXJ_16TO8(25),	/* re20 */
		DRXJ_16TO8(-62),	/* re21 */
		DRXJ_16TO8(-28),	/* re22 */
		DRXJ_16TO8(105),	/* re23 */
		DRXJ_16TO8(31),	/* re24 */
		DRXJ_16TO8(-197),	/* re25 */
		DRXJ_16TO8(-33),	/* re26 */
		DRXJ_16TO8(626)	/* re27 */
	};

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {
		if (ext_attr->standard == DRX_STANDARD_ITU_B) {
			switch (channel->constellation) {
			case DRX_CONSTELLATION_QAM256:
				iqm_rc_rate = 0x00AE3562;
				lc_symbol_freq =
				    QAM_LC_SYMBOL_FREQ_FREQ_QAM_B_256;
				channel->symbolrate = 5360537;
				iqm_rc_stretch = IQM_RC_STRETCH_QAM_B_256;
				break;
			case DRX_CONSTELLATION_QAM64:
				iqm_rc_rate = 0x00C05A0E;
				lc_symbol_freq = 409;
				channel->symbolrate = 5056941;
				iqm_rc_stretch = IQM_RC_STRETCH_QAM_B_64;
				break;
			default:
				return -EINVAL;
			}
		} else {
			adc_frequency = (common_attr->sys_clock_freq * 1000) / 3;
			if (channel->symbolrate == 0) {
				pr_err("error: channel symbolrate is zero!\n");
				return -EIO;
			}
			iqm_rc_rate =
			    (adc_frequency / channel->symbolrate) * (1 << 21) +
			    (frac28
			     ((adc_frequency % channel->symbolrate),
			      channel->symbolrate) >> 7) - (1 << 23);
			lc_symbol_freq =
			    (u16) (frac28
				     (channel->symbolrate +
				      (adc_frequency >> 13),
				      adc_frequency) >> 16);
			if (lc_symbol_freq > 511)
				lc_symbol_freq = 511;

			iqm_rc_stretch = 21;
		}

		if (ext_attr->standard == DRX_STANDARD_ITU_A) {
			set_env_parameters = QAM_TOP_ANNEX_A;	/* annex             */
			set_param_parameters[0] = channel->constellation;	/* constellation     */
			set_param_parameters[1] = DRX_INTERLEAVEMODE_I12_J17;	/* interleave mode   */
		} else if (ext_attr->standard == DRX_STANDARD_ITU_B) {
			set_env_parameters = QAM_TOP_ANNEX_B;	/* annex             */
			set_param_parameters[0] = channel->constellation;	/* constellation     */
			set_param_parameters[1] = channel->interleavemode;	/* interleave mode   */
		} else if (ext_attr->standard == DRX_STANDARD_ITU_C) {
			set_env_parameters = QAM_TOP_ANNEX_C;	/* annex             */
			set_param_parameters[0] = channel->constellation;	/* constellation     */
			set_param_parameters[1] = DRX_INTERLEAVEMODE_I12_J17;	/* interleave mode   */
		} else {
			return -EINVAL;
		}
	}

	if (op & QAM_SET_OP_ALL) {
		/*
		   STEP 1: reset demodulator
		   resets IQM, QAM and FEC HW blocks
		   resets SCU variables
		 */
		/* stop all comm_exec */
		rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_COMM_EXEC__A, QAM_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		cmd_scu.command = SCU_RAM_COMMAND_STANDARD_QAM |
		    SCU_RAM_COMMAND_CMD_DEMOD_RESET;
		cmd_scu.parameter_len = 0;
		cmd_scu.result_len = 1;
		cmd_scu.parameter = NULL;
		cmd_scu.result = &cmd_result;
		rc = scu_command(dev_addr, &cmd_scu);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {
		/*
		   STEP 2: configure demodulator
		   -set env
		   -set params (resets IQM,QAM,FEC HW; initializes some SCU variables )
		 */
		cmd_scu.command = SCU_RAM_COMMAND_STANDARD_QAM |
		    SCU_RAM_COMMAND_CMD_DEMOD_SET_ENV;
		cmd_scu.parameter_len = 1;
		cmd_scu.result_len = 1;
		cmd_scu.parameter = &set_env_parameters;
		cmd_scu.result = &cmd_result;
		rc = scu_command(dev_addr, &cmd_scu);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		cmd_scu.command = SCU_RAM_COMMAND_STANDARD_QAM |
		    SCU_RAM_COMMAND_CMD_DEMOD_SET_PARAM;
		cmd_scu.parameter_len = 2;
		cmd_scu.result_len = 1;
		cmd_scu.parameter = set_param_parameters;
		cmd_scu.result = &cmd_result;
		rc = scu_command(dev_addr, &cmd_scu);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* set symbol rate */
		rc = DRXJ_DAP.write_reg32func(dev_addr, IQM_RC_RATE_OFS_LO__A, iqm_rc_rate, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->iqm_rc_rate_ofs = iqm_rc_rate;
		rc = set_qam_measurement(demod, channel->constellation, channel->symbolrate);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
	/* STEP 3: enable the system in a mode where the ADC provides valid signal
	   setup constellation independent registers */
	/* from qam_cmd.py script (qam_driver_b) */
	/* TODO: remove re-writes of HW reset values */
	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_SPECTRUM)) {
		rc = set_frequency(demod, channel, tuner_freq_offset);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {

		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_SYMBOL_FREQ__A, lc_symbol_freq, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_STRETCH__A, iqm_rc_stretch, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	if (op & QAM_SET_OP_ALL) {
		if (!ext_attr->has_lna) {
			rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_AMUX__A, 0x02, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_SYMMETRIC__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_MIDTAP__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_OUT_ENA__A, IQM_CF_OUT_ENA_QAM__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_WR_RSV_0__A, 0x5f, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* scu temporary shut down agc */

		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_SYNC_SEL__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_CLP_LEN__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_CLP_TH__A, 448, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_SNS_LEN__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_PDREF__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_STDBY__A, 0x10, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_PGA_GAIN__A, 11, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_POW_MEAS_LEN__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_SCALE_SH__A, IQM_CF_SCALE_SH__PRE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/*! reset default val ! */

		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_TIMEOUT__A, QAM_SY_TIMEOUT__PRE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/*! reset default val ! */
		if (ext_attr->standard == DRX_STANDARD_ITU_B) {
			rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_SYNC_LWM__A, QAM_SY_SYNC_LWM__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/*! reset default val ! */
			rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_SYNC_AWM__A, QAM_SY_SYNC_AWM__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/*! reset default val ! */
			rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_SYNC_HWM__A, QAM_SY_SYNC_HWM__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/*! reset default val ! */
		} else {
			switch (channel->constellation) {
			case DRX_CONSTELLATION_QAM16:
			case DRX_CONSTELLATION_QAM64:
			case DRX_CONSTELLATION_QAM256:
				rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_SYNC_LWM__A, 0x03, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_SYNC_AWM__A, 0x04, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_SYNC_HWM__A, QAM_SY_SYNC_HWM__PRE, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}	/*! reset default val ! */
				break;
			case DRX_CONSTELLATION_QAM32:
			case DRX_CONSTELLATION_QAM128:
				rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_SYNC_LWM__A, 0x03, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_SYNC_AWM__A, 0x05, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SY_SYNC_HWM__A, 0x06, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				break;
			default:
				return -EIO;
			}	/* switch */
		}

		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_MODE__A, QAM_LC_MODE__PRE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/*! reset default val ! */
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_RATE_LIMIT__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_LPF_FACTORP__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_LPF_FACTORI__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_MODE__A, 7, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB0__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB1__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB2__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB3__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB4__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB5__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB6__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB8__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB9__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB10__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB12__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB15__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB16__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB20__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_QUAL_TAB25__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FS_ADJ_SEL__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_ADJ_SEL__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_ADJ_SEL__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_POW_MEAS_LEN__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_GPIO__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* No more resets of the IQM, current standard correctly set =>
		   now AGCs can be configured. */
		/* turn on IQMAF. It has to be in front of setAgc**() */
		rc = set_iqm_af(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = adc_synchronization(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = init_agc(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = set_agc_if(demod, &(ext_attr->qam_if_agc_cfg), false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = set_agc_rf(demod, &(ext_attr->qam_rf_agc_cfg), false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		{
			/* TODO fix this, store a struct drxj_cfg_afe_gain structure in struct drxj_data instead
			   of only the gain */
			struct drxj_cfg_afe_gain qam_pga_cfg = { DRX_STANDARD_ITU_B, 0 };

			qam_pga_cfg.gain = ext_attr->qam_pga_cfg;
			rc = ctrl_set_cfg_afe_gain(demod, &qam_pga_cfg);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		rc = ctrl_set_cfg_pre_saw(demod, &(ext_attr->qam_pre_saw_cfg));
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {
		if (ext_attr->standard == DRX_STANDARD_ITU_A) {
			rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(qam_a_taps), ((u8 *)qam_a_taps), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(qam_a_taps), ((u8 *)qam_a_taps), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} else if (ext_attr->standard == DRX_STANDARD_ITU_B) {
			switch (channel->constellation) {
			case DRX_CONSTELLATION_QAM64:
				rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(qam_b64_taps), ((u8 *)qam_b64_taps), 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(qam_b64_taps), ((u8 *)qam_b64_taps), 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				break;
			case DRX_CONSTELLATION_QAM256:
				rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(qam_b256_taps), ((u8 *)qam_b256_taps), 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(qam_b256_taps), ((u8 *)qam_b256_taps), 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				break;
			default:
				return -EIO;
			}
		} else if (ext_attr->standard == DRX_STANDARD_ITU_C) {
			rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(qam_c_taps), ((u8 *)qam_c_taps), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(qam_c_taps), ((u8 *)qam_c_taps), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}

		/* SETP 4: constellation specific setup */
		switch (channel->constellation) {
		case DRX_CONSTELLATION_QAM16:
			rc = set_qam16(demod);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_CONSTELLATION_QAM32:
			rc = set_qam32(demod);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_CONSTELLATION_QAM64:
			rc = set_qam64(demod);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_CONSTELLATION_QAM128:
			rc = set_qam128(demod);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_CONSTELLATION_QAM256:
			rc = set_qam256(demod);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		default:
			return -EIO;
		}		/* switch */
	}

	if ((op & QAM_SET_OP_ALL)) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_SCALE_SH__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* Mpeg output has to be in front of FEC active */
		rc = set_mpegtei_handling(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = bit_reverse_mpeg_output(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = set_mpeg_start_width(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		{
			/* TODO: move to set_standard after hardware reset value problem is solved */
			/* Configure initial MPEG output */
			struct drx_cfg_mpeg_output cfg_mpeg_output;

			cfg_mpeg_output.enable_mpeg_output = true;
			cfg_mpeg_output.insert_rs_byte =
			    common_attr->mpeg_cfg.insert_rs_byte;
			cfg_mpeg_output.enable_parallel =
			    common_attr->mpeg_cfg.enable_parallel;
			cfg_mpeg_output.invert_data =
			    common_attr->mpeg_cfg.invert_data;
			cfg_mpeg_output.invert_err = common_attr->mpeg_cfg.invert_err;
			cfg_mpeg_output.invert_str = common_attr->mpeg_cfg.invert_str;
			cfg_mpeg_output.invert_val = common_attr->mpeg_cfg.invert_val;
			cfg_mpeg_output.invert_clk = common_attr->mpeg_cfg.invert_clk;
			cfg_mpeg_output.static_clk = common_attr->mpeg_cfg.static_clk;
			cfg_mpeg_output.bitrate = common_attr->mpeg_cfg.bitrate;
			rc = ctrl_set_cfg_mpeg_output(demod, &cfg_mpeg_output);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
	}

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {

		/* STEP 5: start QAM demodulator (starts FEC, QAM and IQM HW) */
		cmd_scu.command = SCU_RAM_COMMAND_STANDARD_QAM |
		    SCU_RAM_COMMAND_CMD_DEMOD_START;
		cmd_scu.parameter_len = 0;
		cmd_scu.result_len = 1;
		cmd_scu.parameter = NULL;
		cmd_scu.result = &cmd_result;
		rc = scu_command(dev_addr, &cmd_scu);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_COMM_EXEC__A, QAM_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
static int
ctrl_get_qam_sig_quality(struct drx_demod_instance *demod, struct drx_sig_quality *sig_quality);
static int qam_flip_spec(struct drx_demod_instance *demod, struct drx_channel *channel)
{
	int rc;
	u32 iqm_fs_rate_ofs = 0;
	u32 iqm_fs_rate_lo = 0;
	u16 qam_ctl_ena = 0;
	u16 data = 0;
	u16 equ_mode = 0;
	u16 fsm_state = 0;
	int i = 0;
	int ofsofs = 0;
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* Silence the controlling of lc, equ, and the acquisition state machine */
	rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_QAM_CTL_ENA__A, &qam_ctl_ena, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_CTL_ENA__A, qam_ctl_ena & ~(SCU_RAM_QAM_CTL_ENA_ACQ__M | SCU_RAM_QAM_CTL_ENA_EQU__M | SCU_RAM_QAM_CTL_ENA_LC__M), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* freeze the frequency control loop */
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_CF__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_CF1__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_atomic_read_reg32(dev_addr, IQM_FS_RATE_OFS_LO__A, &iqm_fs_rate_ofs, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_atomic_read_reg32(dev_addr, IQM_FS_RATE_LO__A, &iqm_fs_rate_lo, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ofsofs = iqm_fs_rate_lo - iqm_fs_rate_ofs;
	iqm_fs_rate_ofs = ~iqm_fs_rate_ofs + 1;
	iqm_fs_rate_ofs -= 2 * ofsofs;

	/* freeze dq/fq updating */
	rc = DRXJ_DAP.read_reg16func(dev_addr, QAM_DQ_MODE__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	data = (data & 0xfff9);
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_DQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_FQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* lc_cp / _ci / _ca */
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_CI__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_LC_EP__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_FQ_LA_FACTOR__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* flip the spec */
	rc = DRXJ_DAP.write_reg32func(dev_addr, IQM_FS_RATE_OFS_LO__A, iqm_fs_rate_ofs, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->iqm_fs_rate_ofs = iqm_fs_rate_ofs;
	ext_attr->pos_image = (ext_attr->pos_image) ? false : true;

	/* freeze dq/fq updating */
	rc = DRXJ_DAP.read_reg16func(dev_addr, QAM_DQ_MODE__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	equ_mode = data;
	data = (data & 0xfff9);
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_DQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_FQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	for (i = 0; i < 28; i++) {
		rc = DRXJ_DAP.read_reg16func(dev_addr, QAM_DQ_TAP_IM_EL0__A + (2 * i), &data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_DQ_TAP_IM_EL0__A + (2 * i), -data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	for (i = 0; i < 24; i++) {
		rc = DRXJ_DAP.read_reg16func(dev_addr, QAM_FQ_TAP_IM_EL0__A + (2 * i), &data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_FQ_TAP_IM_EL0__A + (2 * i), -data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	data = equ_mode;
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_DQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_FQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_FSM_STATE_TGT__A, 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	i = 0;
	while ((fsm_state != 4) && (i++ < 100)) {
		rc = DRXJ_DAP.read_reg16func(dev_addr, SCU_RAM_QAM_FSM_STATE__A, &fsm_state, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_QAM_CTL_ENA__A, (qam_ctl_ena | 0x0016), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;

}

#define  NO_LOCK        0x0
#define  DEMOD_LOCKED   0x1
#define  SYNC_FLIPPED   0x2
#define  SPEC_MIRRORED  0x4
/**
* \fn int qam64auto ()
* \brief auto do sync pattern switching and mirroring.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \param tuner_freq_offset: tuner frequency offset.
* \param lock_status: pointer to lock status.
* \return int.
*/
static int
qam64auto(struct drx_demod_instance *demod,
	  struct drx_channel *channel,
	  s32 tuner_freq_offset, enum drx_lock_status *lock_status)
{
	struct drx_sig_quality sig_quality;
	struct drxj_data *ext_attr = NULL;
	int rc;
	u32 state = NO_LOCK;
	u32 start_time = 0;
	u32 d_locked_time = 0;
	u32 timeout_ofs = 0;
	u16 data = 0;

	/* external attributes for storing aquired channel constellation */
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	*lock_status = DRX_NOT_LOCKED;
	start_time = drxbsp_hst_clock();
	state = NO_LOCK;
	do {
		rc = ctrl_lock_status(demod, lock_status);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		switch (state) {
		case NO_LOCK:
			if (*lock_status == DRXJ_DEMOD_LOCK) {
				rc = ctrl_get_qam_sig_quality(demod, &sig_quality);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				if (sig_quality.MER > 208) {
					state = DEMOD_LOCKED;
					/* some delay to see if fec_lock possible TODO find the right value */
					timeout_ofs += DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME;	/* see something, waiting longer */
					d_locked_time = drxbsp_hst_clock();
				}
			}
			break;
		case DEMOD_LOCKED:
			if ((*lock_status == DRXJ_DEMOD_LOCK) &&	/* still demod_lock in 150ms */
			    ((drxbsp_hst_clock() - d_locked_time) >
			     DRXJ_QAM_FEC_LOCK_WAITTIME)) {
				rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, &data, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, data | 0x1, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				state = SYNC_FLIPPED;
				drxbsp_hst_sleep(10);
			}
			break;
		case SYNC_FLIPPED:
			if (*lock_status == DRXJ_DEMOD_LOCK) {
				if (channel->mirror == DRX_MIRROR_AUTO) {
					/* flip sync pattern back */
					rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, &data, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, data & 0xFFFE, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					/* flip spectrum */
					ext_attr->mirror = DRX_MIRROR_YES;
					rc = qam_flip_spec(demod, channel);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					state = SPEC_MIRRORED;
					/* reset timer TODO: still need 500ms? */
					start_time = d_locked_time =
					    drxbsp_hst_clock();
					timeout_ofs = 0;
				} else {	/* no need to wait lock */

					start_time =
					    drxbsp_hst_clock() -
					    DRXJ_QAM_MAX_WAITTIME - timeout_ofs;
				}
			}
			break;
		case SPEC_MIRRORED:
			if ((*lock_status == DRXJ_DEMOD_LOCK) &&	/* still demod_lock in 150ms */
			    ((drxbsp_hst_clock() - d_locked_time) >
			     DRXJ_QAM_FEC_LOCK_WAITTIME)) {
				rc = ctrl_get_qam_sig_quality(demod, &sig_quality);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				if (sig_quality.MER > 208) {
					rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, &data, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, data | 0x1, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					/* no need to wait lock */
					start_time =
					    drxbsp_hst_clock() -
					    DRXJ_QAM_MAX_WAITTIME - timeout_ofs;
				}
			}
			break;
		default:
			break;
		}
		drxbsp_hst_sleep(10);
	} while
	    ((*lock_status != DRX_LOCKED) &&
	     (*lock_status != DRX_NEVER_LOCK) &&
	     ((drxbsp_hst_clock() - start_time) <
	      (DRXJ_QAM_MAX_WAITTIME + timeout_ofs))
	    );
	/* Returning control to apllication ... */

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int qam256auto ()
* \brief auto do sync pattern switching and mirroring.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \param tuner_freq_offset: tuner frequency offset.
* \param lock_status: pointer to lock status.
* \return int.
*/
static int
qam256auto(struct drx_demod_instance *demod,
	   struct drx_channel *channel,
	   s32 tuner_freq_offset, enum drx_lock_status *lock_status)
{
	struct drx_sig_quality sig_quality;
	struct drxj_data *ext_attr = NULL;
	int rc;
	u32 state = NO_LOCK;
	u32 start_time = 0;
	u32 d_locked_time = 0;
	u32 timeout_ofs = DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME;

	/* external attributes for storing aquired channel constellation */
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	*lock_status = DRX_NOT_LOCKED;
	start_time = drxbsp_hst_clock();
	state = NO_LOCK;
	do {
		rc = ctrl_lock_status(demod, lock_status);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		switch (state) {
		case NO_LOCK:
			if (*lock_status == DRXJ_DEMOD_LOCK) {
				rc = ctrl_get_qam_sig_quality(demod, &sig_quality);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				if (sig_quality.MER > 268) {
					state = DEMOD_LOCKED;
					timeout_ofs += DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME;	/* see something, wait longer */
					d_locked_time = drxbsp_hst_clock();
				}
			}
			break;
		case DEMOD_LOCKED:
			if (*lock_status == DRXJ_DEMOD_LOCK) {
				if ((channel->mirror == DRX_MIRROR_AUTO) &&
				    ((drxbsp_hst_clock() - d_locked_time) >
				     DRXJ_QAM_FEC_LOCK_WAITTIME)) {
					ext_attr->mirror = DRX_MIRROR_YES;
					rc = qam_flip_spec(demod, channel);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					state = SPEC_MIRRORED;
					/* reset timer TODO: still need 300ms? */
					start_time = drxbsp_hst_clock();
					timeout_ofs = -DRXJ_QAM_MAX_WAITTIME / 2;
				}
			}
			break;
		case SPEC_MIRRORED:
			break;
		default:
			break;
		}
		drxbsp_hst_sleep(10);
	} while
	    ((*lock_status < DRX_LOCKED) &&
	     (*lock_status != DRX_NEVER_LOCK) &&
	     ((drxbsp_hst_clock() - start_time) <
	      (DRXJ_QAM_MAX_WAITTIME + timeout_ofs)));

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int set_qam_channel ()
* \brief Set QAM channel according to the requested constellation.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \return int.
*/
static int
set_qam_channel(struct drx_demod_instance *demod,
	       struct drx_channel *channel, s32 tuner_freq_offset)
{
	struct drxj_data *ext_attr = NULL;
	int rc;
	enum drx_lock_status lock_status = DRX_NOT_LOCKED;
	bool auto_flag = false;

	/* external attributes for storing aquired channel constellation */
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* set QAM channel constellation */
	switch (channel->constellation) {
	case DRX_CONSTELLATION_QAM16:
	case DRX_CONSTELLATION_QAM32:
	case DRX_CONSTELLATION_QAM64:
	case DRX_CONSTELLATION_QAM128:
	case DRX_CONSTELLATION_QAM256:
		ext_attr->constellation = channel->constellation;
		if (channel->mirror == DRX_MIRROR_AUTO)
			ext_attr->mirror = DRX_MIRROR_NO;
		else
			ext_attr->mirror = channel->mirror;
		rc = set_qam(demod, channel, tuner_freq_offset, QAM_SET_OP_ALL);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		if ((ext_attr->standard == DRX_STANDARD_ITU_B) &&
		    (channel->constellation == DRX_CONSTELLATION_QAM64)) {
			rc = qam64auto(demod, channel, tuner_freq_offset, &lock_status);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}

		if ((ext_attr->standard == DRX_STANDARD_ITU_B) &&
		    (channel->mirror == DRX_MIRROR_AUTO) &&
		    (channel->constellation == DRX_CONSTELLATION_QAM256)) {
			rc = qam256auto(demod, channel, tuner_freq_offset, &lock_status);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		break;
	case DRX_CONSTELLATION_AUTO:	/* for channel scan */
		if (ext_attr->standard == DRX_STANDARD_ITU_B) {
			auto_flag = true;
			/* try to lock default QAM constellation: QAM64 */
			channel->constellation = DRX_CONSTELLATION_QAM256;
			ext_attr->constellation = DRX_CONSTELLATION_QAM256;
			if (channel->mirror == DRX_MIRROR_AUTO)
				ext_attr->mirror = DRX_MIRROR_NO;
			else
				ext_attr->mirror = channel->mirror;
			rc = set_qam(demod, channel, tuner_freq_offset, QAM_SET_OP_ALL);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = qam256auto(demod, channel, tuner_freq_offset, &lock_status);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			if (lock_status < DRX_LOCKED) {
				/* QAM254 not locked -> try to lock QAM64 constellation */
				channel->constellation =
				    DRX_CONSTELLATION_QAM64;
				ext_attr->constellation =
				    DRX_CONSTELLATION_QAM64;
				if (channel->mirror == DRX_MIRROR_AUTO)
					ext_attr->mirror = DRX_MIRROR_NO;
				else
					ext_attr->mirror = channel->mirror;
				{
					u16 qam_ctl_ena = 0;
					rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SCU_RAM_QAM_CTL_ENA__A, &qam_ctl_ena, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SCU_RAM_QAM_CTL_ENA__A, qam_ctl_ena & ~SCU_RAM_QAM_CTL_ENA_ACQ__M, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SCU_RAM_QAM_FSM_STATE_TGT__A, 0x2, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}	/* force to rate hunting */

					rc = set_qam(demod, channel, tuner_freq_offset, QAM_SET_OP_CONSTELLATION);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SCU_RAM_QAM_CTL_ENA__A, qam_ctl_ena, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
				}
				rc = qam64auto(demod, channel, tuner_freq_offset, &lock_status);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
			}
			channel->constellation = DRX_CONSTELLATION_AUTO;
		} else if (ext_attr->standard == DRX_STANDARD_ITU_C) {
			channel->constellation = DRX_CONSTELLATION_QAM64;
			ext_attr->constellation = DRX_CONSTELLATION_QAM64;
			auto_flag = true;

			if (channel->mirror == DRX_MIRROR_AUTO)
				ext_attr->mirror = DRX_MIRROR_NO;
			else
				ext_attr->mirror = channel->mirror;
			{
				u16 qam_ctl_ena = 0;
				rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SCU_RAM_QAM_CTL_ENA__A, &qam_ctl_ena, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SCU_RAM_QAM_CTL_ENA__A, qam_ctl_ena & ~SCU_RAM_QAM_CTL_ENA_ACQ__M, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SCU_RAM_QAM_FSM_STATE_TGT__A, 0x2, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}	/* force to rate hunting */

				rc = set_qam(demod, channel, tuner_freq_offset, QAM_SET_OP_CONSTELLATION);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = DRXJ_DAP.write_reg16func(demod->my_i2c_dev_addr, SCU_RAM_QAM_CTL_ENA__A, qam_ctl_ena, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
			}
			rc = qam64auto(demod, channel, tuner_freq_offset, &lock_status);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			channel->constellation = DRX_CONSTELLATION_AUTO;
		} else {
			channel->constellation = DRX_CONSTELLATION_AUTO;
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
rw_error:
	/* restore starting value */
	if (auto_flag)
		channel->constellation = DRX_CONSTELLATION_AUTO;
	return -EIO;
}

/*============================================================================*/

/**
* \fn static short get_qamrs_err_count(struct i2c_device_addr *dev_addr)
* \brief Get RS error count in QAM mode (used for post RS BER calculation)
* \return Error code
*
* precondition: measurement period & measurement prescale must be set
*
*/
static int
get_qamrs_err_count(struct i2c_device_addr *dev_addr, struct drxjrs_errors *rs_errors)
{
	int rc;
	u16 nr_bit_errors = 0,
	    nr_symbol_errors = 0,
	    nr_packet_errors = 0, nr_failures = 0, nr_snc_par_fail_count = 0;

	/* check arguments */
	if (dev_addr == NULL)
		return -EINVAL;

	/* all reported errors are received in the  */
	/* most recently finished measurment period */
	/*   no of pre RS bit errors */
	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_RS_NR_BIT_ERRORS__A, &nr_bit_errors, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/*   no of symbol errors      */
	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_RS_NR_SYMBOL_ERRORS__A, &nr_symbol_errors, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/*   no of packet errors      */
	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_RS_NR_PACKET_ERRORS__A, &nr_packet_errors, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/*   no of failures to decode */
	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_RS_NR_FAILURES__A, &nr_failures, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/*   no of post RS bit erros  */
	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_SNC_FAIL_COUNT__A, &nr_snc_par_fail_count, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* TODO: NOTE */
	/* These register values are fetched in non-atomic fashion           */
	/* It is possible that the read values contain unrelated information */

	rs_errors->nr_bit_errors = nr_bit_errors & FEC_RS_NR_BIT_ERRORS__M;
	rs_errors->nr_symbol_errors = nr_symbol_errors & FEC_RS_NR_SYMBOL_ERRORS__M;
	rs_errors->nr_packet_errors = nr_packet_errors & FEC_RS_NR_PACKET_ERRORS__M;
	rs_errors->nr_failures = nr_failures & FEC_RS_NR_FAILURES__M;
	rs_errors->nr_snc_par_fail_count =
	    nr_snc_par_fail_count & FEC_OC_SNC_FAIL_COUNT__M;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_get_qam_sig_quality()
* \brief Retreive QAM signal quality from device.
* \param devmod Pointer to demodulator instance.
* \param sig_quality Pointer to signal quality data.
* \return int.
* \retval 0 sig_quality contains valid data.
* \retval -EINVAL sig_quality is NULL.
* \retval -EIO Erroneous data, sig_quality contains invalid data.

*  Pre-condition: Device must be started and in lock.
*/
static int
ctrl_get_qam_sig_quality(struct drx_demod_instance *demod, struct drx_sig_quality *sig_quality)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;
	enum drx_modulation constellation = DRX_CONSTELLATION_UNKNOWN;
	struct drxjrs_errors measuredrs_errors = { 0, 0, 0, 0, 0 };

	u32 pre_bit_err_rs = 0;	/* pre RedSolomon Bit Error Rate */
	u32 post_bit_err_rs = 0;	/* post RedSolomon Bit Error Rate */
	u32 pkt_errs = 0;	/* no of packet errors in RS */
	u16 qam_sl_err_power = 0;	/* accumulated error between raw and sliced symbols */
	u16 qsym_err_vd = 0;	/* quadrature symbol errors in QAM_VD */
	u16 fec_oc_period = 0;	/* SNC sync failure measurement period */
	u16 fec_rs_prescale = 0;	/* ReedSolomon Measurement Prescale */
	u16 fec_rs_period = 0;	/* Value for corresponding I2C register */
	/* calculation constants */
	u32 rs_bit_cnt = 0;	/* RedSolomon Bit Count */
	u32 qam_sl_sig_power = 0;	/* used for MER, depends of QAM constellation */
	/* intermediate results */
	u32 e = 0;		/* exponent value used for QAM BER/SER */
	u32 m = 0;		/* mantisa value used for QAM BER/SER */
	u32 ber_cnt = 0;	/* BER count */
	/* signal quality info */
	u32 qam_sl_mer = 0;	/* QAM MER */
	u32 qam_pre_rs_ber = 0;	/* Pre RedSolomon BER */
	u32 qam_post_rs_ber = 0;	/* Post RedSolomon BER */
	u32 qam_vd_ser = 0;	/* ViterbiDecoder SER */
	u16 qam_vd_prescale = 0;	/* Viterbi Measurement Prescale */
	u16 qam_vd_period = 0;	/* Viterbi Measurement period */
	u32 vd_bit_cnt = 0;	/* ViterbiDecoder Bit Count */

	/* get device basic information */
	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	constellation = ext_attr->constellation;

	/* read the physical registers */
	/*   Get the RS error data */
	rc = get_qamrs_err_count(dev_addr, &measuredrs_errors);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* get the register value needed for MER */
	rc = DRXJ_DAP.read_reg16func(dev_addr, QAM_SL_ERR_POWER__A, &qam_sl_err_power, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* get the register value needed for post RS BER */
	rc = DRXJ_DAP.read_reg16func(dev_addr, FEC_OC_SNC_FAIL_PERIOD__A, &fec_oc_period, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* get constants needed for signal quality calculation */
	fec_rs_period = ext_attr->fec_rs_period;
	fec_rs_prescale = ext_attr->fec_rs_prescale;
	rs_bit_cnt = fec_rs_period * fec_rs_prescale * ext_attr->fec_rs_plen;
	qam_vd_period = ext_attr->qam_vd_period;
	qam_vd_prescale = ext_attr->qam_vd_prescale;
	vd_bit_cnt = qam_vd_period * qam_vd_prescale * ext_attr->fec_vd_plen;

	/* DRXJ_QAM_SL_SIG_POWER_QAMxxx  * 4     */
	switch (constellation) {
	case DRX_CONSTELLATION_QAM16:
		qam_sl_sig_power = DRXJ_QAM_SL_SIG_POWER_QAM16 << 2;
		break;
	case DRX_CONSTELLATION_QAM32:
		qam_sl_sig_power = DRXJ_QAM_SL_SIG_POWER_QAM32 << 2;
		break;
	case DRX_CONSTELLATION_QAM64:
		qam_sl_sig_power = DRXJ_QAM_SL_SIG_POWER_QAM64 << 2;
		break;
	case DRX_CONSTELLATION_QAM128:
		qam_sl_sig_power = DRXJ_QAM_SL_SIG_POWER_QAM128 << 2;
		break;
	case DRX_CONSTELLATION_QAM256:
		qam_sl_sig_power = DRXJ_QAM_SL_SIG_POWER_QAM256 << 2;
		break;
	default:
		return -EIO;
	}

	/* ------------------------------ */
	/* MER Calculation                */
	/* ------------------------------ */
	/* MER is good if it is above 27.5 for QAM256 or 21.5 for QAM64 */

	/* 10.0*log10(qam_sl_sig_power * 4.0 / qam_sl_err_power); */
	if (qam_sl_err_power == 0)
		qam_sl_mer = 0;
	else
		qam_sl_mer = log1_times100(qam_sl_sig_power) - log1_times100((u32)qam_sl_err_power);

	/* ----------------------------------------- */
	/* Pre Viterbi Symbol Error Rate Calculation */
	/* ----------------------------------------- */
	/* pre viterbi SER is good if it is bellow 0.025 */

	/* get the register value */
	/*   no of quadrature symbol errors */
	rc = DRXJ_DAP.read_reg16func(dev_addr, QAM_VD_NR_QSYM_ERRORS__A, &qsym_err_vd, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* Extract the Exponent and the Mantisa  */
	/* of number of quadrature symbol errors */
	e = (qsym_err_vd & QAM_VD_NR_QSYM_ERRORS_EXP__M) >>
	    QAM_VD_NR_QSYM_ERRORS_EXP__B;
	m = (qsym_err_vd & QAM_VD_NR_SYMBOL_ERRORS_FIXED_MANT__M) >>
	    QAM_VD_NR_SYMBOL_ERRORS_FIXED_MANT__B;

	if ((m << e) >> 3 > 549752)
		qam_vd_ser = 500000;
	else
		qam_vd_ser = frac_times1e6(m << ((e > 2) ? (e - 3) : e), vd_bit_cnt * ((e > 2) ? 1 : 8) / 8);

	/* --------------------------------------- */
	/* pre and post RedSolomon BER Calculation */
	/* --------------------------------------- */
	/* pre RS BER is good if it is below 3.5e-4 */

	/* get the register values */
	pre_bit_err_rs = (u32) measuredrs_errors.nr_bit_errors;
	pkt_errs = post_bit_err_rs = (u32) measuredrs_errors.nr_snc_par_fail_count;

	/* Extract the Exponent and the Mantisa of the */
	/* pre Reed-Solomon bit error count            */
	e = (pre_bit_err_rs & FEC_RS_NR_BIT_ERRORS_EXP__M) >>
	    FEC_RS_NR_BIT_ERRORS_EXP__B;
	m = (pre_bit_err_rs & FEC_RS_NR_BIT_ERRORS_FIXED_MANT__M) >>
	    FEC_RS_NR_BIT_ERRORS_FIXED_MANT__B;

	ber_cnt = m << e;

	/*qam_pre_rs_ber = frac_times1e6( ber_cnt, rs_bit_cnt ); */
	if (m > (rs_bit_cnt >> (e + 1)) || (rs_bit_cnt >> e) == 0)
		qam_pre_rs_ber = 500000;
	else
		qam_pre_rs_ber = frac_times1e6(m, rs_bit_cnt >> e);

	/* post RS BER = 1000000* (11.17 * FEC_OC_SNC_FAIL_COUNT__A) /  */
	/*               (1504.0 * FEC_OC_SNC_FAIL_PERIOD__A)  */
	/*
	   => c = (1000000*100*11.17)/1504 =
	   post RS BER = (( c* FEC_OC_SNC_FAIL_COUNT__A) /
	   (100 * FEC_OC_SNC_FAIL_PERIOD__A)
	   *100 and /100 is for more precision.
	   => (20 bits * 12 bits) /(16 bits * 7 bits)  => safe in 32 bits computation

	   Precision errors still possible.
	 */
	e = post_bit_err_rs * 742686;
	m = fec_oc_period * 100;
	if (fec_oc_period == 0)
		qam_post_rs_ber = 0xFFFFFFFF;
	else
		qam_post_rs_ber = e / m;

	/* fill signal quality data structure */
	sig_quality->MER = ((u16) qam_sl_mer);
	if (ext_attr->standard == DRX_STANDARD_ITU_B)
		sig_quality->pre_viterbi_ber = qam_vd_ser;
	else
		sig_quality->pre_viterbi_ber = qam_pre_rs_ber;
	sig_quality->post_viterbi_ber = qam_pre_rs_ber;
	sig_quality->post_reed_solomon_ber = qam_post_rs_ber;
	sig_quality->scale_factor_ber = ((u32) 1000000);
#ifdef DRXJ_SIGNAL_ACCUM_ERR
	rc = get_acc_pkt_err(demod, &sig_quality->packet_error);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
#else
	sig_quality->packet_error = ((u16) pkt_errs);
#endif

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int ctrl_get_qam_constel()
* \brief Retreive a QAM constellation point via I2C.
* \param demod Pointer to demodulator instance.
* \param complex_nr Pointer to the structure in which to store the
		   constellation point.
* \return int.
*/
static int
ctrl_get_qam_constel(struct drx_demod_instance *demod, struct drx_complex *complex_nr)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	u32 data = 0;
	u16 fec_oc_ocr_mode = 0;
			      /**< FEC OCR grabber configuration        */
	u16 qam_sl_comm_mb = 0;/**< QAM SL MB configuration              */
	u16 qam_sl_comm_mb_init = 0;
			      /**< QAM SL MB intial configuration       */
	u16 im = 0;	      /**< constellation Im part                */
	u16 re = 0;	      /**< constellation Re part                */
				     /**< device address */

	/* read device info */
	dev_addr = demod->my_i2c_dev_addr;

	/* TODO: */
	/* Monitor bus grabbing is an open external interface issue  */
	/* Needs to be checked when external interface PG is updated */

	/* Configure MB (Monitor bus) */
	rc = DRXJ_DAP.read_reg16func(dev_addr, QAM_SL_COMM_MB__A, &qam_sl_comm_mb_init, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* set observe flag & MB mux */
	qam_sl_comm_mb = qam_sl_comm_mb_init & (~(QAM_SL_COMM_MB_OBS__M +
					   QAM_SL_COMM_MB_MUX_OBS__M));
	qam_sl_comm_mb |= (QAM_SL_COMM_MB_OBS_ON +
			QAM_SL_COMM_MB_MUX_OBS_CONST_CORR);
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SL_COMM_MB__A, qam_sl_comm_mb, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Enable MB grabber in the FEC OC */
	fec_oc_ocr_mode = (/* output select:  observe bus */
			       (FEC_OC_OCR_MODE_MB_SELECT__M &
				(0x0 << FEC_OC_OCR_MODE_MB_SELECT__B)) |
			       /* grabber enable: on          */
			       (FEC_OC_OCR_MODE_GRAB_ENABLE__M &
				(0x1 << FEC_OC_OCR_MODE_GRAB_ENABLE__B)) |
			       /* grabber select: observe bus */
			       (FEC_OC_OCR_MODE_GRAB_SELECT__M &
				(0x0 << FEC_OC_OCR_MODE_GRAB_SELECT__B)) |
			       /* grabber mode:   continuous  */
			       (FEC_OC_OCR_MODE_GRAB_COUNTED__M &
				(0x0 << FEC_OC_OCR_MODE_GRAB_COUNTED__B)));
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_OCR_MODE__A, fec_oc_ocr_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Disable MB grabber in the FEC OC */
	rc = DRXJ_DAP.write_reg16func(dev_addr, FEC_OC_OCR_MODE__A, 0x00, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* read data */
	rc = DRXJ_DAP.read_reg32func(dev_addr, FEC_OC_OCR_GRAB_RD0__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	re = (u16) (data & FEC_OC_OCR_GRAB_RD0__M);
	im = (u16) ((data >> 16) & FEC_OC_OCR_GRAB_RD1__M);

	/* TODO: */
	/* interpret data (re & im) according to the Monitor bus mapping ?? */

	/* sign extension, 10th bit is sign bit */
	if ((re & 0x0200) == 0x0200)
		re |= 0xFC00;
	if ((im & 0x0200) == 0x0200)
		im |= 0xFC00;
	complex_nr->re = ((s16) re);
	complex_nr->im = ((s16) im);

	/* Restore MB (Monitor bus) */
	rc = DRXJ_DAP.write_reg16func(dev_addr, QAM_SL_COMM_MB__A, qam_sl_comm_mb_init, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}
#endif /* #ifndef DRXJ_VSB_ONLY */

/*============================================================================*/
/*==                     END QAM DATAPATH FUNCTIONS                         ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                       ATV DATAPATH FUNCTIONS                           ==*/
/*============================================================================*/
/*============================================================================*/

/*
   Implementation notes.

   NTSC/FM AGCs

      Four AGCs are used for NTSC:
      (1) RF (used to attenuate the input signal in case of to much power)
      (2) IF (used to attenuate the input signal in case of to much power)
      (3) Video AGC (used to amplify the output signal in case input to low)
      (4) SIF AGC (used to amplify the output signal in case input to low)

      Video AGC is coupled to RF and IF. SIF AGC is not coupled. It is assumed
      that the coupling between Video AGC and the RF and IF AGCs also works in
      favor of the SIF AGC.

      Three AGCs are used for FM:
      (1) RF (used to attenuate the input signal in case of to much power)
      (2) IF (used to attenuate the input signal in case of to much power)
      (3) SIF AGC (used to amplify the output signal in case input to low)

      The SIF AGC is now coupled to the RF/IF AGCs.
      The SIF AGC is needed for both SIF ouput and the internal SIF signal to
      the AUD block.

      RF and IF AGCs DACs are part of AFE, Video and SIF AGC DACs are part of
      the ATV block. The AGC control algorithms are all implemented in
      microcode.

   ATV SETTINGS

      (Shadow settings will not be used for now, they will be implemented
       later on because of the schedule)

      Several HW/SCU "settings" can be used for ATV. The standard selection
      will reset most of these settings. To avoid that the end user apllication
      has to perform these settings each time the ATV or FM standards is
      selected the driver will shadow these settings. This enables the end user
      to perform the settings only once after a drx_open(). The driver must
      write the shadow settings to HW/SCU incase:
	 ( setstandard FM/ATV) ||
	 ( settings have changed && FM/ATV standard is active)
      The shadow settings will be stored in the device specific data container.
      A set of flags will be defined to flag changes in shadow settings.
      A routine will be implemented to write all changed shadow settings to
      HW/SCU.

      The "settings" will consist of: AGC settings, filter settings etc.

      Disadvantage of use of shadow settings:
      Direct changes in HW/SCU registers will not be reflected in the
      shadow settings and these changes will be overwritten during a next
      update. This can happen during evaluation. This will not be a problem
      for normal customer usage.
*/
/* -------------------------------------------------------------------------- */

/**
* \brief Get array index for atv coef (ext_attr->atvTopCoefX[index])
* \param standard
* \param pointer to index
* \return int.
*
*/
static int atv_equ_coef_index(enum drx_standard standard, int *index)
{
	switch (standard) {
	case DRX_STANDARD_PAL_SECAM_BG:
		*index = (int)DRXJ_COEF_IDX_BG;
		break;
	case DRX_STANDARD_PAL_SECAM_DK:
		*index = (int)DRXJ_COEF_IDX_DK;
		break;
	case DRX_STANDARD_PAL_SECAM_I:
		*index = (int)DRXJ_COEF_IDX_I;
		break;
	case DRX_STANDARD_PAL_SECAM_L:
		*index = (int)DRXJ_COEF_IDX_L;
		break;
	case DRX_STANDARD_PAL_SECAM_LP:
		*index = (int)DRXJ_COEF_IDX_LP;
		break;
	case DRX_STANDARD_NTSC:
		*index = (int)DRXJ_COEF_IDX_MN;
		break;
	case DRX_STANDARD_FM:
		*index = (int)DRXJ_COEF_IDX_FM;
		break;
	default:
		*index = (int)DRXJ_COEF_IDX_MN;	/* still return a valid index */
		return -EIO;
		break;
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/**
* \fn int atv_update_config ()
* \brief Flush changes in ATV shadow registers to physical registers.
* \param demod instance of demodulator
* \param force_update don't look at standard or change flags, flush all.
* \return int.
*
*/
static int
atv_update_config(struct drx_demod_instance *demod, bool force_update)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* equalizer coefficients */
	if (force_update ||
	    ((ext_attr->atv_cfg_changed_flags & DRXJ_ATV_CHANGED_COEF) != 0)) {
		int index = 0;

		rc = atv_equ_coef_index(ext_attr->standard, &index);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_EQU0__A, ext_attr->atv_top_equ0[index], 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_EQU1__A, ext_attr->atv_top_equ1[index], 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_EQU2__A, ext_attr->atv_top_equ2[index], 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_EQU3__A, ext_attr->atv_top_equ3[index], 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* bypass fast carrier recovery */
	if (force_update) {
		u16 data = 0;

		rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_RT_ROT_BP__A, &data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		data &= (~((u16) IQM_RT_ROT_BP_ROT_OFF__M));
		if (ext_attr->phase_correction_bypass)
			data |= IQM_RT_ROT_BP_ROT_OFF_OFF;
		else
			data |= IQM_RT_ROT_BP_ROT_OFF_ACTIVE;
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_ROT_BP__A, data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* peak filter setting */
	if (force_update ||
	    ((ext_attr->atv_cfg_changed_flags & DRXJ_ATV_CHANGED_PEAK_FLT) != 0)) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_VID_PEAK__A, ext_attr->atv_top_vid_peak, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* noise filter setting */
	if (force_update ||
	    ((ext_attr->atv_cfg_changed_flags & DRXJ_ATV_CHANGED_NOISE_FLT) != 0)) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_NOISE_TH__A, ext_attr->atv_top_noise_th, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* SIF attenuation */
	if (force_update ||
	    ((ext_attr->atv_cfg_changed_flags & DRXJ_ATV_CHANGED_SIF_ATT) != 0)) {
		u16 attenuation = 0;

		switch (ext_attr->sif_attenuation) {
		case DRXJ_SIF_ATTENUATION_0DB:
			attenuation = ATV_TOP_AF_SIF_ATT_0DB;
			break;
		case DRXJ_SIF_ATTENUATION_3DB:
			attenuation = ATV_TOP_AF_SIF_ATT_M3DB;
			break;
		case DRXJ_SIF_ATTENUATION_6DB:
			attenuation = ATV_TOP_AF_SIF_ATT_M6DB;
			break;
		case DRXJ_SIF_ATTENUATION_9DB:
			attenuation = ATV_TOP_AF_SIF_ATT_M9DB;
			break;
		default:
			return -EIO;
			break;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_AF_SIF_ATT__A, attenuation, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* SIF & CVBS enable */
	if (force_update ||
	    ((ext_attr->atv_cfg_changed_flags & DRXJ_ATV_CHANGED_OUTPUT) != 0)) {
		u16 data = 0;

		rc = DRXJ_DAP.read_reg16func(dev_addr, ATV_TOP_STDBY__A, &data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (ext_attr->enable_cvbs_output)
			data |= ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE;
		else
			data &= (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE);

		if (ext_attr->enable_sif_output)
			data &= (~ATV_TOP_STDBY_SIF_STDBY_STANDBY);
		else
			data |= ATV_TOP_STDBY_SIF_STDBY_STANDBY;
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STDBY__A, data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	ext_attr->atv_cfg_changed_flags = 0;

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
/**
* \fn int ctrl_set_cfg_atv_output()
* \brief Configure ATV ouputs
* \param demod instance of demodulator
* \param output_cfg output configuaration
* \return int.
*
*/
static int
ctrl_set_cfg_atv_output(struct drx_demod_instance *demod, struct drxj_cfg_atv_output *output_cfg)
{
	struct drxj_data *ext_attr = NULL;
	int rc;

	/* Check arguments */
	if (output_cfg == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	if (output_cfg->enable_sif_output) {
		switch (output_cfg->sif_attenuation) {
		case DRXJ_SIF_ATTENUATION_0DB:	/* fallthrough */
		case DRXJ_SIF_ATTENUATION_3DB:	/* fallthrough */
		case DRXJ_SIF_ATTENUATION_6DB:	/* fallthrough */
		case DRXJ_SIF_ATTENUATION_9DB:
			/* Do nothing */
			break;
		default:
			return -EINVAL;
			break;
		}

		if (ext_attr->sif_attenuation != output_cfg->sif_attenuation) {
			ext_attr->sif_attenuation = output_cfg->sif_attenuation;
			ext_attr->atv_cfg_changed_flags |= DRXJ_ATV_CHANGED_SIF_ATT;
		}
	}

	if (ext_attr->enable_cvbs_output != output_cfg->enable_cvbs_output) {
		ext_attr->enable_cvbs_output = output_cfg->enable_cvbs_output;
		ext_attr->atv_cfg_changed_flags |= DRXJ_ATV_CHANGED_OUTPUT;
	}

	if (ext_attr->enable_sif_output != output_cfg->enable_sif_output) {
		ext_attr->enable_sif_output = output_cfg->enable_sif_output;
		ext_attr->atv_cfg_changed_flags |= DRXJ_ATV_CHANGED_OUTPUT;
	}

	rc = atv_update_config(demod, false);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
#ifndef DRXJ_DIGITAL_ONLY
/**
* \fn int ctrl_set_cfg_atv_equ_coef()
* \brief Set ATV equalizer coefficients
* \param demod instance of demodulator
* \param coef  the equalizer coefficients
* \return int.
*
*/
static int
ctrl_set_cfg_atv_equ_coef(struct drx_demod_instance *demod, struct drxj_cfg_atv_equ_coef *coef)
{
	struct drxj_data *ext_attr = NULL;
	int rc;
	int index;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* current standard needs to be an ATV standard */
	if (!DRXJ_ISATVSTD(ext_attr->standard))
		return -EIO;

	/* Check arguments */
	if ((coef == NULL) ||
	    (coef->coef0 > (ATV_TOP_EQU0_EQU_C0__M / 2)) ||
	    (coef->coef1 > (ATV_TOP_EQU1_EQU_C1__M / 2)) ||
	    (coef->coef2 > (ATV_TOP_EQU2_EQU_C2__M / 2)) ||
	    (coef->coef3 > (ATV_TOP_EQU3_EQU_C3__M / 2)) ||
	    (coef->coef0 < ((s16) ~(ATV_TOP_EQU0_EQU_C0__M >> 1))) ||
	    (coef->coef1 < ((s16) ~(ATV_TOP_EQU1_EQU_C1__M >> 1))) ||
	    (coef->coef2 < ((s16) ~(ATV_TOP_EQU2_EQU_C2__M >> 1))) ||
	    (coef->coef3 < ((s16) ~(ATV_TOP_EQU3_EQU_C3__M >> 1)))) {
		return -EINVAL;
	}

	rc = atv_equ_coef_index(ext_attr->standard, &index);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->atv_top_equ0[index] = coef->coef0;
	ext_attr->atv_top_equ1[index] = coef->coef1;
	ext_attr->atv_top_equ2[index] = coef->coef2;
	ext_attr->atv_top_equ3[index] = coef->coef3;
	ext_attr->atv_cfg_changed_flags |= DRXJ_ATV_CHANGED_COEF;

	rc = atv_update_config(demod, false);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
/**
* \fn int ctrl_get_cfg_atv_equ_coef()
* \brief Get ATV equ coef settings
* \param demod instance of demodulator
* \param coef The ATV equ coefficients
* \return int.
*
* The values are read from the shadow registers maintained by the drxdriver
* If registers are manipulated outside of the drxdriver scope the reported
* settings will not reflect these changes because of the use of shadow
* regitsers.
*
*/
static int
ctrl_get_cfg_atv_equ_coef(struct drx_demod_instance *demod, struct drxj_cfg_atv_equ_coef *coef)
{
	struct drxj_data *ext_attr = NULL;
	int rc;
	int index = 0;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* current standard needs to be an ATV standard */
	if (!DRXJ_ISATVSTD(ext_attr->standard))
		return -EIO;

	/* Check arguments */
	if (coef == NULL)
		return -EINVAL;

	rc = atv_equ_coef_index(ext_attr->standard, &index);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	coef->coef0 = ext_attr->atv_top_equ0[index];
	coef->coef1 = ext_attr->atv_top_equ1[index];
	coef->coef2 = ext_attr->atv_top_equ2[index];
	coef->coef3 = ext_attr->atv_top_equ3[index];

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
/**
* \fn int ctrl_set_cfg_atv_misc()
* \brief Set misc. settings for ATV.
* \param demod instance of demodulator
* \param
* \return int.
*
*/
static int
ctrl_set_cfg_atv_misc(struct drx_demod_instance *demod, struct drxj_cfg_atv_misc *settings)
{
	struct drxj_data *ext_attr = NULL;
	int rc;

	/* Check arguments */
	if ((settings == NULL) ||
	    ((settings->peak_filter) < (s16) (-8)) ||
	    ((settings->peak_filter) > (s16) (15)) ||
	    ((settings->noise_filter) > 15)) {
		return -EINVAL;
	}
	/* if */
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	if (settings->peak_filter != ext_attr->atv_top_vid_peak) {
		ext_attr->atv_top_vid_peak = settings->peak_filter;
		ext_attr->atv_cfg_changed_flags |= DRXJ_ATV_CHANGED_PEAK_FLT;
	}

	if (settings->noise_filter != ext_attr->atv_top_noise_th) {
		ext_attr->atv_top_noise_th = settings->noise_filter;
		ext_attr->atv_cfg_changed_flags |= DRXJ_ATV_CHANGED_NOISE_FLT;
	}

	rc = atv_update_config(demod, false);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
/**
* \fn int  ctrl_get_cfg_atv_misc()
* \brief Get misc settings of ATV.
* \param demod instance of demodulator
* \param settings misc. ATV settings
* \return int.
*
* The values are read from the shadow registers maintained by the drxdriver
* If registers are manipulated outside of the drxdriver scope the reported
* settings will not reflect these changes because of the use of shadow
* regitsers.
*/
static int
ctrl_get_cfg_atv_misc(struct drx_demod_instance *demod, struct drxj_cfg_atv_misc *settings)
{
	struct drxj_data *ext_attr = NULL;

	/* Check arguments */
	if (settings == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	settings->peak_filter = ext_attr->atv_top_vid_peak;
	settings->noise_filter = ext_attr->atv_top_noise_th;

	return 0;
}

/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/**
* \fn int  ctrl_get_cfg_atv_output()
* \brief
* \param demod instance of demodulator
* \param output_cfg output configuaration
* \return int.
*
*/
static int
ctrl_get_cfg_atv_output(struct drx_demod_instance *demod, struct drxj_cfg_atv_output *output_cfg)
{
	int rc;
	u16 data = 0;

	/* Check arguments */
	if (output_cfg == NULL)
		return -EINVAL;

	rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, ATV_TOP_STDBY__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (data & ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE)
		output_cfg->enable_cvbs_output = true;
	else
		output_cfg->enable_cvbs_output = false;

	if (data & ATV_TOP_STDBY_SIF_STDBY_STANDBY) {
		output_cfg->enable_sif_output = false;
	} else {
		output_cfg->enable_sif_output = true;
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, ATV_TOP_AF_SIF_ATT__A, &data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		output_cfg->sif_attenuation = (enum drxjsif_attenuation) data;
	}

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
/**
* \fn int  ctrl_get_cfg_atv_agc_status()
* \brief
* \param demod instance of demodulator
* \param agc_status agc status
* \return int.
*
*/
static int
ctrl_get_cfg_atv_agc_status(struct drx_demod_instance *demod,
			    struct drxj_cfg_atv_agc_status *agc_status)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	u16 data = 0;
	u32 tmp = 0;

	/* Check arguments */
	if (agc_status == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;

	/*
	   RFgain = (IQM_AF_AGC_RF__A * 26.75)/1000 (uA)
	   = ((IQM_AF_AGC_RF__A * 27) - (0.25*IQM_AF_AGC_RF__A))/1000

	   IQM_AF_AGC_RF__A * 27 is 20 bits worst case.
	 */
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_AGC_RF__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	tmp = ((u32) data) * 27 - ((u32) (data >> 2));	/* nA */
	agc_status->rf_agc_gain = (u16) (tmp / 1000);	/* uA */
	/* rounding */
	if (tmp % 1000 >= 500)
		(agc_status->rf_agc_gain)++;

	/*
	   IFgain = (IQM_AF_AGC_IF__A * 26.75)/1000 (uA)
	   = ((IQM_AF_AGC_IF__A * 27) - (0.25*IQM_AF_AGC_IF__A))/1000

	   IQM_AF_AGC_IF__A * 27 is 20 bits worst case.
	 */
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_AGC_IF__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	tmp = ((u32) data) * 27 - ((u32) (data >> 2));	/* nA */
	agc_status->if_agc_gain = (u16) (tmp / 1000);	/* uA */
	/* rounding */
	if (tmp % 1000 >= 500)
		(agc_status->if_agc_gain)++;

	/*
	   videoGain = (ATV_TOP_SFR_VID_GAIN__A/16 -150)* 0.05 (dB)
	   = (ATV_TOP_SFR_VID_GAIN__A/16 -150)/20 (dB)
	   = 10*(ATV_TOP_SFR_VID_GAIN__A/16 -150)/20 (in 0.1 dB)
	   = (ATV_TOP_SFR_VID_GAIN__A/16 -150)/2 (in 0.1 dB)
	   = (ATV_TOP_SFR_VID_GAIN__A/32) - 75 (in 0.1 dB)
	 */

	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ATV_VID_GAIN_HI__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* dividing by 32 inclusive rounding */
	data >>= 4;
	if ((data & 1) != 0)
		data++;
	data >>= 1;
	agc_status->video_agc_gain = ((s16) data) - 75;	/* 0.1 dB */

	/*
	   audioGain = (SCU_RAM_ATV_SIF_GAIN__A -8)* 0.05 (dB)
	   = (SCU_RAM_ATV_SIF_GAIN__A -8)/20 (dB)
	   = 10*(SCU_RAM_ATV_SIF_GAIN__A -8)/20 (in 0.1 dB)
	   = (SCU_RAM_ATV_SIF_GAIN__A -8)/2 (in 0.1 dB)
	   = (SCU_RAM_ATV_SIF_GAIN__A/2) - 4 (in 0.1 dB)
	 */

	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ATV_SIF_GAIN__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	data &= SCU_RAM_ATV_SIF_GAIN__M;
	/* dividing by 2 inclusive rounding */
	if ((data & 1) != 0)
		data++;
	data >>= 1;
	agc_status->audio_agc_gain = ((s16) data) - 4;	/* 0.1 dB */

	/* Loop gain's */
	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_AGC_KI__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	agc_status->video_agc_loop_gain =
	    ((data & SCU_RAM_AGC_KI_DGAIN__M) >> SCU_RAM_AGC_KI_DGAIN__B);
	agc_status->rf_agc_loop_gain =
	    ((data & SCU_RAM_AGC_KI_RF__M) >> SCU_RAM_AGC_KI_RF__B);
	agc_status->if_agc_loop_gain =
	    ((data & SCU_RAM_AGC_KI_IF__M) >> SCU_RAM_AGC_KI_IF__B);

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */

/**
* \fn int power_up_atv ()
* \brief Power up ATV.
* \param demod instance of demodulator
* \param standard either NTSC or FM (sub strandard for ATV )
* \return int.
*
* * Starts ATV and IQM
* * AUdio already started during standard init for ATV.
*/
static int power_up_atv(struct drx_demod_instance *demod, enum drx_standard standard)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;

	/* ATV NTSC */
	rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_COMM_EXEC__A, ATV_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* turn on IQM_AF */
	rc = set_iqm_af(demod, true);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = adc_synchronization(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Audio, already done during set standard */

	return 0;
rw_error:
	return -EIO;
}
#endif /* #ifndef DRXJ_DIGITAL_ONLY */

/* -------------------------------------------------------------------------- */

/**
* \fn int power_down_atv ()
* \brief Power down ATV.
* \param demod instance of demodulator
* \param standard either NTSC or FM (sub strandard for ATV )
* \return int.
*
*  Stops and thus resets ATV and IQM block
*  SIF and CVBS ADC are powered down
*  Calls audio power down
*/
static int
power_down_atv(struct drx_demod_instance *demod, enum drx_standard standard, bool primary)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drxjscu_cmd cmd_scu = { /* command      */ 0,
		/* parameter_len */ 0,
		/* result_len    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	int rc;
	u16 cmd_result = 0;

	/* ATV NTSC */

	/* Stop ATV SCU (will reset ATV and IQM hardware */
	cmd_scu.command = SCU_RAM_COMMAND_STANDARD_ATV |
	    SCU_RAM_COMMAND_CMD_DEMOD_STOP;
	cmd_scu.parameter_len = 0;
	cmd_scu.result_len = 1;
	cmd_scu.parameter = NULL;
	cmd_scu.result = &cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* Disable ATV outputs (ATV reset enables CVBS, undo this) */
	rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STDBY__A, (ATV_TOP_STDBY_SIF_STDBY_STANDBY & (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE)), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_COMM_EXEC__A, ATV_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (primary) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = set_iqm_af(demod, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
	rc = power_down_aud(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
/**
* \fn int set_atv_standard ()
* \brief Set up ATV demodulator.
* \param demod instance of demodulator
* \param standard either NTSC or FM (sub strandard for ATV )
* \return int.
*
* Init all channel independent registers.
* Assuming that IQM, ATV and AUD blocks have been reset and are in STOP mode
*
*/
#ifndef DRXJ_DIGITAL_ONLY
#define SCU_RAM_ATV_ENABLE_IIR_WA__A 0x831F6D	/* TODO remove after done with reg import */
static int
set_atv_standard(struct drx_demod_instance *demod, enum drx_standard *standard)
{
/* TODO: enable alternative for tap settings via external file

something like:
#ifdef   DRXJ_ATV_COEF_FILE
#include DRXJ_ATV_COEF_FILE
#else
... code defining fixed coef's ...
#endif

Cutsomer must create file "customer_coefs.c.inc" containing
modified copy off the constants below, and define the compiler
switch DRXJ_ATV_COEF_FILE="customer_coefs.c.inc".

Still to check if this will work; DRXJ_16TO8 macro may cause
trouble ?
*/
	const u8 ntsc_taps_re[] = {
		DRXJ_16TO8(-12),	/* re0  */
		DRXJ_16TO8(-9),	/* re1  */
		DRXJ_16TO8(9),	/* re2  */
		DRXJ_16TO8(19),	/* re3  */
		DRXJ_16TO8(-4),	/* re4  */
		DRXJ_16TO8(-24),	/* re5  */
		DRXJ_16TO8(-6),	/* re6  */
		DRXJ_16TO8(16),	/* re7  */
		DRXJ_16TO8(6),	/* re8  */
		DRXJ_16TO8(-16),	/* re9  */
		DRXJ_16TO8(-5),	/* re10 */
		DRXJ_16TO8(13),	/* re11 */
		DRXJ_16TO8(-2),	/* re12 */
		DRXJ_16TO8(-20),	/* re13 */
		DRXJ_16TO8(4),	/* re14 */
		DRXJ_16TO8(25),	/* re15 */
		DRXJ_16TO8(-6),	/* re16 */
		DRXJ_16TO8(-36),	/* re17 */
		DRXJ_16TO8(2),	/* re18 */
		DRXJ_16TO8(38),	/* re19 */
		DRXJ_16TO8(-10),	/* re20 */
		DRXJ_16TO8(-48),	/* re21 */
		DRXJ_16TO8(35),	/* re22 */
		DRXJ_16TO8(94),	/* re23 */
		DRXJ_16TO8(-59),	/* re24 */
		DRXJ_16TO8(-217),	/* re25 */
		DRXJ_16TO8(50),	/* re26 */
		DRXJ_16TO8(679)	/* re27 */
	};
	const u8 ntsc_taps_im[] = {
		DRXJ_16TO8(11),	/* im0  */
		DRXJ_16TO8(1),	/* im1  */
		DRXJ_16TO8(-10),	/* im2  */
		DRXJ_16TO8(2),	/* im3  */
		DRXJ_16TO8(24),	/* im4  */
		DRXJ_16TO8(21),	/* im5  */
		DRXJ_16TO8(1),	/* im6  */
		DRXJ_16TO8(-4),	/* im7  */
		DRXJ_16TO8(7),	/* im8  */
		DRXJ_16TO8(14),	/* im9  */
		DRXJ_16TO8(27),	/* im10 */
		DRXJ_16TO8(42),	/* im11 */
		DRXJ_16TO8(22),	/* im12 */
		DRXJ_16TO8(-20),	/* im13 */
		DRXJ_16TO8(2),	/* im14 */
		DRXJ_16TO8(98),	/* im15 */
		DRXJ_16TO8(122),	/* im16 */
		DRXJ_16TO8(0),	/* im17 */
		DRXJ_16TO8(-85),	/* im18 */
		DRXJ_16TO8(51),	/* im19 */
		DRXJ_16TO8(247),	/* im20 */
		DRXJ_16TO8(192),	/* im21 */
		DRXJ_16TO8(-55),	/* im22 */
		DRXJ_16TO8(-95),	/* im23 */
		DRXJ_16TO8(217),	/* im24 */
		DRXJ_16TO8(544),	/* im25 */
		DRXJ_16TO8(553),	/* im26 */
		DRXJ_16TO8(302)	/* im27 */
	};
	const u8 bg_taps_re[] = {
		DRXJ_16TO8(-18),	/* re0  */
		DRXJ_16TO8(18),	/* re1  */
		DRXJ_16TO8(19),	/* re2  */
		DRXJ_16TO8(-26),	/* re3  */
		DRXJ_16TO8(-20),	/* re4  */
		DRXJ_16TO8(36),	/* re5  */
		DRXJ_16TO8(5),	/* re6  */
		DRXJ_16TO8(-51),	/* re7  */
		DRXJ_16TO8(15),	/* re8  */
		DRXJ_16TO8(45),	/* re9  */
		DRXJ_16TO8(-46),	/* re10 */
		DRXJ_16TO8(-24),	/* re11 */
		DRXJ_16TO8(71),	/* re12 */
		DRXJ_16TO8(-17),	/* re13 */
		DRXJ_16TO8(-83),	/* re14 */
		DRXJ_16TO8(74),	/* re15 */
		DRXJ_16TO8(75),	/* re16 */
		DRXJ_16TO8(-134),	/* re17 */
		DRXJ_16TO8(-40),	/* re18 */
		DRXJ_16TO8(191),	/* re19 */
		DRXJ_16TO8(-11),	/* re20 */
		DRXJ_16TO8(-233),	/* re21 */
		DRXJ_16TO8(74),	/* re22 */
		DRXJ_16TO8(271),	/* re23 */
		DRXJ_16TO8(-132),	/* re24 */
		DRXJ_16TO8(-341),	/* re25 */
		DRXJ_16TO8(172),	/* re26 */
		DRXJ_16TO8(801)	/* re27 */
	};
	const u8 bg_taps_im[] = {
		DRXJ_16TO8(-24),	/* im0  */
		DRXJ_16TO8(-10),	/* im1  */
		DRXJ_16TO8(9),	/* im2  */
		DRXJ_16TO8(-5),	/* im3  */
		DRXJ_16TO8(-51),	/* im4  */
		DRXJ_16TO8(-17),	/* im5  */
		DRXJ_16TO8(31),	/* im6  */
		DRXJ_16TO8(-48),	/* im7  */
		DRXJ_16TO8(-95),	/* im8  */
		DRXJ_16TO8(25),	/* im9  */
		DRXJ_16TO8(37),	/* im10 */
		DRXJ_16TO8(-123),	/* im11 */
		DRXJ_16TO8(-77),	/* im12 */
		DRXJ_16TO8(94),	/* im13 */
		DRXJ_16TO8(-10),	/* im14 */
		DRXJ_16TO8(-149),	/* im15 */
		DRXJ_16TO8(10),	/* im16 */
		DRXJ_16TO8(108),	/* im17 */
		DRXJ_16TO8(-49),	/* im18 */
		DRXJ_16TO8(-59),	/* im19 */
		DRXJ_16TO8(90),	/* im20 */
		DRXJ_16TO8(73),	/* im21 */
		DRXJ_16TO8(55),	/* im22 */
		DRXJ_16TO8(148),	/* im23 */
		DRXJ_16TO8(86),	/* im24 */
		DRXJ_16TO8(146),	/* im25 */
		DRXJ_16TO8(687),	/* im26 */
		DRXJ_16TO8(877)	/* im27 */
	};
	const u8 dk_i_l_lp_taps_re[] = {
		DRXJ_16TO8(-23),	/* re0  */
		DRXJ_16TO8(9),	/* re1  */
		DRXJ_16TO8(16),	/* re2  */
		DRXJ_16TO8(-26),	/* re3  */
		DRXJ_16TO8(-3),	/* re4  */
		DRXJ_16TO8(13),	/* re5  */
		DRXJ_16TO8(-19),	/* re6  */
		DRXJ_16TO8(-3),	/* re7  */
		DRXJ_16TO8(13),	/* re8  */
		DRXJ_16TO8(-26),	/* re9  */
		DRXJ_16TO8(-4),	/* re10 */
		DRXJ_16TO8(28),	/* re11 */
		DRXJ_16TO8(-15),	/* re12 */
		DRXJ_16TO8(-14),	/* re13 */
		DRXJ_16TO8(10),	/* re14 */
		DRXJ_16TO8(1),	/* re15 */
		DRXJ_16TO8(39),	/* re16 */
		DRXJ_16TO8(-18),	/* re17 */
		DRXJ_16TO8(-90),	/* re18 */
		DRXJ_16TO8(109),	/* re19 */
		DRXJ_16TO8(113),	/* re20 */
		DRXJ_16TO8(-235),	/* re21 */
		DRXJ_16TO8(-49),	/* re22 */
		DRXJ_16TO8(359),	/* re23 */
		DRXJ_16TO8(-79),	/* re24 */
		DRXJ_16TO8(-459),	/* re25 */
		DRXJ_16TO8(206),	/* re26 */
		DRXJ_16TO8(894)	/* re27 */
	};
	const u8 dk_i_l_lp_taps_im[] = {
		DRXJ_16TO8(-8),	/* im0  */
		DRXJ_16TO8(-20),	/* im1  */
		DRXJ_16TO8(17),	/* im2  */
		DRXJ_16TO8(-14),	/* im3  */
		DRXJ_16TO8(-52),	/* im4  */
		DRXJ_16TO8(4),	/* im5  */
		DRXJ_16TO8(9),	/* im6  */
		DRXJ_16TO8(-62),	/* im7  */
		DRXJ_16TO8(-47),	/* im8  */
		DRXJ_16TO8(0),	/* im9  */
		DRXJ_16TO8(-20),	/* im10 */
		DRXJ_16TO8(-48),	/* im11 */
		DRXJ_16TO8(-65),	/* im12 */
		DRXJ_16TO8(-23),	/* im13 */
		DRXJ_16TO8(44),	/* im14 */
		DRXJ_16TO8(-60),	/* im15 */
		DRXJ_16TO8(-113),	/* im16 */
		DRXJ_16TO8(92),	/* im17 */
		DRXJ_16TO8(81),	/* im18 */
		DRXJ_16TO8(-125),	/* im19 */
		DRXJ_16TO8(28),	/* im20 */
		DRXJ_16TO8(182),	/* im21 */
		DRXJ_16TO8(35),	/* im22 */
		DRXJ_16TO8(94),	/* im23 */
		DRXJ_16TO8(180),	/* im24 */
		DRXJ_16TO8(134),	/* im25 */
		DRXJ_16TO8(657),	/* im26 */
		DRXJ_16TO8(1023)	/* im27 */
	};
	const u8 fm_taps_re[] = {
		DRXJ_16TO8(0),	/* re0  */
		DRXJ_16TO8(0),	/* re1  */
		DRXJ_16TO8(0),	/* re2  */
		DRXJ_16TO8(0),	/* re3  */
		DRXJ_16TO8(0),	/* re4  */
		DRXJ_16TO8(0),	/* re5  */
		DRXJ_16TO8(0),	/* re6  */
		DRXJ_16TO8(0),	/* re7  */
		DRXJ_16TO8(0),	/* re8  */
		DRXJ_16TO8(0),	/* re9  */
		DRXJ_16TO8(0),	/* re10 */
		DRXJ_16TO8(0),	/* re11 */
		DRXJ_16TO8(0),	/* re12 */
		DRXJ_16TO8(0),	/* re13 */
		DRXJ_16TO8(0),	/* re14 */
		DRXJ_16TO8(0),	/* re15 */
		DRXJ_16TO8(0),	/* re16 */
		DRXJ_16TO8(0),	/* re17 */
		DRXJ_16TO8(0),	/* re18 */
		DRXJ_16TO8(0),	/* re19 */
		DRXJ_16TO8(0),	/* re20 */
		DRXJ_16TO8(0),	/* re21 */
		DRXJ_16TO8(0),	/* re22 */
		DRXJ_16TO8(0),	/* re23 */
		DRXJ_16TO8(0),	/* re24 */
		DRXJ_16TO8(0),	/* re25 */
		DRXJ_16TO8(0),	/* re26 */
		DRXJ_16TO8(0)	/* re27 */
	};
	const u8 fm_taps_im[] = {
		DRXJ_16TO8(-6),	/* im0  */
		DRXJ_16TO8(2),	/* im1  */
		DRXJ_16TO8(14),	/* im2  */
		DRXJ_16TO8(-38),	/* im3  */
		DRXJ_16TO8(58),	/* im4  */
		DRXJ_16TO8(-62),	/* im5  */
		DRXJ_16TO8(42),	/* im6  */
		DRXJ_16TO8(0),	/* im7  */
		DRXJ_16TO8(-45),	/* im8  */
		DRXJ_16TO8(73),	/* im9  */
		DRXJ_16TO8(-65),	/* im10 */
		DRXJ_16TO8(23),	/* im11 */
		DRXJ_16TO8(34),	/* im12 */
		DRXJ_16TO8(-77),	/* im13 */
		DRXJ_16TO8(80),	/* im14 */
		DRXJ_16TO8(-39),	/* im15 */
		DRXJ_16TO8(-25),	/* im16 */
		DRXJ_16TO8(78),	/* im17 */
		DRXJ_16TO8(-90),	/* im18 */
		DRXJ_16TO8(52),	/* im19 */
		DRXJ_16TO8(16),	/* im20 */
		DRXJ_16TO8(-77),	/* im21 */
		DRXJ_16TO8(97),	/* im22 */
		DRXJ_16TO8(-62),	/* im23 */
		DRXJ_16TO8(-8),	/* im24 */
		DRXJ_16TO8(75),	/* im25 */
		DRXJ_16TO8(-100),	/* im26 */
		DRXJ_16TO8(70)	/* im27 */
	};

	struct i2c_device_addr *dev_addr = NULL;
	struct drxjscu_cmd cmd_scu = { /* command      */ 0,
		/* parameter_len */ 0,
		/* result_len    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	u16 cmd_result = 0;
	u16 cmd_param = 0;
#ifdef DRXJ_SPLIT_UCODE_UPLOAD
	struct drxu_code_info ucode_info;
	struct drx_common_attr *common_attr = NULL;
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */
	struct drxj_data *ext_attr = NULL;
	int rc;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	dev_addr = demod->my_i2c_dev_addr;

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
	common_attr = demod->my_common_attr;

	/* Check if audio microcode is already uploaded */
	if (!(ext_attr->flag_aud_mc_uploaded)) {
		ucode_info.mc_data = common_attr->microcode;

		/* Upload only audio microcode */
		rc = ctrl_u_code_upload(demod, &ucode_info, UCODE_UPLOAD, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		if (common_attr->verify_microcode == true) {
			rc = ctrl_u_code_upload(demod, &ucode_info, UCODE_VERIFY, true);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}

		/* Prevent uploading audio microcode again */
		ext_attr->flag_aud_mc_uploaded = true;
	}
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

	rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_COMM_EXEC__A, ATV_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* Reset ATV SCU */
	cmd_scu.command = SCU_RAM_COMMAND_STANDARD_ATV |
	    SCU_RAM_COMMAND_CMD_DEMOD_RESET;
	cmd_scu.parameter_len = 0;
	cmd_scu.result_len = 1;
	cmd_scu.parameter = NULL;
	cmd_scu.result = &cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_MOD_CONTROL__A, ATV_TOP_MOD_CONTROL__PRE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* TODO remove AUTO/OFF patches after ucode fix. */
	switch (*standard) {
	case DRX_STANDARD_NTSC:
		/* NTSC */
		cmd_param = SCU_RAM_ATV_STANDARD_STANDARD_MN;

		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_LO_INCR__A, IQM_RT_LO_INCR_MN, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(ntsc_taps_re), ((u8 *)ntsc_taps_re), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(ntsc_taps_im), ((u8 *)ntsc_taps_im), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_AMP_TH__A, ATV_TOP_CR_AMP_TH_MN, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_CONT__A, (ATV_TOP_CR_CONT_CR_P_MN | ATV_TOP_CR_CONT_CR_D_MN | ATV_TOP_CR_CONT_CR_I_MN), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_MN, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_MN | ATV_TOP_STD_VID_POL_MN), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_MN, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AGC_MODE__A, (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM | SCU_RAM_ATV_AGC_MODE_FAST_VAGC_EN_FAGC_ENABLE), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AMS_MAX_REF__A, SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_BG_MN, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->phase_correction_bypass = false;
		ext_attr->enable_cvbs_output = true;
		break;
	case DRX_STANDARD_FM:
		/* FM */
		cmd_param = SCU_RAM_ATV_STANDARD_STANDARD_FM;

		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_LO_INCR__A, 2994, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_MIDTAP__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(fm_taps_re), ((u8 *)fm_taps_re), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(fm_taps_im), ((u8 *)fm_taps_im), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_FM | ATV_TOP_STD_VID_POL_FM), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_MOD_CONTROL__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_CONT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AGC_MODE__A, (SCU_RAM_ATV_AGC_MODE_VAGC_VEL_AGC_SLOW | SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_ROT_BP__A, IQM_RT_ROT_BP_ROT_OFF_OFF, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->phase_correction_bypass = true;
		ext_attr->enable_cvbs_output = false;
		break;
	case DRX_STANDARD_PAL_SECAM_BG:
		/* PAL/SECAM B/G */
		cmd_param = SCU_RAM_ATV_STANDARD_STANDARD_B;

		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_LO_INCR__A, 1820, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* TODO check with IS */
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(bg_taps_re), ((u8 *)bg_taps_re), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(bg_taps_im), ((u8 *)bg_taps_im), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_BG, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_AMP_TH__A, ATV_TOP_CR_AMP_TH_BG, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_CONT__A, (ATV_TOP_CR_CONT_CR_P_BG | ATV_TOP_CR_CONT_CR_D_BG | ATV_TOP_CR_CONT_CR_I_BG), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_BG, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_BG | ATV_TOP_STD_VID_POL_BG), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AGC_MODE__A, (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM | SCU_RAM_ATV_AGC_MODE_FAST_VAGC_EN_FAGC_ENABLE), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AMS_MAX_REF__A, SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_BG_MN, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->phase_correction_bypass = false;
		ext_attr->atv_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_AUTO;
		ext_attr->enable_cvbs_output = true;
		break;
	case DRX_STANDARD_PAL_SECAM_DK:
		/* PAL/SECAM D/K */
		cmd_param = SCU_RAM_ATV_STANDARD_STANDARD_DK;

		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_LO_INCR__A, 2225, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* TODO check with IS */
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(dk_i_l_lp_taps_re), ((u8 *)dk_i_l_lp_taps_re), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(dk_i_l_lp_taps_im), ((u8 *)dk_i_l_lp_taps_im), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_AMP_TH__A, ATV_TOP_CR_AMP_TH_DK, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_DK, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_CONT__A, (ATV_TOP_CR_CONT_CR_P_DK | ATV_TOP_CR_CONT_CR_D_DK | ATV_TOP_CR_CONT_CR_I_DK), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_DK, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_DK | ATV_TOP_STD_VID_POL_DK), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AGC_MODE__A, (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM | SCU_RAM_ATV_AGC_MODE_FAST_VAGC_EN_FAGC_ENABLE), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AMS_MAX_REF__A, SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_DK, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->phase_correction_bypass = false;
		ext_attr->atv_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_AUTO;
		ext_attr->enable_cvbs_output = true;
		break;
	case DRX_STANDARD_PAL_SECAM_I:
		/* PAL/SECAM I   */
		cmd_param = SCU_RAM_ATV_STANDARD_STANDARD_I;

		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_LO_INCR__A, 2225, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* TODO check with IS */
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(dk_i_l_lp_taps_re), ((u8 *)dk_i_l_lp_taps_re), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(dk_i_l_lp_taps_im), ((u8 *)dk_i_l_lp_taps_im), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_AMP_TH__A, ATV_TOP_CR_AMP_TH_I, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_I, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_CONT__A, (ATV_TOP_CR_CONT_CR_P_I | ATV_TOP_CR_CONT_CR_D_I | ATV_TOP_CR_CONT_CR_I_I), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_I, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_I | ATV_TOP_STD_VID_POL_I), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AGC_MODE__A, (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM | SCU_RAM_ATV_AGC_MODE_FAST_VAGC_EN_FAGC_ENABLE), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AMS_MAX_REF__A, SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_I, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->phase_correction_bypass = false;
		ext_attr->atv_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_AUTO;
		ext_attr->enable_cvbs_output = true;
		break;
	case DRX_STANDARD_PAL_SECAM_L:
		/* PAL/SECAM L with negative modulation */
		cmd_param = SCU_RAM_ATV_STANDARD_STANDARD_L;

		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_LO_INCR__A, 2225, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* TODO check with IS */
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_L, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(dk_i_l_lp_taps_re), ((u8 *)dk_i_l_lp_taps_re), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(dk_i_l_lp_taps_im), ((u8 *)dk_i_l_lp_taps_im), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_AMP_TH__A, 0x2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* TODO check with IS */
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_CONT__A, (ATV_TOP_CR_CONT_CR_P_L | ATV_TOP_CR_CONT_CR_D_L | ATV_TOP_CR_CONT_CR_I_L), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_L, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_L | ATV_TOP_STD_VID_POL_L), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AGC_MODE__A, (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_AM | SCU_RAM_ATV_AGC_MODE_BP_EN_BPC_ENABLE | SCU_RAM_ATV_AGC_MODE_VAGC_VEL_AGC_SLOW), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AMS_MAX_REF__A, SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_LLP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->phase_correction_bypass = false;
		ext_attr->atv_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_USER;
		ext_attr->atv_if_agc_cfg.output_level = ext_attr->atv_rf_agc_cfg.top;
		ext_attr->enable_cvbs_output = true;
		break;
	case DRX_STANDARD_PAL_SECAM_LP:
		/* PAL/SECAM L with positive modulation */
		cmd_param = SCU_RAM_ATV_STANDARD_STANDARD_LP;

		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_LP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_LO_INCR__A, 2225, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* TODO check with IS */
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_RE0__A, sizeof(dk_i_l_lp_taps_re), ((u8 *)dk_i_l_lp_taps_re), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_block_func(dev_addr, IQM_CF_TAP_IM0__A, sizeof(dk_i_l_lp_taps_im), ((u8 *)dk_i_l_lp_taps_im), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_AMP_TH__A, 0x2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* TODO check with IS */
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_CONT__A, (ATV_TOP_CR_CONT_CR_P_LP | ATV_TOP_CR_CONT_CR_D_LP | ATV_TOP_CR_CONT_CR_I_LP), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_LP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_LP | ATV_TOP_STD_VID_POL_LP), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AGC_MODE__A, (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_AM | SCU_RAM_ATV_AGC_MODE_BP_EN_BPC_ENABLE | SCU_RAM_ATV_AGC_MODE_VAGC_VEL_AGC_SLOW), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AMS_MAX_REF__A, SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_LLP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->phase_correction_bypass = false;
		ext_attr->atv_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_USER;
		ext_attr->atv_if_agc_cfg.output_level = ext_attr->atv_rf_agc_cfg.top;
		ext_attr->enable_cvbs_output = true;
		break;
	default:
		return -EIO;
	}

	/* Common initializations FM & NTSC & B/G & D/K & I & L & LP */
	if (!ext_attr->has_lna) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_AMUX__A, 0x01, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_STANDARD__A, 0x002, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_CLP_LEN__A, IQM_AF_CLP_LEN_ATV, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_CLP_TH__A, IQM_AF_CLP_TH_ATV, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_SNS_LEN__A, IQM_AF_SNS_LEN_ATV, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = ctrl_set_cfg_pre_saw(demod, &(ext_attr->atv_pre_saw_cfg));
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_AGC_IF__A, 10248, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	ext_attr->iqm_rc_rate_ofs = 0x00200000L;
	rc = DRXJ_DAP.write_reg32func(dev_addr, IQM_RC_RATE_OFS_LO__A, ext_attr->iqm_rc_rate_ofs, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_ADJ_SEL__A, IQM_RC_ADJ_SEL_B_OFF, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RC_STRETCH__A, IQM_RC_STRETCH_ATV, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_ACTIVE__A, IQM_RT_ACTIVE_ACTIVE_RT_ATV_FCR_ON | IQM_RT_ACTIVE_ACTIVE_CR_ATV_CR_ON, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_OUT_ENA__A, IQM_CF_OUT_ENA_ATV__M, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_CF_SYMMETRIC__A, IQM_CF_SYMMETRIC_IM__M, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* default: SIF in standby */
	rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_SYNC_SLICE__A, ATV_TOP_SYNC_SLICE_MN, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_MOD_ACCU__A, ATV_TOP_MOD_ACCU__PRE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_SIF_GAIN__A, 0x080, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_FAGC_TH_RED__A, 10, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AAGC_CNT__A, 7, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_NAGC_KI_MIN__A, 0x0225, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_NAGC_KI_MAX__A, 0x0547, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_KI_CHANGE_TH__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_LOCK__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_RT_DELAY__A, IQM_RT_DELAY__PRE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_BPC_KI_MIN__A, 531, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_PAGC_KI_MIN__A, 1061, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_BP_REF_MIN__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_BP_REF_MAX__A, 260, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_BP_LVL__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AMS_MAX__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_AMS_MIN__A, 2047, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_GPIO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Override reset values with current shadow settings */
	rc = atv_update_config(demod, true);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Configure/restore AGC settings */
	rc = init_agc(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = set_agc_if(demod, &(ext_attr->atv_if_agc_cfg), false);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = set_agc_rf(demod, &(ext_attr->atv_rf_agc_cfg), false);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = ctrl_set_cfg_pre_saw(demod, &(ext_attr->atv_pre_saw_cfg));
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Set SCU ATV substandard,assuming this doesn't require running ATV block */
	cmd_scu.command = SCU_RAM_COMMAND_STANDARD_ATV |
	    SCU_RAM_COMMAND_CMD_DEMOD_SET_ENV;
	cmd_scu.parameter_len = 1;
	cmd_scu.result_len = 1;
	cmd_scu.parameter = &cmd_param;
	cmd_scu.result = &cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* turn the analog work around on/off (must after set_env b/c it is set in mc) */
	if (ext_attr->mfx == 0x03) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_ENABLE_IIR_WA__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_ENABLE_IIR_WA__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ATV_IIR_CRIT__A, 225, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	return 0;
rw_error:
	return -EIO;
}
#endif

/* -------------------------------------------------------------------------- */

#ifndef DRXJ_DIGITAL_ONLY
/**
* \fn int set_atv_channel ()
* \brief Set ATV channel.
* \param demod:   instance of demod.
* \return int.
*
* Not much needs to be done here, only start the SCU for NTSC/FM.
* Mirrored channels are not expected in the RF domain, so IQM FS setting
* doesn't need to be remembered.
* The channel->mirror parameter is therefor ignored.
*
*/
static int
set_atv_channel(struct drx_demod_instance *demod,
		s32 tuner_freq_offset,
	      struct drx_channel *channel, enum drx_standard standard)
{
	struct drxjscu_cmd cmd_scu = { /* command      */ 0,
		/* parameter_len */ 0,
		/* result_len    */ 0,
		/* parameter    */ NULL,
		/* result       */ NULL
	};
	u16 cmd_result = 0;
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	int rc;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/*
	   Program frequency shifter
	   No need to account for mirroring on RF
	 */
	if (channel->mirror == DRX_MIRROR_AUTO)
		ext_attr->mirror = DRX_MIRROR_NO;
	else
		ext_attr->mirror = channel->mirror;

	rc = set_frequency(demod, channel, tuner_freq_offset);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_CR_FREQ__A, ATV_TOP_CR_FREQ__PRE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Start ATV SCU */
	cmd_scu.command = SCU_RAM_COMMAND_STANDARD_ATV |
	    SCU_RAM_COMMAND_CMD_DEMOD_START;
	cmd_scu.parameter_len = 0;
	cmd_scu.result_len = 1;
	cmd_scu.parameter = NULL;
	cmd_scu.result = &cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

/*   if ( (ext_attr->standard == DRX_STANDARD_FM) && (ext_attr->flagSetAUDdone == true) )
   {
      ext_attr->detectedRDS = (bool)false;
   }*/

	return 0;
rw_error:
	return -EIO;
}
#endif

/* -------------------------------------------------------------------------- */

/**
* \fn int get_atv_channel ()
* \brief Set ATV channel.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \param standard: NTSC or FM.
* \return int.
*
* Covers NTSC, PAL/SECAM - B/G, D/K, I, L, LP and FM.
* Computes the frequency offset in te RF domain and adds it to
* channel->frequency. Determines the value for channel->bandwidth.
*
*/
#ifndef DRXJ_DIGITAL_ONLY
static int
get_atv_channel(struct drx_demod_instance *demod,
		struct drx_channel *channel, enum drx_standard standard)
{
	s32 offset = 0;
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;

	/* Bandwidth */
	channel->bandwidth = ((struct drxj_data *) demod->my_ext_attr)->curr_bandwidth;

	switch (standard) {
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
		{
			u16 measured_offset = 0;

			/* get measured frequency offset */
			rc = DRXJ_DAP.read_reg16func(dev_addr, ATV_TOP_CR_FREQ__A, &measured_offset, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			/* Signed 8 bit register => sign extension needed */
			if ((measured_offset & 0x0080) != 0)
				measured_offset |= 0xFF80;
			offset +=
			    (s32) (((s16) measured_offset) * 10);
			break;
		}
	case DRX_STANDARD_PAL_SECAM_LP:
		{
			u16 measured_offset = 0;

			/* get measured frequency offset */
			rc = DRXJ_DAP.read_reg16func(dev_addr, ATV_TOP_CR_FREQ__A, &measured_offset, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			/* Signed 8 bit register => sign extension needed */
			if ((measured_offset & 0x0080) != 0)
				measured_offset |= 0xFF80;
			offset -=
			    (s32) (((s16) measured_offset) * 10);
		}
		break;
	case DRX_STANDARD_FM:
		/* TODO: compute offset using AUD_DSP_RD_FM_DC_LEVEL_A__A and
		   AUD_DSP_RD_FM_DC_LEVEL_B__A. For now leave frequency as is.
		 */
		/* No bandwidth know for FM */
		channel->bandwidth = DRX_BANDWIDTH_UNKNOWN;
		break;
	default:
		return -EIO;
	}

	channel->frequency -= offset;

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
/**
* \fn int get_atv_sig_strength()
* \brief Retrieve signal strength for ATV & FM.
* \param devmod Pointer to demodulator instance.
* \param sig_quality Pointer to signal strength data; range 0, .. , 100.
* \return int.
* \retval 0 sig_strength contains valid data.
* \retval -EIO Erroneous data, sig_strength equals 0.
*
* Taking into account:
*  * digital gain
*  * IF gain      (not implemented yet, waiting for IF gain control by ucode)
*  * RF gain
*
* All weights (digital, if, rf) must add up to 100.
*
* TODO: ? dynamically adapt weights in case RF and/or IF agc of drxj
*         is not used ?
*/
static int
get_atv_sig_strength(struct drx_demod_instance *demod, u16 *sig_strength)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	/* All weights must add up to 100 (%)
	   TODO: change weights when IF ctrl is available */
	u32 digital_weight = 50;	/* 0 .. 100 */
	u32 rf_weight = 50;	/* 0 .. 100 */
	u32 if_weight = 0;	/* 0 .. 100 */

	u16 digital_curr_gain = 0;
	u32 digital_max_gain = 0;
	u32 digital_min_gain = 0;
	u16 rf_curr_gain = 0;
	u32 rf_max_gain = 0x800;	/* taken from ucode */
	u32 rf_min_gain = 0x7fff;
	u16 if_curr_gain = 0;
	u32 if_max_gain = 0x800;	/* taken from ucode */
	u32 if_min_gain = 0x7fff;

	u32 digital_strength = 0;	/* 0.. 100 */
	u32 rf_strength = 0;	/* 0.. 100 */
	u32 if_strength = 0;	/* 0.. 100 */

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	*sig_strength = 0;

	switch (ext_attr->standard) {
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:
		rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ATV_VID_GAIN_HI__A, &digital_curr_gain, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		digital_max_gain = 22512;	/* taken from ucode */
		digital_min_gain = 2400;	/* taken from ucode */
		break;
	case DRX_STANDARD_FM:
		rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ATV_SIF_GAIN__A, &digital_curr_gain, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		digital_max_gain = 0x4ff;	/* taken from ucode */
		digital_min_gain = 0;	/* taken from ucode */
		break;
	default:
		return -EIO;
		break;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_AGC_RF__A, &rf_curr_gain, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_AF_AGC_IF__A, &if_curr_gain, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* clipping */
	if (digital_curr_gain >= digital_max_gain)
		digital_curr_gain = (u16)digital_max_gain;
	if (digital_curr_gain <= digital_min_gain)
		digital_curr_gain = (u16)digital_min_gain;
	if (if_curr_gain <= if_max_gain)
		if_curr_gain = (u16)if_max_gain;
	if (if_curr_gain >= if_min_gain)
		if_curr_gain = (u16)if_min_gain;
	if (rf_curr_gain <= rf_max_gain)
		rf_curr_gain = (u16)rf_max_gain;
	if (rf_curr_gain >= rf_min_gain)
		rf_curr_gain = (u16)rf_min_gain;

	/* TODO: use SCU_RAM_ATV_RAGC_HR__A to shift max and min in case
	   of clipping at ADC */

	/* Compute signal strength (in %) per "gain domain" */

	/* Digital gain  */
	/* TODO: ADC clipping not handled */
	digital_strength = (100 * (digital_max_gain - (u32) digital_curr_gain)) /
	    (digital_max_gain - digital_min_gain);

	/* TODO: IF gain not implemented yet in microcode, check after impl. */
	if_strength = (100 * ((u32) if_curr_gain - if_max_gain)) /
	    (if_min_gain - if_max_gain);

	/* Rf gain */
	/* TODO: ADC clipping not handled */
	rf_strength = (100 * ((u32) rf_curr_gain - rf_max_gain)) /
	    (rf_min_gain - rf_max_gain);

	/* Compute a weighted signal strength (in %) */
	*sig_strength = (u16) (digital_weight * digital_strength +
				rf_weight * rf_strength + if_weight * if_strength);
	*sig_strength /= 100;

	return 0;
rw_error:
	return -EIO;
}

/* -------------------------------------------------------------------------- */
/**
* \fn int atv_sig_quality()
* \brief Retrieve signal quality indication for ATV.
* \param devmod Pointer to demodulator instance.
* \param sig_quality Pointer to signal quality structure.
* \return int.
* \retval 0 sig_quality contains valid data.
* \retval -EIO Erroneous data, sig_quality indicator equals 0.
*
*
*/
static int
atv_sig_quality(struct drx_demod_instance *demod, struct drx_sig_quality *sig_quality)
{
	struct i2c_device_addr *dev_addr = NULL;
	u16 quality_indicator = 0;
	int rc;

	dev_addr = demod->my_i2c_dev_addr;

	/* defined values for fields not used */
	sig_quality->MER = 0;
	sig_quality->pre_viterbi_ber = 0;
	sig_quality->post_viterbi_ber = 0;
	sig_quality->scale_factor_ber = 1;
	sig_quality->packet_error = 0;
	sig_quality->post_reed_solomon_ber = 0;

	/*
	   Mapping:
	   0x000..0x080: strong signal  => 80% .. 100%
	   0x080..0x700: weak signal    => 30% .. 80%
	   0x700..0x7ff: no signal      => 0%  .. 30%
	 */

	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ATV_CR_LOCK__A, &quality_indicator, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	quality_indicator &= SCU_RAM_ATV_CR_LOCK_CR_LOCK__M;
	if (quality_indicator <= 0x80) {
		sig_quality->indicator =
		    80 + ((20 * (0x80 - quality_indicator)) / 0x80);
	} else if (quality_indicator <= 0x700)
		sig_quality->indicator = 30 + ((50 * (0x700 - quality_indicator)) / (0x700 - 0x81));
	else
		sig_quality->indicator = (30 * (0x7FF - quality_indicator)) / (0x7FF - 0x701);

	return 0;
rw_error:
	return -EIO;
}
#endif /* DRXJ_DIGITAL_ONLY */

/*============================================================================*/
/*==                     END ATV DATAPATH FUNCTIONS                         ==*/
/*============================================================================*/

#ifndef DRXJ_EXCLUDE_AUDIO
/*===========================================================================*/
/*===========================================================================*/
/*==                      AUDIO DATAPATH FUNCTIONS                         ==*/
/*===========================================================================*/
/*===========================================================================*/

/*
* \brief Power up AUD.
* \param demod instance of demodulator
* \return int.
*
*/
static int power_up_aud(struct drx_demod_instance *demod, bool set_standard)
{
	enum drx_aud_standard aud_standard = DRX_AUD_STANDARD_AUTO;
	struct i2c_device_addr *dev_addr = NULL;
	int rc;

	dev_addr = demod->my_i2c_dev_addr;

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_TOP_COMM_EXEC__A, AUD_TOP_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* setup TR interface: R/W mode, fifosize=8 */
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_TOP_TR_MDE__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_COMM_EXEC__A, AUD_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (set_standard) {
		rc = aud_ctrl_set_standard(demod, &aud_standard);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \brief Power up AUD.
* \param demod instance of demodulator
* \return int.
*
*/
static int power_down_aud(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_COMM_EXEC__A, AUD_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	ext_attr->aud_data.audio_is_active = false;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get Modus data from audio RAM
* \param demod instance of demodulator
* \param pointer to modus
* \return int.
*
*/
static int aud_get_modus(struct drx_demod_instance *demod, u16 *modus)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	u16 r_modus = 0;
	u16 r_modus_hi = 0;
	u16 r_modus_lo = 0;

	if (modus == NULL)
		return -EINVAL;

	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* Modus register is combined in to RAM location */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_MODUS_HI__A, &r_modus_hi, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_MODUS_LO__A, &r_modus_lo, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	r_modus = ((r_modus_hi << 12) & AUD_DEM_RAM_MODUS_HI__M)
	    | (((r_modus_lo & AUD_DEM_RAM_MODUS_LO__M)));

	*modus = r_modus;

	return 0;
rw_error:
	return -EIO;

}

/*============================================================================*/
/**
* \brief Get audio RDS dat
* \param demod instance of demodulator
* \param pointer to struct drx_cfg_aud_rds * \return int.
*
*/
static int
aud_ctrl_get_cfg_rds(struct drx_demod_instance *demod, struct drx_cfg_aud_rds *status)
{
	struct i2c_device_addr *addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	u16 r_rds_array_cnt_init = 0;
	u16 r_rds_array_cnt_check = 0;
	u16 r_rds_data = 0;
	u16 rds_data_cnt = 0;

	addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	if (status == NULL)
		return -EINVAL;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	status->valid = false;

	rc = DRXJ_DAP.read_reg16func(addr, AUD_DEM_RD_RDS_ARRAY_CNT__A, &r_rds_array_cnt_init, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (r_rds_array_cnt_init ==
	    AUD_DEM_RD_RDS_ARRAY_CNT_RDS_ARRAY_CT_RDS_DATA_NOT_VALID) {
		/* invalid data */
		return 0;
	}

	if (ext_attr->aud_data.rds_data_counter == r_rds_array_cnt_init) {
		/* no new data */
		return 0;
	}

	/* RDS is detected, as long as FM radio is selected assume
	   RDS will be available                                    */
	ext_attr->aud_data.rds_data_present = true;

	/* new data */
	/* read the data */
	for (rds_data_cnt = 0; rds_data_cnt < AUD_RDS_ARRAY_SIZE; rds_data_cnt++) {
		rc = DRXJ_DAP.read_reg16func(addr, AUD_DEM_RD_RDS_DATA__A, &r_rds_data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		status->data[rds_data_cnt] = r_rds_data;
	}

	rc = DRXJ_DAP.read_reg16func(addr, AUD_DEM_RD_RDS_ARRAY_CNT__A, &r_rds_array_cnt_check, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (r_rds_array_cnt_check == r_rds_array_cnt_init) {
		status->valid = true;
		ext_attr->aud_data.rds_data_counter = r_rds_array_cnt_check;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get the current audio carrier detection status
* \param demod instance of demodulator
* \param pointer to aud_ctrl_get_status
* \return int.
*
*/
static int
aud_ctrl_get_carrier_detect_status(struct drx_demod_instance *demod, struct drx_aud_status *status)
{
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	u16 r_data = 0;

	if (status == NULL)
		return -EINVAL;

	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* initialize the variables */
	status->carrier_a = false;
	status->carrier_b = false;
	status->nicam_status = DRX_AUD_NICAM_NOT_DETECTED;
	status->sap = false;
	status->stereo = false;

	/* read stereo sound mode indication */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RD_STATUS__A, &r_data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* carrier a detected */
	if ((r_data & AUD_DEM_RD_STATUS_STAT_CARR_A__M) == AUD_DEM_RD_STATUS_STAT_CARR_A_DETECTED)
		status->carrier_a = true;

	/* carrier b detected */
	if ((r_data & AUD_DEM_RD_STATUS_STAT_CARR_B__M) == AUD_DEM_RD_STATUS_STAT_CARR_B_DETECTED)
		status->carrier_b = true;
	/* nicam detected */
	if ((r_data & AUD_DEM_RD_STATUS_STAT_NICAM__M) ==
	    AUD_DEM_RD_STATUS_STAT_NICAM_NICAM_DETECTED) {
		if ((r_data & AUD_DEM_RD_STATUS_BAD_NICAM__M) == AUD_DEM_RD_STATUS_BAD_NICAM_OK)
			status->nicam_status = DRX_AUD_NICAM_DETECTED;
		else
			status->nicam_status = DRX_AUD_NICAM_BAD;
	}

	/* audio mode bilingual or SAP detected */
	if ((r_data & AUD_DEM_RD_STATUS_STAT_BIL_OR_SAP__M) == AUD_DEM_RD_STATUS_STAT_BIL_OR_SAP_SAP)
		status->sap = true;

	/* stereo detected */
	if ((r_data & AUD_DEM_RD_STATUS_STAT_STEREO__M) == AUD_DEM_RD_STATUS_STAT_STEREO_STEREO)
		status->stereo = true;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get the current audio status parameters
* \param demod instance of demodulator
* \param pointer to aud_ctrl_get_status
* \return int.
*
*/
static int
aud_ctrl_get_status(struct drx_demod_instance *demod, struct drx_aud_status *status)
{
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	struct drx_cfg_aud_rds rds = { false, {0} };
	int rc;
	u16 r_data = 0;

	if (status == NULL)
		return -EINVAL;

	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* carrier detection */
	rc = aud_ctrl_get_carrier_detect_status(demod, status);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* rds data */
	status->rds = false;
	rc = aud_ctrl_get_cfg_rds(demod, &rds);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	status->rds = ext_attr->aud_data.rds_data_present;

	/* fm_ident */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_RD_FM_IDENT_VALUE__A, &r_data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	r_data >>= AUD_DSP_RD_FM_IDENT_VALUE_FM_IDENT__B;
	status->fm_ident = (s8) r_data;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get the current volume settings
* \param demod instance of demodulator
* \param pointer to struct drx_cfg_aud_volume * \return int.
*
*/
static int
aud_ctrl_get_cfg_volume(struct drx_demod_instance *demod, struct drx_cfg_aud_volume *volume)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	u16 r_volume = 0;
	u16 r_avc = 0;
	u16 r_strength_left = 0;
	u16 r_strength_right = 0;

	if (volume == NULL)
		return -EINVAL;

	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* volume */
	volume->mute = ext_attr->aud_data.volume.mute;
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_VOLUME__A, &r_volume, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (r_volume == 0) {
		volume->mute = true;
		volume->volume = ext_attr->aud_data.volume.volume;
	} else {
		volume->mute = false;
		volume->volume = ((r_volume & AUD_DSP_WR_VOLUME_VOL_MAIN__M) >>
				  AUD_DSP_WR_VOLUME_VOL_MAIN__B) -
		    AUD_VOLUME_ZERO_DB;
		if (volume->volume < AUD_VOLUME_DB_MIN)
			volume->volume = AUD_VOLUME_DB_MIN;
		if (volume->volume > AUD_VOLUME_DB_MAX)
			volume->volume = AUD_VOLUME_DB_MAX;
	}

	/* automatic volume control */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_AVC__A, &r_avc, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if ((r_avc & AUD_DSP_WR_AVC_AVC_ON__M) == AUD_DSP_WR_AVC_AVC_ON_OFF) {
		volume->avc_mode = DRX_AUD_AVC_OFF;
	} else {
		switch (r_avc & AUD_DSP_WR_AVC_AVC_DECAY__M) {
		case AUD_DSP_WR_AVC_AVC_DECAY_20_MSEC:
			volume->avc_mode = DRX_AUD_AVC_DECAYTIME_20MS;
			break;
		case AUD_DSP_WR_AVC_AVC_DECAY_8_SEC:
			volume->avc_mode = DRX_AUD_AVC_DECAYTIME_8S;
			break;
		case AUD_DSP_WR_AVC_AVC_DECAY_4_SEC:
			volume->avc_mode = DRX_AUD_AVC_DECAYTIME_4S;
			break;
		case AUD_DSP_WR_AVC_AVC_DECAY_2_SEC:
			volume->avc_mode = DRX_AUD_AVC_DECAYTIME_2S;
			break;
		default:
			return -EIO;
			break;
		}
	}

	/* max attenuation */
	switch (r_avc & AUD_DSP_WR_AVC_AVC_MAX_ATT__M) {
	case AUD_DSP_WR_AVC_AVC_MAX_ATT_12DB:
		volume->avc_max_atten = DRX_AUD_AVC_MAX_ATTEN_12DB;
		break;
	case AUD_DSP_WR_AVC_AVC_MAX_ATT_18DB:
		volume->avc_max_atten = DRX_AUD_AVC_MAX_ATTEN_18DB;
		break;
	case AUD_DSP_WR_AVC_AVC_MAX_ATT_24DB:
		volume->avc_max_atten = DRX_AUD_AVC_MAX_ATTEN_24DB;
		break;
	default:
		return -EIO;
		break;
	}

	/* max gain */
	switch (r_avc & AUD_DSP_WR_AVC_AVC_MAX_GAIN__M) {
	case AUD_DSP_WR_AVC_AVC_MAX_GAIN_0DB:
		volume->avc_max_gain = DRX_AUD_AVC_MAX_GAIN_0DB;
		break;
	case AUD_DSP_WR_AVC_AVC_MAX_GAIN_6DB:
		volume->avc_max_gain = DRX_AUD_AVC_MAX_GAIN_6DB;
		break;
	case AUD_DSP_WR_AVC_AVC_MAX_GAIN_12DB:
		volume->avc_max_gain = DRX_AUD_AVC_MAX_GAIN_12DB;
		break;
	default:
		return -EIO;
		break;
	}

	/* reference level */
	volume->avc_ref_level = (u16) ((r_avc & AUD_DSP_WR_AVC_AVC_REF_LEV__M) >>
				       AUD_DSP_WR_AVC_AVC_REF_LEV__B);

	/* read qpeak registers and calculate strength of left and right carrier */
	/* quasi peaks formula: QP(dB) = 20 * log( AUD_DSP_RD_QPEAKx / Q(0dB) */
	/* Q(0dB) represents QP value of 0dB (hex value 0x4000) */
	/* left carrier */

	/* QP vaues */
	/* left carrier */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_RD_QPEAK_L__A, &r_strength_left, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	volume->strength_left = (((s16) log1_times100(r_strength_left)) -
				AUD_CARRIER_STRENGTH_QP_0DB_LOG10T100) / 5;

	/* right carrier */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_RD_QPEAK_R__A, &r_strength_right, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	volume->strength_right = (((s16) log1_times100(r_strength_right)) -
				 AUD_CARRIER_STRENGTH_QP_0DB_LOG10T100) / 5;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Set the current volume settings
* \param demod instance of demodulator
* \param pointer to struct drx_cfg_aud_volume * \return int.
*
*/
static int
aud_ctrl_set_cfg_volume(struct drx_demod_instance *demod, struct drx_cfg_aud_volume *volume)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	u16 w_volume = 0;
	u16 w_avc = 0;

	if (volume == NULL)
		return -EINVAL;

	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* volume */
	/* volume range from -60 to 12 (expressed in dB) */
	if ((volume->volume < AUD_VOLUME_DB_MIN) ||
	    (volume->volume > AUD_VOLUME_DB_MAX))
		return -EINVAL;

	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_VOLUME__A, &w_volume, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* clear the volume mask */
	w_volume &= (u16) ~AUD_DSP_WR_VOLUME_VOL_MAIN__M;
	if (volume->mute == true)
		w_volume |= (u16)(0);
	else
		w_volume |= (u16)((volume->volume + AUD_VOLUME_ZERO_DB) << AUD_DSP_WR_VOLUME_VOL_MAIN__B);

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DSP_WR_VOLUME__A, w_volume, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* automatic volume control */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_AVC__A, &w_avc, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* clear masks that require writing */
	w_avc &= (u16) ~AUD_DSP_WR_AVC_AVC_ON__M;
	w_avc &= (u16) ~AUD_DSP_WR_AVC_AVC_DECAY__M;

	if (volume->avc_mode == DRX_AUD_AVC_OFF) {
		w_avc |= (AUD_DSP_WR_AVC_AVC_ON_OFF);
	} else {

		w_avc |= (AUD_DSP_WR_AVC_AVC_ON_ON);

		/* avc decay */
		switch (volume->avc_mode) {
		case DRX_AUD_AVC_DECAYTIME_20MS:
			w_avc |= AUD_DSP_WR_AVC_AVC_DECAY_20_MSEC;
			break;
		case DRX_AUD_AVC_DECAYTIME_8S:
			w_avc |= AUD_DSP_WR_AVC_AVC_DECAY_8_SEC;
			break;
		case DRX_AUD_AVC_DECAYTIME_4S:
			w_avc |= AUD_DSP_WR_AVC_AVC_DECAY_4_SEC;
			break;
		case DRX_AUD_AVC_DECAYTIME_2S:
			w_avc |= AUD_DSP_WR_AVC_AVC_DECAY_2_SEC;
			break;
		default:
			return -EINVAL;
		}
	}

	/* max attenuation */
	w_avc &= (u16) ~AUD_DSP_WR_AVC_AVC_MAX_ATT__M;
	switch (volume->avc_max_atten) {
	case DRX_AUD_AVC_MAX_ATTEN_12DB:
		w_avc |= AUD_DSP_WR_AVC_AVC_MAX_ATT_12DB;
		break;
	case DRX_AUD_AVC_MAX_ATTEN_18DB:
		w_avc |= AUD_DSP_WR_AVC_AVC_MAX_ATT_18DB;
		break;
	case DRX_AUD_AVC_MAX_ATTEN_24DB:
		w_avc |= AUD_DSP_WR_AVC_AVC_MAX_ATT_24DB;
		break;
	default:
		return -EINVAL;
	}

	/* max gain */
	w_avc &= (u16) ~AUD_DSP_WR_AVC_AVC_MAX_GAIN__M;
	switch (volume->avc_max_gain) {
	case DRX_AUD_AVC_MAX_GAIN_0DB:
		w_avc |= AUD_DSP_WR_AVC_AVC_MAX_GAIN_0DB;
		break;
	case DRX_AUD_AVC_MAX_GAIN_6DB:
		w_avc |= AUD_DSP_WR_AVC_AVC_MAX_GAIN_6DB;
		break;
	case DRX_AUD_AVC_MAX_GAIN_12DB:
		w_avc |= AUD_DSP_WR_AVC_AVC_MAX_GAIN_12DB;
		break;
	default:
		return -EINVAL;
	}

	/* avc reference level */
	if (volume->avc_ref_level > AUD_MAX_AVC_REF_LEVEL)
		return -EINVAL;

	w_avc &= (u16) ~AUD_DSP_WR_AVC_AVC_REF_LEV__M;
	w_avc |= (u16) (volume->avc_ref_level << AUD_DSP_WR_AVC_AVC_REF_LEV__B);

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DSP_WR_AVC__A, w_avc, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* all done, store config in data structure */
	ext_attr->aud_data.volume = *volume;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get the I2S settings
* \param demod instance of demodulator
* \param pointer to struct drx_cfg_i2s_output * \return int.
*
*/
static int
aud_ctrl_get_cfg_output_i2s(struct drx_demod_instance *demod, struct drx_cfg_i2s_output *output)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;
	u16 w_i2s_config = 0;
	u16 r_i2s_freq = 0;

	if (output == NULL)
		return -EINVAL;

	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_I2S_CONFIG2__A, &w_i2s_config, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_I2S_OUT_FS__A, &r_i2s_freq, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* I2S mode */
	switch (w_i2s_config & AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST__M) {
	case AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST_MASTER:
		output->mode = DRX_I2S_MODE_MASTER;
		break;
	case AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST_SLAVE:
		output->mode = DRX_I2S_MODE_SLAVE;
		break;
	default:
		return -EIO;
	}

	/* I2S format */
	switch (w_i2s_config & AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE__M) {
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE_DELAY:
		output->format = DRX_I2S_FORMAT_WS_ADVANCED;
		break;
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE_NO_DELAY:
		output->format = DRX_I2S_FORMAT_WS_WITH_DATA;
		break;
	default:
		return -EIO;
	}

	/* I2S word length */
	switch (w_i2s_config & AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN__M) {
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN_BIT_16:
		output->word_length = DRX_I2S_WORDLENGTH_16;
		break;
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN_BIT_32:
		output->word_length = DRX_I2S_WORDLENGTH_32;
		break;
	default:
		return -EIO;
	}

	/* I2S polarity */
	switch (w_i2s_config & AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL__M) {
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL_LEFT_HIGH:
		output->polarity = DRX_I2S_POLARITY_LEFT;
		break;
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL_LEFT_LOW:
		output->polarity = DRX_I2S_POLARITY_RIGHT;
		break;
	default:
		return -EIO;
	}

	/* I2S output enabled */
	if ((w_i2s_config & AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE__M) == AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE_ENABLE)
		output->output_enable = true;
	else
		output->output_enable = false;

	if (r_i2s_freq > 0) {
		output->frequency = 6144UL * 48000 / r_i2s_freq;
		if (output->word_length == DRX_I2S_WORDLENGTH_16)
			output->frequency *= 2;
	} else {
		output->frequency = AUD_I2S_FREQUENCY_MAX;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Set the I2S settings
* \param demod instance of demodulator
* \param pointer to struct drx_cfg_i2s_output * \return int.
*
*/
static int
aud_ctrl_set_cfg_output_i2s(struct drx_demod_instance *demod, struct drx_cfg_i2s_output *output)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;
	u16 w_i2s_config = 0;
	u16 w_i2s_pads_data_da = 0;
	u16 w_i2s_pads_data_cl = 0;
	u16 w_i2s_pads_data_ws = 0;
	u32 w_i2s_freq = 0;

	if (output == NULL)
		return -EINVAL;

	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_I2S_CONFIG2__A, &w_i2s_config, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* I2S mode */
	w_i2s_config &= (u16) ~AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST__M;

	switch (output->mode) {
	case DRX_I2S_MODE_MASTER:
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST_MASTER;
		break;
	case DRX_I2S_MODE_SLAVE:
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST_SLAVE;
		break;
	default:
		return -EINVAL;
	}

	/* I2S format */
	w_i2s_config &= (u16) ~AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE__M;

	switch (output->format) {
	case DRX_I2S_FORMAT_WS_ADVANCED:
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE_DELAY;
		break;
	case DRX_I2S_FORMAT_WS_WITH_DATA:
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE_NO_DELAY;
		break;
	default:
		return -EINVAL;
	}

	/* I2S word length */
	w_i2s_config &= (u16) ~AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN__M;

	switch (output->word_length) {
	case DRX_I2S_WORDLENGTH_16:
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN_BIT_16;
		break;
	case DRX_I2S_WORDLENGTH_32:
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN_BIT_32;
		break;
	default:
		return -EINVAL;
	}

	/* I2S polarity */
	w_i2s_config &= (u16) ~AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL__M;
	switch (output->polarity) {
	case DRX_I2S_POLARITY_LEFT:
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL_LEFT_HIGH;
		break;
	case DRX_I2S_POLARITY_RIGHT:
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL_LEFT_LOW;
		break;
	default:
		return -EINVAL;
	}

	/* I2S output enabled */
	w_i2s_config &= (u16) ~AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE__M;
	if (output->output_enable == true)
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE_ENABLE;
	else
		w_i2s_config |= AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE_DISABLE;

	/*
	   I2S frequency

	   w_i2s_freq = 6144 * 48000 * nrbits / ( 32 * frequency )

	   16bit: 6144 * 48000 / ( 2 * freq ) = ( 6144 * 48000 / freq ) / 2
	   32bit: 6144 * 48000 / freq         = ( 6144 * 48000 / freq )
	 */
	if ((output->frequency > AUD_I2S_FREQUENCY_MAX) ||
	    output->frequency < AUD_I2S_FREQUENCY_MIN) {
		return -EINVAL;
	}

	w_i2s_freq = (6144UL * 48000UL) + (output->frequency >> 1);
	w_i2s_freq /= output->frequency;

	if (output->word_length == DRX_I2S_WORDLENGTH_16)
		w_i2s_freq *= 2;

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_I2S_CONFIG2__A, w_i2s_config, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DSP_WR_I2S_OUT_FS__A, (u16)w_i2s_freq, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* configure I2S output pads for master or slave mode */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (output->mode == DRX_I2S_MODE_MASTER) {
		w_i2s_pads_data_da = SIO_PDR_I2S_DA_CFG_MODE__MASTER |
		    SIO_PDR_I2S_DA_CFG_DRIVE__MASTER;
		w_i2s_pads_data_cl = SIO_PDR_I2S_CL_CFG_MODE__MASTER |
		    SIO_PDR_I2S_CL_CFG_DRIVE__MASTER;
		w_i2s_pads_data_ws = SIO_PDR_I2S_WS_CFG_MODE__MASTER |
		    SIO_PDR_I2S_WS_CFG_DRIVE__MASTER;
	} else {
		w_i2s_pads_data_da = SIO_PDR_I2S_DA_CFG_MODE__SLAVE |
		    SIO_PDR_I2S_DA_CFG_DRIVE__SLAVE;
		w_i2s_pads_data_cl = SIO_PDR_I2S_CL_CFG_MODE__SLAVE |
		    SIO_PDR_I2S_CL_CFG_DRIVE__SLAVE;
		w_i2s_pads_data_ws = SIO_PDR_I2S_WS_CFG_MODE__SLAVE |
		    SIO_PDR_I2S_WS_CFG_DRIVE__SLAVE;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2S_DA_CFG__A, w_i2s_pads_data_da, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2S_CL_CFG__A, w_i2s_pads_data_cl, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_I2S_WS_CFG__A, w_i2s_pads_data_ws, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY__PRE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* all done, store config in data structure */
	ext_attr->aud_data.i2sdata = *output;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get the Automatic Standard Select (ASS)
*        and Automatic Sound Change (ASC)
* \param demod instance of demodulator
* \param pointer to pDRXAudAutoSound_t
* \return int.
*
*/
static int
aud_ctrl_get_cfg_auto_sound(struct drx_demod_instance *demod,
			    enum drx_cfg_aud_auto_sound *auto_sound)
{
	struct drxj_data *ext_attr = NULL;
	int rc;
	u16 r_modus = 0;

	if (auto_sound == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	rc = aud_get_modus(demod, &r_modus);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	switch (r_modus & (AUD_DEM_WR_MODUS_MOD_ASS__M |
			  AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG__M)) {
	case AUD_DEM_WR_MODUS_MOD_ASS_OFF | AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_DISABLED:
	case AUD_DEM_WR_MODUS_MOD_ASS_OFF | AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_ENABLED:
		*auto_sound =
		    DRX_AUD_AUTO_SOUND_OFF;
		break;
	case AUD_DEM_WR_MODUS_MOD_ASS_ON | AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_ENABLED:
		*auto_sound =
		    DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_ON;
		break;
	case AUD_DEM_WR_MODUS_MOD_ASS_ON | AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_DISABLED:
		*auto_sound =
		    DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_OFF;
		break;
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Set the Automatic Standard Select (ASS)
*        and Automatic Sound Change (ASC)
* \param demod instance of demodulator
* \param pointer to pDRXAudAutoSound_t
* \return int.
*
*/
static int
aud_ctr_setl_cfg_auto_sound(struct drx_demod_instance *demod,
			    enum drx_cfg_aud_auto_sound *auto_sound)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 r_modus = 0;
	u16 w_modus = 0;

	if (auto_sound == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	rc = aud_get_modus(demod, &r_modus);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	w_modus = r_modus;
	/* clear ASS & ASC bits */
	w_modus &= (u16) ~AUD_DEM_WR_MODUS_MOD_ASS__M;
	w_modus &= (u16) ~AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG__M;

	switch (*auto_sound) {
	case DRX_AUD_AUTO_SOUND_OFF:
		w_modus |= AUD_DEM_WR_MODUS_MOD_ASS_OFF;
		w_modus |= AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_DISABLED;
		break;
	case DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_ON:
		w_modus |= AUD_DEM_WR_MODUS_MOD_ASS_ON;
		w_modus |= AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_ENABLED;
		break;
	case DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_OFF:
		w_modus |= AUD_DEM_WR_MODUS_MOD_ASS_ON;
		w_modus |= AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_DISABLED;
		break;
	default:
		return -EINVAL;
	}

	if (w_modus != r_modus) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_MODUS__A, w_modus, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
	/* copy to data structure */
	ext_attr->aud_data.auto_sound = *auto_sound;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get the Automatic Standard Select thresholds
* \param demod instance of demodulator
* \param pointer to pDRXAudASSThres_t
* \return int.
*
*/
static int
aud_ctrl_get_cfg_ass_thres(struct drx_demod_instance *demod, struct drx_cfg_aud_ass_thres *thres)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 thres_a2 = 0;
	u16 thres_btsc = 0;
	u16 thres_nicam = 0;

	if (thres == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_A2_THRSHLD__A, &thres_a2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_BTSC_THRSHLD__A, &thres_btsc, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_NICAM_THRSHLD__A, &thres_nicam, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	thres->a2 = thres_a2;
	thres->btsc = thres_btsc;
	thres->nicam = thres_nicam;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get the Automatic Standard Select thresholds
* \param demod instance of demodulator
* \param pointer to pDRXAudASSThres_t
* \return int.
*
*/
static int
aud_ctrl_set_cfg_ass_thres(struct drx_demod_instance *demod, struct drx_cfg_aud_ass_thres *thres)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	if (thres == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_A2_THRSHLD__A, thres->a2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_BTSC_THRSHLD__A, thres->btsc, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_NICAM_THRSHLD__A, thres->nicam, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* update DRXK data structure with hardware values */
	ext_attr->aud_data.ass_thresholds = *thres;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get Audio Carrier settings
* \param demod instance of demodulator
* \param pointer to struct drx_aud_carrier ** \return int.
*
*/
static int
aud_ctrl_get_cfg_carrier(struct drx_demod_instance *demod, struct drx_cfg_aud_carriers *carriers)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 w_modus = 0;

	u16 dco_a_hi = 0;
	u16 dco_a_lo = 0;
	u16 dco_b_hi = 0;
	u16 dco_b_lo = 0;

	u32 valA = 0;
	u32 valB = 0;

	u16 dc_lvl_a = 0;
	u16 dc_lvl_b = 0;

	u16 cm_thes_a = 0;
	u16 cm_thes_b = 0;

	if (carriers == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	rc = aud_get_modus(demod, &w_modus);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Behaviour of primary audio channel */
	switch (w_modus & (AUD_DEM_WR_MODUS_MOD_CM_A__M)) {
	case AUD_DEM_WR_MODUS_MOD_CM_A_MUTE:
		carriers->a.opt = DRX_NO_CARRIER_MUTE;
		break;
	case AUD_DEM_WR_MODUS_MOD_CM_A_NOISE:
		carriers->a.opt = DRX_NO_CARRIER_NOISE;
		break;
	default:
		return -EIO;
		break;
	}

	/* Behaviour of secondary audio channel */
	switch (w_modus & (AUD_DEM_WR_MODUS_MOD_CM_B__M)) {
	case AUD_DEM_WR_MODUS_MOD_CM_B_MUTE:
		carriers->b.opt = DRX_NO_CARRIER_MUTE;
		break;
	case AUD_DEM_WR_MODUS_MOD_CM_B_NOISE:
		carriers->b.opt = DRX_NO_CARRIER_NOISE;
		break;
	default:
		return -EIO;
		break;
	}

	/* frequency adjustment for primary & secondary audio channel */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_DCO_A_HI__A, &dco_a_hi, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_DCO_A_LO__A, &dco_a_lo, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_DCO_B_HI__A, &dco_b_hi, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_DCO_B_LO__A, &dco_b_lo, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	valA = (((u32) dco_a_hi) << 12) | ((u32) dco_a_lo & 0xFFF);
	valB = (((u32) dco_b_hi) << 12) | ((u32) dco_b_lo & 0xFFF);

	/* Multiply by 20250 * 1>>24  ~= 2 / 1657 */
	carriers->a.dco = DRX_S24TODRXFREQ(valA) * 2L / 1657L;
	carriers->b.dco = DRX_S24TODRXFREQ(valB) * 2L / 1657L;

	/* DC level of the incoming FM signal on the primary
	   & seconday sound channel */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_RD_FM_DC_LEVEL_A__A, &dc_lvl_a, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_RD_FM_DC_LEVEL_B__A, &dc_lvl_b, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* offset (kHz) = (dcLvl / 322) */
	carriers->a.shift = (DRX_U16TODRXFREQ(dc_lvl_a) / 322L);
	carriers->b.shift = (DRX_U16TODRXFREQ(dc_lvl_b) / 322L);

	/* Carrier detetcion threshold for primary & secondary channel */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_CM_A_THRSHLD__A, &cm_thes_a, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RAM_CM_B_THRSHLD__A, &cm_thes_b, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	carriers->a.thres = cm_thes_a;
	carriers->b.thres = cm_thes_b;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Set Audio Carrier settings
* \param demod instance of demodulator
* \param pointer to struct drx_aud_carrier ** \return int.
*
*/
static int
aud_ctrl_set_cfg_carrier(struct drx_demod_instance *demod, struct drx_cfg_aud_carriers *carriers)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 w_modus = 0;
	u16 r_modus = 0;
	u16 dco_a_hi = 0;
	u16 dco_a_lo = 0;
	u16 dco_b_hi = 0;
	u16 dco_b_lo = 0;
	s32 valA = 0;
	s32 valB = 0;

	if (carriers == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	rc = aud_get_modus(demod, &r_modus);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	w_modus = r_modus;
	w_modus &= (u16) ~AUD_DEM_WR_MODUS_MOD_CM_A__M;
	/* Behaviour of primary audio channel */
	switch (carriers->a.opt) {
	case DRX_NO_CARRIER_MUTE:
		w_modus |= AUD_DEM_WR_MODUS_MOD_CM_A_MUTE;
		break;
	case DRX_NO_CARRIER_NOISE:
		w_modus |= AUD_DEM_WR_MODUS_MOD_CM_A_NOISE;
		break;
	default:
		return -EINVAL;
		break;
	}

	/* Behaviour of secondary audio channel */
	w_modus &= (u16) ~AUD_DEM_WR_MODUS_MOD_CM_B__M;
	switch (carriers->b.opt) {
	case DRX_NO_CARRIER_MUTE:
		w_modus |= AUD_DEM_WR_MODUS_MOD_CM_B_MUTE;
		break;
	case DRX_NO_CARRIER_NOISE:
		w_modus |= AUD_DEM_WR_MODUS_MOD_CM_B_NOISE;
		break;
	default:
		return -EINVAL;
		break;
	}

	/* now update the modus register */
	if (w_modus != r_modus) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_MODUS__A, w_modus, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* frequency adjustment for primary & secondary audio channel */
	valA = (s32) ((carriers->a.dco) * 1657L / 2);
	valB = (s32) ((carriers->b.dco) * 1657L / 2);

	dco_a_hi = (u16) ((valA >> 12) & 0xFFF);
	dco_a_lo = (u16) (valA & 0xFFF);
	dco_b_hi = (u16) ((valB >> 12) & 0xFFF);
	dco_b_lo = (u16) (valB & 0xFFF);

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_DCO_A_HI__A, dco_a_hi, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_DCO_A_LO__A, dco_a_lo, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_DCO_B_HI__A, dco_b_hi, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_DCO_B_LO__A, dco_b_lo, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Carrier detetcion threshold for primary & secondary channel */
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_CM_A_THRSHLD__A, carriers->a.thres, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_CM_B_THRSHLD__A, carriers->b.thres, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* update DRXK data structure */
	ext_attr->aud_data.carriers = *carriers;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get I2S Source, I2S matrix and FM matrix
* \param demod instance of demodulator
* \param pointer to pDRXAudmixer_t
* \return int.
*
*/
static int
aud_ctrl_get_cfg_mixer(struct drx_demod_instance *demod, struct drx_cfg_aud_mixer *mixer)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 src_i2s_matr = 0;
	u16 fm_matr = 0;

	if (mixer == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* Source Selctor */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_SRC_I2S_MATR__A, &src_i2s_matr, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	switch (src_i2s_matr & AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S__M) {
	case AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_MONO:
		mixer->source_i2s = DRX_AUD_SRC_MONO;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_AB:
		mixer->source_i2s = DRX_AUD_SRC_STEREO_OR_AB;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_A:
		mixer->source_i2s = DRX_AUD_SRC_STEREO_OR_A;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_B:
		mixer->source_i2s = DRX_AUD_SRC_STEREO_OR_B;
		break;
	default:
		return -EIO;
	}

	/* Matrix */
	switch (src_i2s_matr & AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S__M) {
	case AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_MONO:
		mixer->matrix_i2s = DRX_AUD_I2S_MATRIX_MONO;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_STEREO:
		mixer->matrix_i2s = DRX_AUD_I2S_MATRIX_STEREO;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_SOUND_A:
		mixer->matrix_i2s = DRX_AUD_I2S_MATRIX_A_MONO;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_SOUND_B:
		mixer->matrix_i2s = DRX_AUD_I2S_MATRIX_B_MONO;
		break;
	default:
		return -EIO;
	}

	/* FM Matrix */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_WR_FM_MATRIX__A, &fm_matr, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	switch (fm_matr & AUD_DEM_WR_FM_MATRIX__M) {
	case AUD_DEM_WR_FM_MATRIX_NO_MATRIX:
		mixer->matrix_fm = DRX_AUD_FM_MATRIX_NO_MATRIX;
		break;
	case AUD_DEM_WR_FM_MATRIX_GERMAN_MATRIX:
		mixer->matrix_fm = DRX_AUD_FM_MATRIX_GERMAN;
		break;
	case AUD_DEM_WR_FM_MATRIX_KOREAN_MATRIX:
		mixer->matrix_fm = DRX_AUD_FM_MATRIX_KOREAN;
		break;
	case AUD_DEM_WR_FM_MATRIX_SOUND_A:
		mixer->matrix_fm = DRX_AUD_FM_MATRIX_SOUND_A;
		break;
	case AUD_DEM_WR_FM_MATRIX_SOUND_B:
		mixer->matrix_fm = DRX_AUD_FM_MATRIX_SOUND_B;
		break;
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Set I2S Source, I2S matrix and FM matrix
* \param demod instance of demodulator
* \param pointer to DRXAudmixer_t
* \return int.
*
*/
static int
aud_ctrl_set_cfg_mixer(struct drx_demod_instance *demod, struct drx_cfg_aud_mixer *mixer)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 src_i2s_matr = 0;
	u16 fm_matr = 0;

	if (mixer == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* Source Selctor */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_SRC_I2S_MATR__A, &src_i2s_matr, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	src_i2s_matr &= (u16) ~AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S__M;

	switch (mixer->source_i2s) {
	case DRX_AUD_SRC_MONO:
		src_i2s_matr |= AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_MONO;
		break;
	case DRX_AUD_SRC_STEREO_OR_AB:
		src_i2s_matr |= AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_AB;
		break;
	case DRX_AUD_SRC_STEREO_OR_A:
		src_i2s_matr |= AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_A;
		break;
	case DRX_AUD_SRC_STEREO_OR_B:
		src_i2s_matr |= AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_B;
		break;
	default:
		return -EINVAL;
	}

	/* Matrix */
	src_i2s_matr &= (u16) ~AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S__M;
	switch (mixer->matrix_i2s) {
	case DRX_AUD_I2S_MATRIX_MONO:
		src_i2s_matr |= AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_MONO;
		break;
	case DRX_AUD_I2S_MATRIX_STEREO:
		src_i2s_matr |= AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_STEREO;
		break;
	case DRX_AUD_I2S_MATRIX_A_MONO:
		src_i2s_matr |= AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_SOUND_A;
		break;
	case DRX_AUD_I2S_MATRIX_B_MONO:
		src_i2s_matr |= AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_SOUND_B;
		break;
	default:
		return -EINVAL;
	}
	/* write the result */
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DSP_WR_SRC_I2S_MATR__A, src_i2s_matr, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* FM Matrix */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_WR_FM_MATRIX__A, &fm_matr, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	fm_matr &= (u16) ~AUD_DEM_WR_FM_MATRIX__M;
	switch (mixer->matrix_fm) {
	case DRX_AUD_FM_MATRIX_NO_MATRIX:
		fm_matr |= AUD_DEM_WR_FM_MATRIX_NO_MATRIX;
		break;
	case DRX_AUD_FM_MATRIX_GERMAN:
		fm_matr |= AUD_DEM_WR_FM_MATRIX_GERMAN_MATRIX;
		break;
	case DRX_AUD_FM_MATRIX_KOREAN:
		fm_matr |= AUD_DEM_WR_FM_MATRIX_KOREAN_MATRIX;
		break;
	case DRX_AUD_FM_MATRIX_SOUND_A:
		fm_matr |= AUD_DEM_WR_FM_MATRIX_SOUND_A;
		break;
	case DRX_AUD_FM_MATRIX_SOUND_B:
		fm_matr |= AUD_DEM_WR_FM_MATRIX_SOUND_B;
		break;
	default:
		return -EINVAL;
	}

	/* Only write if ASS is off */
	if (ext_attr->aud_data.auto_sound == DRX_AUD_AUTO_SOUND_OFF) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_FM_MATRIX__A, fm_matr, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* update the data structure with hardware state */
	ext_attr->aud_data.mixer = *mixer;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Set AV Sync settings
* \param demod instance of demodulator
* \param pointer to DRXICfgAVSync_t
* \return int.
*
*/
static int
aud_ctrl_set_cfg_av_sync(struct drx_demod_instance *demod, enum drx_cfg_aud_av_sync *av_sync)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 w_aud_vid_sync = 0;

	if (av_sync == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* audio/video synchronisation */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_AV_SYNC__A, &w_aud_vid_sync, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	w_aud_vid_sync &= (u16) ~AUD_DSP_WR_AV_SYNC_AV_ON__M;

	if (*av_sync == DRX_AUD_AVSYNC_OFF)
		w_aud_vid_sync |= AUD_DSP_WR_AV_SYNC_AV_ON_DISABLE;
	else
		w_aud_vid_sync |= AUD_DSP_WR_AV_SYNC_AV_ON_ENABLE;

	w_aud_vid_sync &= (u16) ~AUD_DSP_WR_AV_SYNC_AV_STD_SEL__M;

	switch (*av_sync) {
	case DRX_AUD_AVSYNC_NTSC:
		w_aud_vid_sync |= AUD_DSP_WR_AV_SYNC_AV_STD_SEL_NTSC;
		break;
	case DRX_AUD_AVSYNC_MONOCHROME:
		w_aud_vid_sync |= AUD_DSP_WR_AV_SYNC_AV_STD_SEL_MONOCHROME;
		break;
	case DRX_AUD_AVSYNC_PAL_SECAM:
		w_aud_vid_sync |= AUD_DSP_WR_AV_SYNC_AV_STD_SEL_PAL_SECAM;
		break;
	case DRX_AUD_AVSYNC_OFF:
		/* OK */
		break;
	default:
		return -EINVAL;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DSP_WR_AV_SYNC__A, w_aud_vid_sync, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get AV Sync settings
* \param demod instance of demodulator
* \param pointer to DRXICfgAVSync_t
* \return int.
*
*/
static int
aud_ctrl_get_cfg_av_sync(struct drx_demod_instance *demod, enum drx_cfg_aud_av_sync *av_sync)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 w_aud_vid_sync = 0;

	if (av_sync == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* audio/video synchronisation */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_AV_SYNC__A, &w_aud_vid_sync, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if ((w_aud_vid_sync & AUD_DSP_WR_AV_SYNC_AV_ON__M) ==
	    AUD_DSP_WR_AV_SYNC_AV_ON_DISABLE) {
		*av_sync = DRX_AUD_AVSYNC_OFF;
		return 0;
	}

	switch (w_aud_vid_sync & AUD_DSP_WR_AV_SYNC_AV_STD_SEL__M) {
	case AUD_DSP_WR_AV_SYNC_AV_STD_SEL_NTSC:
		*av_sync = DRX_AUD_AVSYNC_NTSC;
		break;
	case AUD_DSP_WR_AV_SYNC_AV_STD_SEL_MONOCHROME:
		*av_sync = DRX_AUD_AVSYNC_MONOCHROME;
		break;
	case AUD_DSP_WR_AV_SYNC_AV_STD_SEL_PAL_SECAM:
		*av_sync = DRX_AUD_AVSYNC_PAL_SECAM;
		break;
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get deviation mode
* \param demod instance of demodulator
* \param pointer to enum drx_cfg_aud_deviation * \return int.
*
*/
static int
aud_ctrl_get_cfg_dev(struct drx_demod_instance *demod, enum drx_cfg_aud_deviation *dev)
{
	u16 r_modus = 0;
	int rc;

	if (dev == NULL)
		return -EINVAL;

	rc = aud_get_modus(demod, &r_modus);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	switch (r_modus & AUD_DEM_WR_MODUS_MOD_HDEV_A__M) {
	case AUD_DEM_WR_MODUS_MOD_HDEV_A_NORMAL:
		*dev = DRX_AUD_DEVIATION_NORMAL;
		break;
	case AUD_DEM_WR_MODUS_MOD_HDEV_A_HIGH_DEVIATION:
		*dev = DRX_AUD_DEVIATION_HIGH;
		break;
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get deviation mode
* \param demod instance of demodulator
* \param pointer to enum drx_cfg_aud_deviation * \return int.
*
*/
static int
aud_ctrl_set_cfg_dev(struct drx_demod_instance *demod, enum drx_cfg_aud_deviation *dev)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 w_modus = 0;
	u16 r_modus = 0;

	if (dev == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	dev_addr = demod->my_i2c_dev_addr;

	rc = aud_get_modus(demod, &r_modus);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	w_modus = r_modus;

	w_modus &= (u16) ~AUD_DEM_WR_MODUS_MOD_HDEV_A__M;

	switch (*dev) {
	case DRX_AUD_DEVIATION_NORMAL:
		w_modus |= AUD_DEM_WR_MODUS_MOD_HDEV_A_NORMAL;
		break;
	case DRX_AUD_DEVIATION_HIGH:
		w_modus |= AUD_DEM_WR_MODUS_MOD_HDEV_A_HIGH_DEVIATION;
		break;
	default:
		return -EINVAL;
	}

	/* now update the modus register */
	if (w_modus != r_modus) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_MODUS__A, w_modus, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
	/* store in drxk data struct */
	ext_attr->aud_data.deviation = *dev;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get Prescaler settings
* \param demod instance of demodulator
* \param pointer to struct drx_cfg_aud_prescale * \return int.
*
*/
static int
aud_ctrl_get_cfg_prescale(struct drx_demod_instance *demod, struct drx_cfg_aud_prescale *presc)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 r_max_fm_deviation = 0;
	u16 r_nicam_prescaler = 0;

	if (presc == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* read register data */
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_NICAM_PRESC__A, &r_nicam_prescaler, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DSP_WR_FM_PRESC__A, &r_max_fm_deviation, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* calculate max FM deviation */
	r_max_fm_deviation >>= AUD_DSP_WR_FM_PRESC_FM_AM_PRESC__B;
	if (r_max_fm_deviation > 0) {
		presc->fm_deviation = 3600UL + (r_max_fm_deviation >> 1);
		presc->fm_deviation /= r_max_fm_deviation;
	} else {
		presc->fm_deviation = 380;	/* kHz */
	}

	/* calculate NICAM gain from pre-scaler */
	/*
	   nicam_gain   = 20 * ( log10( preScaler / 16) )
	   = ( 100log10( preScaler ) - 100log10( 16 ) ) / 5

	   because log1_times100() cannot return negative numbers
	   = ( 100log10( 10 * preScaler ) - 100log10( 10 * 16) ) / 5

	   for 0.1dB resolution:

	   nicam_gain   = 200 * ( log10( preScaler / 16) )
	   = 2 * ( 100log10( 10 * preScaler ) - 100log10( 10 * 16) )
	   = ( 100log10( 10 * preScaler^2 ) - 100log10( 10 * 16^2 ) )

	 */
	r_nicam_prescaler >>= 8;
	if (r_nicam_prescaler <= 1)
		presc->nicam_gain = -241;
	else
		presc->nicam_gain = (s16)(((s32)(log1_times100(10 * r_nicam_prescaler * r_nicam_prescaler)) - (s32)(log1_times100(10 * 16 * 16))));

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Set Prescaler settings
* \param demod instance of demodulator
* \param pointer to struct drx_cfg_aud_prescale * \return int.
*
*/
static int
aud_ctrl_set_cfg_prescale(struct drx_demod_instance *demod, struct drx_cfg_aud_prescale *presc)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 w_max_fm_deviation = 0;
	u16 nicam_prescaler;

	if (presc == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* setting of max FM deviation */
	w_max_fm_deviation = (u16) (frac(3600UL, presc->fm_deviation, 0));
	w_max_fm_deviation <<= AUD_DSP_WR_FM_PRESC_FM_AM_PRESC__B;
	if (w_max_fm_deviation >= AUD_DSP_WR_FM_PRESC_FM_AM_PRESC_28_KHZ_FM_DEVIATION)
		w_max_fm_deviation = AUD_DSP_WR_FM_PRESC_FM_AM_PRESC_28_KHZ_FM_DEVIATION;

	/* NICAM Prescaler */
	if ((presc->nicam_gain >= -241) && (presc->nicam_gain <= 180)) {
		/* calculation

		   prescaler = 16 * 10^( gd_b / 20 )

		   minval of gd_b = -20*log( 16 ) = -24.1dB

		   negative numbers not allowed for d_b2lin_times100, so

		   prescaler = 16 * 10^( gd_b / 20 )
		   = 10^( (gd_b / 20) + log10(16) )
		   = 10^( (gd_b + 20log10(16)) / 20 )

		   in 0.1dB

		   = 10^( G0.1dB + 200log10(16)) / 200 )

		 */
		nicam_prescaler = (u16)
		    ((d_b2lin_times100(presc->nicam_gain + 241UL) + 50UL) / 100UL);

		/* clip result */
		if (nicam_prescaler > 127)
			nicam_prescaler = 127;

		/* shift before writing to register */
		nicam_prescaler <<= 8;
	} else {
		return -EINVAL;
	}
	/* end of setting NICAM Prescaler */

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DSP_WR_NICAM_PRESC__A, nicam_prescaler, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DSP_WR_FM_PRESC__A, w_max_fm_deviation, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	ext_attr->aud_data.prescale = *presc;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Beep
* \param demod instance of demodulator
* \param pointer to struct drx_aud_beep * \return int.
*
*/
static int aud_ctrl_beep(struct drx_demod_instance *demod, struct drx_aud_beep *beep)
{
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	int rc;
	u16 the_beep = 0;
	u16 volume = 0;
	u32 frequency = 0;

	if (beep == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	if ((beep->volume > 0) || (beep->volume < -127))
		return -EINVAL;

	if (beep->frequency > 3000)
		return -EINVAL;

	volume = (u16) beep->volume + 127;
	the_beep |= volume << AUD_DSP_WR_BEEPER_BEEP_VOLUME__B;

	frequency = ((u32) beep->frequency) * 23 / 500;
	if (frequency > AUD_DSP_WR_BEEPER_BEEP_FREQUENCY__M)
		frequency = AUD_DSP_WR_BEEPER_BEEP_FREQUENCY__M;
	the_beep |= (u16) frequency;

	if (beep->mute == true)
		the_beep = 0;

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DSP_WR_BEEPER__A, the_beep, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Set an audio standard
* \param demod instance of demodulator
* \param pointer to enum drx_aud_standard * \return int.
*
*/
static int
aud_ctrl_set_standard(struct drx_demod_instance *demod, enum drx_aud_standard *standard)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	enum drx_standard current_standard = DRX_STANDARD_UNKNOWN;
	int rc;
	u16 w_standard = 0;
	u16 w_modus = 0;
	u16 r_modus = 0;

	bool mute_buffer = false;
	s16 volume_buffer = 0;
	u16 w_volume = 0;

	if (standard == NULL)
		return -EINVAL;

	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	/* reset RDS data availability flag */
	ext_attr->aud_data.rds_data_present = false;

	/* we need to mute from here to avoid noise during standard switching */
	mute_buffer = ext_attr->aud_data.volume.mute;
	volume_buffer = ext_attr->aud_data.volume.volume;

	ext_attr->aud_data.volume.mute = true;
	/* restore data structure from DRX ExtAttr, call volume first to mute */
	rc = aud_ctrl_set_cfg_volume(demod, &ext_attr->aud_data.volume);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = aud_ctrl_set_cfg_carrier(demod, &ext_attr->aud_data.carriers);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = aud_ctrl_set_cfg_ass_thres(demod, &ext_attr->aud_data.ass_thresholds);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = aud_ctr_setl_cfg_auto_sound(demod, &ext_attr->aud_data.auto_sound);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = aud_ctrl_set_cfg_mixer(demod, &ext_attr->aud_data.mixer);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = aud_ctrl_set_cfg_av_sync(demod, &ext_attr->aud_data.av_sync);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = aud_ctrl_set_cfg_output_i2s(demod, &ext_attr->aud_data.i2sdata);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* get prescaler from presets */
	rc = aud_ctrl_set_cfg_prescale(demod, &ext_attr->aud_data.prescale);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = aud_get_modus(demod, &r_modus);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	w_modus = r_modus;

	switch (*standard) {
	case DRX_AUD_STANDARD_AUTO:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_AUTO;
		break;
	case DRX_AUD_STANDARD_BTSC:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_BTSC_STEREO;
		if (ext_attr->aud_data.btsc_detect == DRX_BTSC_MONO_AND_SAP)
			w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_BTSC_SAP;
		break;
	case DRX_AUD_STANDARD_A2:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_M_KOREA;
		break;
	case DRX_AUD_STANDARD_EIAJ:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_EIA_J;
		break;
	case DRX_AUD_STANDARD_FM_STEREO:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_FM_RADIO;
		break;
	case DRX_AUD_STANDARD_BG_FM:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_BG_FM;
		break;
	case DRX_AUD_STANDARD_D_K1:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_D_K1;
		break;
	case DRX_AUD_STANDARD_D_K2:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_D_K2;
		break;
	case DRX_AUD_STANDARD_D_K3:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_D_K3;
		break;
	case DRX_AUD_STANDARD_BG_NICAM_FM:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_BG_NICAM_FM;
		break;
	case DRX_AUD_STANDARD_L_NICAM_AM:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_L_NICAM_AM;
		break;
	case DRX_AUD_STANDARD_I_NICAM_FM:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_I_NICAM_FM;
		break;
	case DRX_AUD_STANDARD_D_K_NICAM_FM:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_D_K_NICAM_FM;
		break;
	case DRX_AUD_STANDARD_UNKNOWN:
		w_standard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_AUTO;
		break;
	default:
		return -EIO;
	}

	if (*standard == DRX_AUD_STANDARD_AUTO) {
		/* we need the current standard here */
		current_standard = ext_attr->standard;

		w_modus &= (u16) ~AUD_DEM_WR_MODUS_MOD_6_5MHZ__M;

		if ((current_standard == DRX_STANDARD_PAL_SECAM_L) || (current_standard == DRX_STANDARD_PAL_SECAM_LP))
			w_modus |= (AUD_DEM_WR_MODUS_MOD_6_5MHZ_SECAM);
		else
			w_modus |= (AUD_DEM_WR_MODUS_MOD_6_5MHZ_D_K);

		w_modus &= (u16) ~AUD_DEM_WR_MODUS_MOD_4_5MHZ__M;
		if (current_standard == DRX_STANDARD_NTSC)
			w_modus |= (AUD_DEM_WR_MODUS_MOD_4_5MHZ_M_BTSC);
		else
			w_modus |= (AUD_DEM_WR_MODUS_MOD_4_5MHZ_CHROMA);

	}

	w_modus &= (u16) ~AUD_DEM_WR_MODUS_MOD_FMRADIO__M;

	/* just get hardcoded deemphasis and activate here */
	if (ext_attr->aud_data.deemph == DRX_AUD_FM_DEEMPH_50US)
		w_modus |= (AUD_DEM_WR_MODUS_MOD_FMRADIO_EU_50U);
	else
		w_modus |= (AUD_DEM_WR_MODUS_MOD_FMRADIO_US_75U);

	w_modus &= (u16) ~AUD_DEM_WR_MODUS_MOD_BTSC__M;
	if (ext_attr->aud_data.btsc_detect == DRX_BTSC_STEREO)
		w_modus |= (AUD_DEM_WR_MODUS_MOD_BTSC_BTSC_STEREO);
	else
		w_modus |= (AUD_DEM_WR_MODUS_MOD_BTSC_BTSC_SAP);

	if (w_modus != r_modus) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_MODUS__A, w_modus, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DEM_WR_STANDARD_SEL__A, w_standard, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

   /**************************************************************************/
	/* NOT calling aud_ctrl_set_cfg_volume to avoid interfering standard          */
	/* detection, need to keep things very minimal here, but keep audio       */
	/* buffers intact                                                         */
   /**************************************************************************/
	ext_attr->aud_data.volume.mute = mute_buffer;
	if (ext_attr->aud_data.volume.mute == false) {
		w_volume |= (u16) ((volume_buffer + AUD_VOLUME_ZERO_DB) <<
				    AUD_DSP_WR_VOLUME_VOL_MAIN__B);
		rc = DRXJ_DAP.write_reg16func(dev_addr, AUD_DSP_WR_VOLUME__A, w_volume, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* write standard selected */
	ext_attr->aud_data.audio_standard = *standard;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief Get the current audio standard
* \param demod instance of demodulator
* \param pointer to enum drx_aud_standard * \return int.
*
*/
static int
aud_ctrl_get_standard(struct drx_demod_instance *demod, enum drx_aud_standard *standard)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;
	u16 r_data = 0;

	if (standard == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	dev_addr = (struct i2c_device_addr *)demod->my_i2c_dev_addr;

	/* power up */
	if (ext_attr->aud_data.audio_is_active == false) {
		rc = power_up_aud(demod, true);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->aud_data.audio_is_active = true;
	}

	*standard = DRX_AUD_STANDARD_UNKNOWN;

	rc = DRXJ_DAP.read_reg16func(dev_addr, AUD_DEM_RD_STANDARD_RES__A, &r_data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* return OK if the detection is not ready yet */
	if (r_data >= AUD_DEM_RD_STANDARD_RES_STD_RESULT_DETECTION_STILL_ACTIVE) {
		*standard = DRX_AUD_STANDARD_NOT_READY;
		return 0;
	}

	/* detection done, return correct standard */
	switch (r_data) {
		/* no standard detected */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_NO_SOUND_STANDARD:
		*standard = DRX_AUD_STANDARD_UNKNOWN;
		break;
		/* standard is KOREA(A2) */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_NTSC_M_DUAL_CARRIER_FM:
		*standard = DRX_AUD_STANDARD_A2;
		break;
		/* standard is EIA-J (Japan) */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_NTSC_EIA_J:
		*standard = DRX_AUD_STANDARD_EIAJ;
		break;
		/* standard is BTSC-stereo */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_BTSC_STEREO:
		*standard = DRX_AUD_STANDARD_BTSC;
		break;
		/* standard is BTSC-mono (SAP) */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_BTSC_MONO_SAP:
		*standard = DRX_AUD_STANDARD_BTSC;
		break;
		/* standard is FM radio */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_FM_RADIO:
		*standard = DRX_AUD_STANDARD_FM_STEREO;
		break;
		/* standard is BG FM */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_B_G_DUAL_CARRIER_FM:
		*standard = DRX_AUD_STANDARD_BG_FM;
		break;
		/* standard is DK-1 FM */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_D_K1_DUAL_CARRIER_FM:
		*standard = DRX_AUD_STANDARD_D_K1;
		break;
		/* standard is DK-2 FM */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_D_K2_DUAL_CARRIER_FM:
		*standard = DRX_AUD_STANDARD_D_K2;
		break;
		/* standard is DK-3 FM */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_D_K3_DUAL_CARRIER_FM:
		*standard = DRX_AUD_STANDARD_D_K3;
		break;
		/* standard is BG-NICAM FM */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_B_G_NICAM_FM:
		*standard = DRX_AUD_STANDARD_BG_NICAM_FM;
		break;
		/* standard is L-NICAM AM */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_L_NICAM_AM:
		*standard = DRX_AUD_STANDARD_L_NICAM_AM;
		break;
		/* standard is I-NICAM FM */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_I_NICAM_FM:
		*standard = DRX_AUD_STANDARD_I_NICAM_FM;
		break;
		/* standard is DK-NICAM FM */
	case AUD_DEM_RD_STANDARD_RES_STD_RESULT_D_K_NICAM_FM:
		*standard = DRX_AUD_STANDARD_D_K_NICAM_FM;
		break;
	default:
		*standard = DRX_AUD_STANDARD_UNKNOWN;
	}

	return 0;
rw_error:
	return -EIO;

}

/*============================================================================*/
/**
* \brief Retreive lock status in case of FM standard
* \param demod instance of demodulator
* \param pointer to lock status
* \return int.
*
*/
static int
fm_lock_status(struct drx_demod_instance *demod, enum drx_lock_status *lock_stat)
{
	struct drx_aud_status status;
	int rc;

	/* Check detection of audio carriers */
	rc = aud_ctrl_get_carrier_detect_status(demod, &status);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* locked if either primary or secondary carrier is detected */
	if ((status.carrier_a == true) || (status.carrier_b == true))
		*lock_stat = DRX_LOCKED;
	else
		*lock_stat = DRX_NOT_LOCKED;

	return 0;

rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \brief retreive signal quality in case of FM standard
* \param demod instance of demodulator
* \param pointer to signal quality
* \return int.
*
* Only the quality indicator field is will be supplied.
* This will either be 0% or 100%, nothing in between.
*
*/
static int
fm_sig_quality(struct drx_demod_instance *demod, struct drx_sig_quality *sig_quality)
{
	enum drx_lock_status lock_status = DRX_NOT_LOCKED;
	int rc;

	rc = fm_lock_status(demod, &lock_status);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (lock_status == DRX_LOCKED)
		sig_quality->indicator = 100;
	else
		sig_quality->indicator = 0;

	return 0;

rw_error:
	return -EIO;
}

#endif

/*===========================================================================*/
/*==                    END AUDIO DATAPATH FUNCTIONS                       ==*/
/*===========================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                       OOB DATAPATH FUNCTIONS                           ==*/
/*============================================================================*/
/*============================================================================*/
#ifndef DRXJ_DIGITAL_ONLY
/**
* \fn int get_oob_lock_status ()
* \brief Get OOB lock status.
* \param dev_addr I2C address
  \      oob_lock OOB lock status.
* \return int.
*
* Gets OOB lock status
*
*/
static int
get_oob_lock_status(struct drx_demod_instance *demod,
		    struct i2c_device_addr *dev_addr, enum drx_lock_status *oob_lock)
{
	struct drxjscu_cmd scu_cmd;
	int rc;
	u16 cmd_result[2];
	u16 oob_lock_state;

	*oob_lock = DRX_NOT_LOCKED;

	scu_cmd.command = SCU_RAM_COMMAND_STANDARD_OOB |
	    SCU_RAM_COMMAND_CMD_DEMOD_GET_LOCK;
	scu_cmd.result_len = 2;
	scu_cmd.result = cmd_result;
	scu_cmd.parameter_len = 0;

	rc = scu_command(dev_addr, &scu_cmd);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (scu_cmd.result[1] < 0x4000) {
		/* 0x00 NOT LOCKED */
		*oob_lock = DRX_NOT_LOCKED;
	} else if (scu_cmd.result[1] < 0x8000) {
		/* 0x40 DEMOD LOCKED */
		*oob_lock = DRXJ_OOB_SYNC_LOCK;
	} else if (scu_cmd.result[1] < 0xC000) {
		/* 0x80 DEMOD + OOB LOCKED (system lock) */
		oob_lock_state = scu_cmd.result[1] & 0x00FF;

		if (oob_lock_state & 0x0008)
			*oob_lock = DRXJ_OOB_SYNC_LOCK;
		else if ((oob_lock_state & 0x0002) && (oob_lock_state & 0x0001))
			*oob_lock = DRXJ_OOB_AGC_LOCK;
	} else {
		/* 0xC0 NEVER LOCKED (system will never be able to lock to the signal) */
		*oob_lock = DRX_NEVER_LOCK;
	}

	/* *oob_lock = scu_cmd.result[1]; */

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int get_oob_symbol_rate_offset ()
* \brief Get OOB Symbol rate offset. Unit is [ppm]
* \param dev_addr I2C address
* \      Symbol Rate Offset OOB parameter.
* \return int.
*
* Gets OOB frequency offset
*
*/
static int
get_oob_symbol_rate_offset(struct i2c_device_addr *dev_addr, s32 *symbol_rate_offset)
{
/*  offset = -{(timing_offset/2^19)*(symbol_rate/12,656250MHz)}*10^6 [ppm]  */
/*  offset = -{(timing_offset/2^19)*(symbol_rate/12656250)}*10^6 [ppm]  */
/*  after reconfiguration: */
/*  offset = -{(timing_offset*symbol_rate)/(2^19*12656250)}*10^6 [ppm]  */
/*  shift symbol rate left by 5 without lossing information */
/*  offset = -{(timing_offset*(symbol_rate * 2^-5))/(2^14*12656250)}*10^6 [ppm]*/
/*  shift 10^6 left by 6 without loosing information */
/*  offset = -{(timing_offset*(symbol_rate * 2^-5))/(2^8*12656250)}*15625 [ppm]*/
/*  trim 12656250/15625 = 810 */
/*  offset = -{(timing_offset*(symbol_rate * 2^-5))/(2^8*810)} [ppm]  */
/*  offset = -[(symbol_rate * 2^-5)*(timing_offset)/(2^8)]/810 [ppm]  */
	int rc;
	s32 timing_offset = 0;
	u32 unsigned_timing_offset = 0;
	s32 division_factor = 810;
	u16 data = 0;
	u32 symbol_rate = 0;
	bool negative = false;

	*symbol_rate_offset = 0;
	/* read data rate */
	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ORX_RF_RX_DATA_RATE__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	switch (data & SCU_RAM_ORX_RF_RX_DATA_RATE__M) {
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC_ALT:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC_ALT:
		symbol_rate = 1024000;	/* bps */
		break;
	case SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_INVSPEC:
		symbol_rate = 772000;	/* bps */
		break;
	case SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_INVSPEC:
		symbol_rate = 1544000;	/* bps */
		break;
	default:
		return -EIO;
	}

	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_CON_CTI_DTI_R__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* convert data to positive and keep information about sign */
	if ((data & 0x8000) == 0x8000) {
		if (data == 0x8000)
			unsigned_timing_offset = 32768;
		else
			unsigned_timing_offset = 0x00007FFF & (u32) (-data);
		negative = true;
	} else
		unsigned_timing_offset = (u32) data;

	symbol_rate = symbol_rate >> 5;
	unsigned_timing_offset = (unsigned_timing_offset * symbol_rate);
	unsigned_timing_offset = frac(unsigned_timing_offset, 256, FRAC_ROUND);
	unsigned_timing_offset = frac(unsigned_timing_offset,
				    division_factor, FRAC_ROUND);
	if (negative)
		timing_offset = (s32) unsigned_timing_offset;
	else
		timing_offset = -(s32) unsigned_timing_offset;

	*symbol_rate_offset = timing_offset;

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int get_oob_freq_offset ()
* \brief Get OOB lock status.
* \param dev_addr I2C address
* \      freq_offset OOB frequency offset.
* \return int.
*
* Gets OOB frequency offset
*
*/
static int
get_oob_freq_offset(struct drx_demod_instance *demod, s32 *freq_offset)
{
	struct drx_common_attr *common_attr = (struct drx_common_attr *) (NULL);
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	u16 data = 0;
	u16 rot = 0;
	u16 symbol_rate_reg = 0;
	u32 symbol_rate = 0;
	s32 coarse_freq_offset = 0;
	s32 fine_freq_offset = 0;
	s32 fine_sign = 1;
	s32 coarse_sign = 1;
	u32 data64hi = 0;
	u32 data64lo = 0;
	u32 temp_freq_offset = 0;

	/* check arguments */
	if ((demod == NULL) || (freq_offset == NULL))
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;

	*freq_offset = 0;

	/* read sign (spectrum inversion) */
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_FWP_IQM_FRQ_W__A, &rot, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* read frequency offset */
	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ORX_FRQ_OFFSET__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* find COARSE frequency offset */
	/* coarse_freq_offset = ( 25312500Hz*FRQ_OFFSET >> 21 ); */
	if (data & 0x8000) {
		data = (0xffff - data + 1);
		coarse_sign = -1;
	}
	mult32(data, (common_attr->sys_clock_freq * 1000) / 6, &data64hi,
	       &data64lo);
	temp_freq_offset = (((data64lo >> 21) & 0x7ff) | (data64hi << 11));

	/* get value in KHz */
	coarse_freq_offset = coarse_sign * frac(temp_freq_offset, 1000, FRAC_ROUND);	/* KHz */
	/* read data rate */
	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ORX_RF_RX_DATA_RATE__A, &symbol_rate_reg, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	switch (symbol_rate_reg & SCU_RAM_ORX_RF_RX_DATA_RATE__M) {
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC_ALT:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC_ALT:
		symbol_rate = 1024000;
		break;
	case SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_INVSPEC:
		symbol_rate = 772000;
		break;
	case SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_INVSPEC:
		symbol_rate = 1544000;
		break;
	default:
		return -EIO;
	}

	/* find FINE frequency offset */
	/* fine_freq_offset = ( (CORRECTION_VALUE*symbol_rate) >> 18 ); */
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_CON_CPH_FRQ_R__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* at least 5 MSB are 0 so first divide with 2^5 without information loss */
	fine_freq_offset = (symbol_rate >> 5);
	if (data & 0x8000) {
		fine_freq_offset *= 0xffff - data + 1;	/* Hz */
		fine_sign = -1;
	} else {
		fine_freq_offset *= data;	/* Hz */
	}
	/* Left to divide with 8192 (2^13) */
	fine_freq_offset = frac(fine_freq_offset, 8192, FRAC_ROUND);
	/* and to divide with 1000 to get KHz */
	fine_freq_offset = fine_sign * frac(fine_freq_offset, 1000, FRAC_ROUND);	/* KHz */

	if ((rot & 0x8000) == 0x8000)
		*freq_offset = -(coarse_freq_offset + fine_freq_offset);
	else
		*freq_offset = (coarse_freq_offset + fine_freq_offset);

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int get_oob_frequency ()
* \brief Get OOB frequency (Unit:KHz).
* \param dev_addr I2C address
* \      frequency OOB frequency parameters.
* \return int.
*
* Gets OOB frequency
*
*/
static int
get_oob_frequency(struct drx_demod_instance *demod, s32 *frequency)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	u16 data = 0;
	s32 freq_offset = 0;
	s32 freq = 0;

	dev_addr = demod->my_i2c_dev_addr;

	*frequency = 0;		/* KHz */

	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ORX_RF_RX_FREQUENCY_VALUE__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	freq = (s32) ((s32) data * 50 + 50000L);

	rc = get_oob_freq_offset(demod, &freq_offset);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	*frequency = freq + freq_offset;

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int get_oobmer ()
* \brief Get OOB MER.
* \param dev_addr I2C address
  \      MER OOB parameter in dB.
* \return int.
*
* Gets OOB MER. Table for MER is in Programming guide.
*
*/
static int get_oobmer(struct i2c_device_addr *dev_addr, u32 *mer)
{
	int rc;
	u16 data = 0;

	*mer = 0;
	/* READ MER */
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_EQU_MER_MER_R__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	switch (data) {
	case 0:		/* fall through */
	case 1:
		*mer = 39;
		break;
	case 2:
		*mer = 33;
		break;
	case 3:
		*mer = 29;
		break;
	case 4:
		*mer = 27;
		break;
	case 5:
		*mer = 25;
		break;
	case 6:
		*mer = 23;
		break;
	case 7:
		*mer = 22;
		break;
	case 8:
		*mer = 21;
		break;
	case 9:
		*mer = 20;
		break;
	case 10:
		*mer = 19;
		break;
	case 11:
		*mer = 18;
		break;
	case 12:
		*mer = 17;
		break;
	case 13:		/* fall through */
	case 14:
		*mer = 16;
		break;
	case 15:		/* fall through */
	case 16:
		*mer = 15;
		break;
	case 17:		/* fall through */
	case 18:
		*mer = 14;
		break;
	case 19:		/* fall through */
	case 20:
		*mer = 13;
		break;
	case 21:		/* fall through */
	case 22:
		*mer = 12;
		break;
	case 23:		/* fall through */
	case 24:		/* fall through */
	case 25:
		*mer = 11;
		break;
	case 26:		/* fall through */
	case 27:		/* fall through */
	case 28:
		*mer = 10;
		break;
	case 29:		/* fall through */
	case 30:		/* fall through */
	case 31:		/* fall through */
	case 32:
		*mer = 9;
		break;
	case 33:		/* fall through */
	case 34:		/* fall through */
	case 35:		/* fall through */
	case 36:
		*mer = 8;
		break;
	case 37:		/* fall through */
	case 38:		/* fall through */
	case 39:		/* fall through */
	case 40:
		*mer = 7;
		break;
	case 41:		/* fall through */
	case 42:		/* fall through */
	case 43:		/* fall through */
	case 44:		/* fall through */
	case 45:
		*mer = 6;
		break;
	case 46:		/* fall through */
	case 47:		/* fall through */
	case 48:		/* fall through */
	case 49:		/* fall through */
	case 50:		/* fall through */
		*mer = 5;
		break;
	case 51:		/* fall through */
	case 52:		/* fall through */
	case 53:		/* fall through */
	case 54:		/* fall through */
	case 55:		/* fall through */
	case 56:		/* fall through */
	case 57:
		*mer = 4;
		break;
	case 58:		/* fall through */
	case 59:		/* fall through */
	case 60:		/* fall through */
	case 61:		/* fall through */
	case 62:		/* fall through */
	case 63:
		*mer = 0;
		break;
	default:
		*mer = 0;
		break;
	}
	return 0;
rw_error:
	return -EIO;
}
#endif /*#ifndef DRXJ_DIGITAL_ONLY */

/**
* \fn int set_orx_nsu_aox()
* \brief Configure OrxNsuAox for OOB
* \param demod instance of demodulator.
* \param active
* \return int.
*/
static int set_orx_nsu_aox(struct drx_demod_instance *demod, bool active)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	u16 data = 0;

	/* Configure NSU_AOX */
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_NSU_AOX_STDBY_W__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (!active)
		data &= ((~ORX_NSU_AOX_STDBY_W_STDBYADC_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYAMP_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYBIAS_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYPLL_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYPD_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYTAGC_IF_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYTAGC_RF_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYFLT_A2_ON));
	else
		data |= (ORX_NSU_AOX_STDBY_W_STDBYADC_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYAMP_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYBIAS_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYPLL_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYPD_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYTAGC_IF_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYTAGC_RF_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYFLT_A2_ON);
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_NSU_AOX_STDBY_W__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int ctrl_set_oob()
* \brief Set OOB channel to be used.
* \param demod instance of demodulator
* \param oob_param OOB parameters for channel setting.
* \frequency should be in KHz
* \return int.
*
* Accepts  only. Returns error otherwise.
* Demapper value is written after scu_command START
* because START command causes COMM_EXEC transition
* from 0 to 1 which causes all registers to be
* overwritten with initial value
*
*/

/* Nyquist filter impulse response */
#define IMPULSE_COSINE_ALPHA_0_3    {-3, -4, -1, 6, 10, 7, -5, -20, -25, -10, 29, 79, 123, 140}	/*sqrt raised-cosine filter with alpha=0.3 */
#define IMPULSE_COSINE_ALPHA_0_5    { 2, 0, -2, -2, 2, 5, 2, -10, -20, -14, 20, 74, 125, 145}	/*sqrt raised-cosine filter with alpha=0.5 */
#define IMPULSE_COSINE_ALPHA_RO_0_5 { 0, 0, 1, 2, 3, 0, -7, -15, -16,  0, 34, 77, 114, 128}	/*full raised-cosine filter with alpha=0.5 (receiver only) */

/* Coefficients for the nyquist fitler (total: 27 taps) */
#define NYQFILTERLEN 27

static int ctrl_set_oob(struct drx_demod_instance *demod, struct drxoob *oob_param)
{
#ifndef DRXJ_DIGITAL_ONLY
	int rc;
	s32 freq = 0;	/* KHz */
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	u16 i = 0;
	bool mirror_freq_spect_oob = false;
	u16 trk_filter_value = 0;
	struct drxjscu_cmd scu_cmd;
	u16 set_param_parameters[3];
	u16 cmd_result[2] = { 0, 0 };
	s16 nyquist_coeffs[4][(NYQFILTERLEN + 1) / 2] = {
		IMPULSE_COSINE_ALPHA_0_3,	/* Target Mode 0 */
		IMPULSE_COSINE_ALPHA_0_3,	/* Target Mode 1 */
		IMPULSE_COSINE_ALPHA_0_5,	/* Target Mode 2 */
		IMPULSE_COSINE_ALPHA_RO_0_5	/* Target Mode 3 */
	};
	u8 mode_val[4] = { 2, 2, 0, 1 };
	u8 pfi_coeffs[4][6] = {
		{DRXJ_16TO8(-92), DRXJ_16TO8(-108), DRXJ_16TO8(100)},	/* TARGET_MODE = 0:     PFI_A = -23/32; PFI_B = -54/32;  PFI_C = 25/32; fg = 0.5 MHz (Att=26dB) */
		{DRXJ_16TO8(-64), DRXJ_16TO8(-80), DRXJ_16TO8(80)},	/* TARGET_MODE = 1:     PFI_A = -16/32; PFI_B = -40/32;  PFI_C = 20/32; fg = 1.0 MHz (Att=28dB) */
		{DRXJ_16TO8(-80), DRXJ_16TO8(-98), DRXJ_16TO8(92)},	/* TARGET_MODE = 2, 3:  PFI_A = -20/32; PFI_B = -49/32;  PFI_C = 23/32; fg = 0.8 MHz (Att=25dB) */
		{DRXJ_16TO8(-80), DRXJ_16TO8(-98), DRXJ_16TO8(92)}	/* TARGET_MODE = 2, 3:  PFI_A = -20/32; PFI_B = -49/32;  PFI_C = 23/32; fg = 0.8 MHz (Att=25dB) */
	};
	u16 mode_index;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	mirror_freq_spect_oob = ext_attr->mirror_freq_spect_oob;

	/* Check parameters */
	if (oob_param == NULL) {
		/* power off oob module  */
		scu_cmd.command = SCU_RAM_COMMAND_STANDARD_OOB
		    | SCU_RAM_COMMAND_CMD_DEMOD_STOP;
		scu_cmd.parameter_len = 0;
		scu_cmd.result_len = 1;
		scu_cmd.result = cmd_result;
		rc = scu_command(dev_addr, &scu_cmd);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = set_orx_nsu_aox(demod, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_COMM_EXEC__A, ORX_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		ext_attr->oob_power_on = false;
		return 0;
	}

	freq = oob_param->frequency;
	if ((freq < 70000) || (freq > 130000))
		return -EIO;
	freq = (freq - 50000) / 50;

	{
		u16 index = 0;
		u16 remainder = 0;
		u16 *trk_filtercfg = ext_attr->oob_trk_filter_cfg;

		index = (u16) ((freq - 400) / 200);
		remainder = (u16) ((freq - 400) % 200);
		trk_filter_value =
		    trk_filtercfg[index] - (trk_filtercfg[index] -
					   trk_filtercfg[index +
							1]) / 10 * remainder /
		    20;
	}

   /*********/
	/* Stop  */
   /*********/
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_COMM_EXEC__A, ORX_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	scu_cmd.command = SCU_RAM_COMMAND_STANDARD_OOB
	    | SCU_RAM_COMMAND_CMD_DEMOD_STOP;
	scu_cmd.parameter_len = 0;
	scu_cmd.result_len = 1;
	scu_cmd.result = cmd_result;
	rc = scu_command(dev_addr, &scu_cmd);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
   /*********/
	/* Reset */
   /*********/
	scu_cmd.command = SCU_RAM_COMMAND_STANDARD_OOB
	    | SCU_RAM_COMMAND_CMD_DEMOD_RESET;
	scu_cmd.parameter_len = 0;
	scu_cmd.result_len = 1;
	scu_cmd.result = cmd_result;
	rc = scu_command(dev_addr, &scu_cmd);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
   /***********/
	/* SET_ENV */
   /***********/
	/* set frequency, spectrum inversion and data rate */
	scu_cmd.command = SCU_RAM_COMMAND_STANDARD_OOB
	    | SCU_RAM_COMMAND_CMD_DEMOD_SET_ENV;
	scu_cmd.parameter_len = 3;
	/* 1-data rate;2-frequency */
	switch (oob_param->standard) {
	case DRX_OOB_MODE_A:
		if (
			   /* signal is transmitted inverted */
			   ((oob_param->spectrum_inverted == true) &
			    /* and tuner is not mirroring the signal */
			    (!mirror_freq_spect_oob)) |
			   /* or */
			   /* signal is transmitted noninverted */
			   ((oob_param->spectrum_inverted == false) &
			    /* and tuner is mirroring the signal */
			    (mirror_freq_spect_oob))
		    )
			set_param_parameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC;
		else
			set_param_parameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC;
		break;
	case DRX_OOB_MODE_B_GRADE_A:
		if (
			   /* signal is transmitted inverted */
			   ((oob_param->spectrum_inverted == true) &
			    /* and tuner is not mirroring the signal */
			    (!mirror_freq_spect_oob)) |
			   /* or */
			   /* signal is transmitted noninverted */
			   ((oob_param->spectrum_inverted == false) &
			    /* and tuner is mirroring the signal */
			    (mirror_freq_spect_oob))
		    )
			set_param_parameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_INVSPEC;
		else
			set_param_parameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_REGSPEC;
		break;
	case DRX_OOB_MODE_B_GRADE_B:
	default:
		if (
			   /* signal is transmitted inverted */
			   ((oob_param->spectrum_inverted == true) &
			    /* and tuner is not mirroring the signal */
			    (!mirror_freq_spect_oob)) |
			   /* or */
			   /* signal is transmitted noninverted */
			   ((oob_param->spectrum_inverted == false) &
			    /* and tuner is mirroring the signal */
			    (mirror_freq_spect_oob))
		    )
			set_param_parameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_INVSPEC;
		else
			set_param_parameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_REGSPEC;
		break;
	}
	set_param_parameters[1] = (u16) (freq & 0xFFFF);
	set_param_parameters[2] = trk_filter_value;
	scu_cmd.parameter = set_param_parameters;
	scu_cmd.result_len = 1;
	scu_cmd.result = cmd_result;
	mode_index = mode_val[(set_param_parameters[0] & 0xC0) >> 6];
	rc = scu_command(dev_addr, &scu_cmd);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, 0xFABA, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/*  Write magic word to enable pdr reg write  */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_OOB_CRX_CFG__A, OOB_CRX_DRIVE_STRENGTH << SIO_PDR_OOB_CRX_CFG_DRIVE__B | 0x03 << SIO_PDR_OOB_CRX_CFG_MODE__B, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_PDR_OOB_DRX_CFG__A, OOB_DRX_DRIVE_STRENGTH << SIO_PDR_OOB_DRX_CFG_DRIVE__B | 0x03 << SIO_PDR_OOB_DRX_CFG_MODE__B, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/*  Write magic word to disable pdr reg write */

	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_TOP_COMM_KEY__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_FWP_AAG_LEN_W__A, 16000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_FWP_AAG_THR_W__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* ddc */
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_DDC_OFO_SET_W__A, ORX_DDC_OFO_SET_W__PRE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* nsu */
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_NSU_AOX_LOPOW_W__A, ext_attr->oob_lo_pow, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* initialization for target mode */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_TARGET_MODE__A, SCU_RAM_ORX_TARGET_MODE_2048KBPS_SQRT, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_FREQ_GAIN_CORR__A, SCU_RAM_ORX_FREQ_GAIN_CORR_2048KBPS, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Reset bits for timing and freq. recovery */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_RST_CPH__A, 0x0001, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_RST_CTI__A, 0x0002, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_RST_KRN__A, 0x0004, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_RST_KRP__A, 0x0008, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* AGN_LOCK = {2048>>3, -2048, 8, -8, 0, 1}; */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_AGN_LOCK_TH__A, 2048 >> 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_AGN_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_AGN_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_AGN_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_AGN_LOCK_MASK__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* DGN_LOCK = {10, -2048, 8, -8, 0, 1<<1}; */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_DGN_LOCK_TH__A, 10, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_DGN_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_DGN_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_DGN_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_DGN_LOCK_MASK__A, 1 << 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* FRQ_LOCK = {15,-2048, 8, -8, 0, 1<<2}; */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_FRQ_LOCK_TH__A, 17, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_FRQ_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_FRQ_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_FRQ_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_FRQ_LOCK_MASK__A, 1 << 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* PHA_LOCK = {5000, -2048, 8, -8, 0, 1<<3}; */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_PHA_LOCK_TH__A, 3000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_PHA_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_PHA_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_PHA_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_PHA_LOCK_MASK__A, 1 << 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* TIM_LOCK = {300,      -2048, 8, -8, 0, 1<<4}; */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_TIM_LOCK_TH__A, 400, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_TIM_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_TIM_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_TIM_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_TIM_LOCK_MASK__A, 1 << 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* EQU_LOCK = {20,      -2048, 8, -8, 0, 1<<5}; */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_EQU_LOCK_TH__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_EQU_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_EQU_ONLOCK_TTH__A, 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_EQU_UNLOCK_TTH__A, (u16)(-4), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_ORX_EQU_LOCK_MASK__A, 1 << 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* PRE-Filter coefficients (PFI) */
	rc = DRXJ_DAP.write_block_func(dev_addr, ORX_FWP_PFI_A_W__A, sizeof(pfi_coeffs[mode_index]), ((u8 *)pfi_coeffs[mode_index]), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_TOP_MDE_W__A, mode_index, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* NYQUIST-Filter coefficients (NYQ) */
	for (i = 0; i < (NYQFILTERLEN + 1) / 2; i++) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_FWP_NYQ_ADR_W__A, i, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_FWP_NYQ_COF_RW__A, nyquist_coeffs[mode_index][i], 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_FWP_NYQ_ADR_W__A, 31, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_COMM_EXEC__A, ORX_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/*********/
	/* Start */
	/*********/
	scu_cmd.command = SCU_RAM_COMMAND_STANDARD_OOB
	    | SCU_RAM_COMMAND_CMD_DEMOD_START;
	scu_cmd.parameter_len = 0;
	scu_cmd.result_len = 1;
	scu_cmd.result = cmd_result;
	rc = scu_command(dev_addr, &scu_cmd);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = set_orx_nsu_aox(demod, true);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_NSU_AOX_STHR_W__A, ext_attr->oob_pre_saw, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	ext_attr->oob_power_on = true;

	return 0;
rw_error:
#endif
	return -EIO;
}

/**
* \fn int ctrl_get_oob()
* \brief Set modulation standard to be used.
* \param demod instance of demodulator
* \param oob_status OOB status parameters.
* \return int.
*/
static int
ctrl_get_oob(struct drx_demod_instance *demod, struct drxoob_status *oob_status)
{
#ifndef DRXJ_DIGITAL_ONLY
	int rc;
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	u16 data = 0;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* check arguments */
	if (oob_status == NULL)
		return -EINVAL;

	if (!ext_attr->oob_power_on)
		return -EIO;

	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_DDC_OFO_SET_W__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_NSU_TUN_RFGAIN_W__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_FWP_AAG_THR_W__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ORX_DGN_KI__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_FWP_SRC_DGN_W__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = get_oob_lock_status(demod, dev_addr, &oob_status->lock);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = get_oob_frequency(demod, &oob_status->frequency);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = get_oobmer(dev_addr, &oob_status->mer);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = get_oob_symbol_rate_offset(dev_addr, &oob_status->symbol_rate_offset);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
#endif
	return -EIO;
}

/**
* \fn int ctrl_set_cfg_oob_pre_saw()
* \brief Configure PreSAW treshold value
* \param cfg_data Pointer to configuration parameter
* \return Error code
*/
#ifndef DRXJ_DIGITAL_ONLY
static int
ctrl_set_cfg_oob_pre_saw(struct drx_demod_instance *demod, u16 *cfg_data)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	if (cfg_data == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_NSU_AOX_STHR_W__A, *cfg_data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->oob_pre_saw = *cfg_data;
	return 0;
rw_error:
	return -EIO;
}
#endif

/**
* \fn int ctrl_get_cfg_oob_pre_saw()
* \brief Configure PreSAW treshold value
* \param cfg_data Pointer to configuration parameter
* \return Error code
*/
#ifndef DRXJ_DIGITAL_ONLY
static int
ctrl_get_cfg_oob_pre_saw(struct drx_demod_instance *demod, u16 *cfg_data)
{
	struct drxj_data *ext_attr = NULL;

	if (cfg_data == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	*cfg_data = ext_attr->oob_pre_saw;

	return 0;
}
#endif

/**
* \fn int ctrl_set_cfg_oob_lo_power()
* \brief Configure LO Power value
* \param cfg_data Pointer to enum drxj_cfg_oob_lo_power ** \return Error code
*/
#ifndef DRXJ_DIGITAL_ONLY
static int
ctrl_set_cfg_oob_lo_power(struct drx_demod_instance *demod, enum drxj_cfg_oob_lo_power *cfg_data)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	if (cfg_data == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	rc = DRXJ_DAP.write_reg16func(dev_addr, ORX_NSU_AOX_LOPOW_W__A, *cfg_data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->oob_lo_pow = *cfg_data;
	return 0;
rw_error:
	return -EIO;
}
#endif

/**
* \fn int ctrl_get_cfg_oob_lo_power()
* \brief Configure LO Power value
* \param cfg_data Pointer to enum drxj_cfg_oob_lo_power ** \return Error code
*/
#ifndef DRXJ_DIGITAL_ONLY
static int
ctrl_get_cfg_oob_lo_power(struct drx_demod_instance *demod, enum drxj_cfg_oob_lo_power *cfg_data)
{
	struct drxj_data *ext_attr = NULL;

	if (cfg_data == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	*cfg_data = ext_attr->oob_lo_pow;

	return 0;
}
#endif
/*============================================================================*/
/*==                     END OOB DATAPATH FUNCTIONS                         ==*/
/*============================================================================*/

/*=============================================================================
  ===== MC command related functions ==========================================
  ===========================================================================*/

/*=============================================================================
  ===== ctrl_set_channel() ==========================================================
  ===========================================================================*/
/**
* \fn int ctrl_set_channel()
* \brief Select a new transmission channel.
* \param demod instance of demod.
* \param channel Pointer to channel data.
* \return int.
*
* In case the tuner module is not used and in case of NTSC/FM the pogrammer
* must tune the tuner to the centre frequency of the NTSC/FM channel.
*
*/
static int
ctrl_set_channel(struct drx_demod_instance *demod, struct drx_channel *channel)
{
	int rc;
	s32 tuner_set_freq = 0;
	s32 tuner_get_freq = 0;
	s32 tuner_freq_offset = 0;
	s32 intermediate_freq = 0;
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
	u32 tuner_mode = 0;
	struct drx_common_attr *common_attr = NULL;
	bool bridge_closed = false;
#ifndef DRXJ_VSB_ONLY
	u32 min_symbol_rate = 0;
	u32 max_symbol_rate = 0;
	int bandwidth_temp = 0;
	int bandwidth = 0;
#endif
   /*== check arguments ======================================================*/
	if ((demod == NULL) || (channel == NULL))
		return -EINVAL;

	common_attr = (struct drx_common_attr *) demod->my_common_attr;
	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	standard = ext_attr->standard;

	/* check valid standards */
	switch (standard) {
	case DRX_STANDARD_8VSB:
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
#endif /* DRXJ_VSB_ONLY */
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_FM:
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
#endif /* DRXJ_DIGITAL_ONLY */
		break;
	case DRX_STANDARD_UNKNOWN:
	default:
		return -EINVAL;
	}

	/* check bandwidth QAM annex B, NTSC and 8VSB */
	if ((standard == DRX_STANDARD_ITU_B) ||
	    (standard == DRX_STANDARD_8VSB) ||
	    (standard == DRX_STANDARD_NTSC)) {
		switch (channel->bandwidth) {
		case DRX_BANDWIDTH_6MHZ:
		case DRX_BANDWIDTH_UNKNOWN:	/* fall through */
			channel->bandwidth = DRX_BANDWIDTH_6MHZ;
			break;
		case DRX_BANDWIDTH_8MHZ:	/* fall through */
		case DRX_BANDWIDTH_7MHZ:	/* fall through */
		default:
			return -EINVAL;
		}
	}
#ifndef DRXJ_DIGITAL_ONLY
	if (standard == DRX_STANDARD_PAL_SECAM_BG) {
		switch (channel->bandwidth) {
		case DRX_BANDWIDTH_7MHZ:	/* fall through */
		case DRX_BANDWIDTH_8MHZ:
			/* ok */
			break;
		case DRX_BANDWIDTH_6MHZ:	/* fall through */
		case DRX_BANDWIDTH_UNKNOWN:	/* fall through */
		default:
			return -EINVAL;
		}
	}
	/* check bandwidth PAL/SECAM  */
	if ((standard == DRX_STANDARD_PAL_SECAM_BG) ||
	    (standard == DRX_STANDARD_PAL_SECAM_DK) ||
	    (standard == DRX_STANDARD_PAL_SECAM_I) ||
	    (standard == DRX_STANDARD_PAL_SECAM_L) ||
	    (standard == DRX_STANDARD_PAL_SECAM_LP)) {
		switch (channel->bandwidth) {
		case DRX_BANDWIDTH_8MHZ:
		case DRX_BANDWIDTH_UNKNOWN:	/* fall through */
			channel->bandwidth = DRX_BANDWIDTH_8MHZ;
			break;
		case DRX_BANDWIDTH_6MHZ:	/* fall through */
		case DRX_BANDWIDTH_7MHZ:	/* fall through */
		default:
			return -EINVAL;
		}
	}
#endif

	/* For QAM annex A and annex C:
	   -check symbolrate and constellation
	   -derive bandwidth from symbolrate (input bandwidth is ignored)
	 */
#ifndef DRXJ_VSB_ONLY
	if ((standard == DRX_STANDARD_ITU_A) ||
	    (standard == DRX_STANDARD_ITU_C)) {
		struct drxuio_cfg uio_cfg = { DRX_UIO1, DRX_UIO_MODE_FIRMWARE_SAW };
		int bw_rolloff_factor = 0;

		bw_rolloff_factor = (standard == DRX_STANDARD_ITU_A) ? 115 : 113;
		min_symbol_rate = DRXJ_QAM_SYMBOLRATE_MIN;
		max_symbol_rate = DRXJ_QAM_SYMBOLRATE_MAX;
		/* config SMA_TX pin to SAW switch mode */
		rc = ctrl_set_uio_cfg(demod, &uio_cfg);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		if (channel->symbolrate < min_symbol_rate ||
		    channel->symbolrate > max_symbol_rate) {
			return -EINVAL;
		}

		switch (channel->constellation) {
		case DRX_CONSTELLATION_QAM16:	/* fall through */
		case DRX_CONSTELLATION_QAM32:	/* fall through */
		case DRX_CONSTELLATION_QAM64:	/* fall through */
		case DRX_CONSTELLATION_QAM128:	/* fall through */
		case DRX_CONSTELLATION_QAM256:
			bandwidth_temp = channel->symbolrate * bw_rolloff_factor;
			bandwidth = bandwidth_temp / 100;

			if ((bandwidth_temp % 100) >= 50)
				bandwidth++;

			if (bandwidth <= 6100000) {
				channel->bandwidth = DRX_BANDWIDTH_6MHZ;
			} else if ((bandwidth > 6100000)
				   && (bandwidth <= 7100000)) {
				channel->bandwidth = DRX_BANDWIDTH_7MHZ;
			} else if (bandwidth > 7100000) {
				channel->bandwidth = DRX_BANDWIDTH_8MHZ;
			}
			break;
		default:
			return -EINVAL;
		}
	}

	/* For QAM annex B:
	   -check constellation
	 */
	if (standard == DRX_STANDARD_ITU_B) {
		switch (channel->constellation) {
		case DRX_CONSTELLATION_AUTO:
		case DRX_CONSTELLATION_QAM256:
		case DRX_CONSTELLATION_QAM64:
			break;
		default:
			return -EINVAL;
		}

		switch (channel->interleavemode) {
		case DRX_INTERLEAVEMODE_I128_J1:
		case DRX_INTERLEAVEMODE_I128_J1_V2:
		case DRX_INTERLEAVEMODE_I128_J2:
		case DRX_INTERLEAVEMODE_I64_J2:
		case DRX_INTERLEAVEMODE_I128_J3:
		case DRX_INTERLEAVEMODE_I32_J4:
		case DRX_INTERLEAVEMODE_I128_J4:
		case DRX_INTERLEAVEMODE_I16_J8:
		case DRX_INTERLEAVEMODE_I128_J5:
		case DRX_INTERLEAVEMODE_I8_J16:
		case DRX_INTERLEAVEMODE_I128_J6:
		case DRX_INTERLEAVEMODE_I128_J7:
		case DRX_INTERLEAVEMODE_I128_J8:
		case DRX_INTERLEAVEMODE_I12_J17:
		case DRX_INTERLEAVEMODE_I5_J4:
		case DRX_INTERLEAVEMODE_B52_M240:
		case DRX_INTERLEAVEMODE_B52_M720:
		case DRX_INTERLEAVEMODE_UNKNOWN:
		case DRX_INTERLEAVEMODE_AUTO:
			break;
		default:
			return -EINVAL;
		}
	}

	if ((ext_attr->uio_sma_tx_mode) == DRX_UIO_MODE_FIRMWARE_SAW) {
		/* SAW SW, user UIO is used for switchable SAW */
		struct drxuio_data uio1 = { DRX_UIO1, false };

		switch (channel->bandwidth) {
		case DRX_BANDWIDTH_8MHZ:
			uio1.value = true;
			break;
		case DRX_BANDWIDTH_7MHZ:
			uio1.value = false;
			break;
		case DRX_BANDWIDTH_6MHZ:
			uio1.value = false;
			break;
		case DRX_BANDWIDTH_UNKNOWN:
		default:
			return -EINVAL;
		}

		rc = ctrl_uio_write(demod, &uio1);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
#endif /* DRXJ_VSB_ONLY */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
   /*== Tune, fast mode ======================================================*/
	if (demod->my_tuner != NULL) {
		/* Determine tuner mode and freq to tune to ... */
		switch (standard) {
#ifndef DRXJ_DIGITAL_ONLY
		case DRX_STANDARD_NTSC:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_LP:
			/* expecting center frequency, not picture carrier so no
			   conversion .... */
			tuner_mode |= TUNER_MODE_ANALOG;
			tuner_set_freq = channel->frequency;
			break;
		case DRX_STANDARD_FM:
			/* center frequency (equals sound carrier) as input,
			   tune to edge of SAW */
			tuner_mode |= TUNER_MODE_ANALOG;
			tuner_set_freq =
			    channel->frequency + DRXJ_FM_CARRIER_FREQ_OFFSET;
			break;
#endif
		case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
		case DRX_STANDARD_ITU_A:	/* fallthrough */
		case DRX_STANDARD_ITU_B:	/* fallthrough */
		case DRX_STANDARD_ITU_C:
#endif
			tuner_mode |= TUNER_MODE_DIGITAL;
			tuner_set_freq = channel->frequency;
			break;
		case DRX_STANDARD_UNKNOWN:
		default:
			return -EIO;
		}		/* switch(standard) */

		tuner_mode |= TUNER_MODE_SWITCH;
		switch (channel->bandwidth) {
		case DRX_BANDWIDTH_8MHZ:
			tuner_mode |= TUNER_MODE_8MHZ;
			break;
		case DRX_BANDWIDTH_7MHZ:
			tuner_mode |= TUNER_MODE_7MHZ;
			break;
		case DRX_BANDWIDTH_6MHZ:
			tuner_mode |= TUNER_MODE_6MHZ;
			break;
		default:
			/* TODO: for FM which bandwidth to use ?
			   also check offset from centre frequency ?
			   For now using 6MHz.
			 */
			tuner_mode |= TUNER_MODE_6MHZ;
			break;
			/* return (-EINVAL); */
		}

		/* store bandwidth for GetChannel() */
		ext_attr->curr_bandwidth = channel->bandwidth;
		ext_attr->curr_symbol_rate = channel->symbolrate;
		ext_attr->frequency = tuner_set_freq;
		if (common_attr->tuner_port_nr == 1) {
			/* close tuner bridge */
			bridge_closed = true;
			rc = ctrl_i2c_bridge(demod, &bridge_closed);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			/* set tuner frequency */
		}

		rc = drxbsp_tuner_set_frequency(demod->my_tuner, tuner_mode, tuner_set_freq);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (common_attr->tuner_port_nr == 1) {
			/* open tuner bridge */
			bridge_closed = false;
			rc = ctrl_i2c_bridge(demod, &bridge_closed);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}

		/* Get actual frequency set by tuner and compute offset */
		rc = drxbsp_tuner_get_frequency(demod->my_tuner, 0, &tuner_get_freq, &intermediate_freq);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		tuner_freq_offset = tuner_get_freq - tuner_set_freq;
		common_attr->intermediate_freq = intermediate_freq;
	} else {
		/* no tuner instance defined, use fixed intermediate frequency */
		tuner_freq_offset = 0;
		intermediate_freq = demod->my_common_attr->intermediate_freq;
	}			/* if ( demod->my_tuner != NULL ) */

   /*== Setup demod for specific standard ====================================*/
	switch (standard) {
	case DRX_STANDARD_8VSB:
		if (channel->mirror == DRX_MIRROR_AUTO)
			ext_attr->mirror = DRX_MIRROR_NO;
		else
			ext_attr->mirror = channel->mirror;
		rc = set_vsb(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = set_frequency(demod, channel, tuner_freq_offset);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:
		if (channel->mirror == DRX_MIRROR_AUTO)
			ext_attr->mirror = DRX_MIRROR_NO;
		else
			ext_attr->mirror = channel->mirror;
		rc = set_atv_channel(demod, tuner_freq_offset, channel, standard);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		rc = set_qam_channel(demod, channel, tuner_freq_offset);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
	case DRX_STANDARD_UNKNOWN:
	default:
		return -EIO;
	}

   /*== Re-tune, slow mode ===================================================*/
	if (demod->my_tuner != NULL) {
		/* tune to slow mode */
		tuner_mode &= ~TUNER_MODE_SWITCH;
		tuner_mode |= TUNER_MODE_LOCK;

		if (common_attr->tuner_port_nr == 1) {
			/* close tuner bridge */
			bridge_closed = true;
			rc = ctrl_i2c_bridge(demod, &bridge_closed);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}

		/* set tuner frequency */
		rc = drxbsp_tuner_set_frequency(demod->my_tuner, tuner_mode, tuner_set_freq);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (common_attr->tuner_port_nr == 1) {
			/* open tuner bridge */
			bridge_closed = false;
			rc = ctrl_i2c_bridge(demod, &bridge_closed);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
	}

	/* if ( demod->my_tuner !=NULL ) */
	/* flag the packet error counter reset */
	ext_attr->reset_pkt_err_acc = true;

	return 0;
rw_error:
	return -EIO;
}

/*=============================================================================
  ===== ctrl_get_channel() ==========================================================
  ===========================================================================*/
/**
* \fn int ctrl_get_channel()
* \brief Retreive parameters of current transmission channel.
* \param demod   Pointer to demod instance.
* \param channel Pointer to channel data.
* \return int.
*/
static int
ctrl_get_channel(struct drx_demod_instance *demod, struct drx_channel *channel)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;
	enum drx_lock_status lock_status = DRX_NOT_LOCKED;
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
	struct drx_common_attr *common_attr = NULL;
	s32 intermediate_freq = 0;
	s32 ctl_freq_offset = 0;
	u32 iqm_rc_rate_lo = 0;
	u32 adc_frequency = 0;
#ifndef DRXJ_VSB_ONLY
	int bandwidth_temp = 0;
	int bandwidth = 0;
#endif

	/* check arguments */
	if ((demod == NULL) || (channel == NULL))
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	standard = ext_attr->standard;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;

	/* initialize channel fields */
	channel->mirror = DRX_MIRROR_UNKNOWN;
	channel->hierarchy = DRX_HIERARCHY_UNKNOWN;
	channel->priority = DRX_PRIORITY_UNKNOWN;
	channel->coderate = DRX_CODERATE_UNKNOWN;
	channel->guard = DRX_GUARD_UNKNOWN;
	channel->fftmode = DRX_FFTMODE_UNKNOWN;
	channel->classification = DRX_CLASSIFICATION_UNKNOWN;
	channel->bandwidth = DRX_BANDWIDTH_UNKNOWN;
	channel->constellation = DRX_CONSTELLATION_UNKNOWN;
	channel->symbolrate = 0;
	channel->interleavemode = DRX_INTERLEAVEMODE_UNKNOWN;
	channel->carrier = DRX_CARRIER_UNKNOWN;
	channel->framemode = DRX_FRAMEMODE_UNKNOWN;
/*   channel->interleaver       = DRX_INTERLEAVER_UNKNOWN;*/
	channel->ldpc = DRX_LDPC_UNKNOWN;

	if (demod->my_tuner != NULL) {
		s32 tuner_freq_offset = 0;
		bool tuner_mirror = common_attr->mirror_freq_spect ? false : true;

		/* Get frequency from tuner */
		rc = drxbsp_tuner_get_frequency(demod->my_tuner, 0, &(channel->frequency), &intermediate_freq);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		tuner_freq_offset = channel->frequency - ext_attr->frequency;
		if (tuner_mirror) {
			/* positive image */
			channel->frequency += tuner_freq_offset;
		} else {
			/* negative image */
			channel->frequency -= tuner_freq_offset;
		}

		/* Handle sound carrier offset in RF domain */
		if (standard == DRX_STANDARD_FM)
			channel->frequency -= DRXJ_FM_CARRIER_FREQ_OFFSET;
	} else {
		intermediate_freq = common_attr->intermediate_freq;
	}

	/* check lock status */
	rc = ctrl_lock_status(demod, &lock_status);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if ((lock_status == DRX_LOCKED) || (lock_status == DRXJ_DEMOD_LOCK)) {
		rc = drxj_dap_atomic_read_reg32(dev_addr, IQM_RC_RATE_LO__A, &iqm_rc_rate_lo, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		adc_frequency = (common_attr->sys_clock_freq * 1000) / 3;

		channel->symbolrate =
		    frac28(adc_frequency, (iqm_rc_rate_lo + (1 << 23))) >> 7;

		switch (standard) {
		case DRX_STANDARD_8VSB:
			channel->bandwidth = DRX_BANDWIDTH_6MHZ;
			/* get the channel frequency */
			rc = get_ctl_freq_offset(demod, &ctl_freq_offset);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			channel->frequency -= ctl_freq_offset;
			/* get the channel constellation */
			channel->constellation = DRX_CONSTELLATION_AUTO;
			break;
#ifndef DRXJ_VSB_ONLY
		case DRX_STANDARD_ITU_A:
		case DRX_STANDARD_ITU_B:
		case DRX_STANDARD_ITU_C:
			{
				/* get the channel frequency */
				rc = get_ctl_freq_offset(demod, &ctl_freq_offset);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				channel->frequency -= ctl_freq_offset;

				if (standard == DRX_STANDARD_ITU_B) {
					channel->bandwidth = DRX_BANDWIDTH_6MHZ;
				} else {
					/* annex A & C */

					u32 roll_off = 113;	/* default annex C */

					if (standard == DRX_STANDARD_ITU_A)
						roll_off = 115;

					bandwidth_temp =
					    channel->symbolrate * roll_off;
					bandwidth = bandwidth_temp / 100;

					if ((bandwidth_temp % 100) >= 50)
						bandwidth++;

					if (bandwidth <= 6000000) {
						channel->bandwidth =
						    DRX_BANDWIDTH_6MHZ;
					} else if ((bandwidth > 6000000)
						   && (bandwidth <= 7000000)) {
						channel->bandwidth =
						    DRX_BANDWIDTH_7MHZ;
					} else if (bandwidth > 7000000) {
						channel->bandwidth =
						    DRX_BANDWIDTH_8MHZ;
					}
				}	/* if (standard == DRX_STANDARD_ITU_B) */

				{
					struct drxjscu_cmd cmd_scu = { 0, 0, 0, NULL, NULL };
					u16 cmd_result[3] = { 0, 0, 0 };

					cmd_scu.command =
					    SCU_RAM_COMMAND_STANDARD_QAM |
					    SCU_RAM_COMMAND_CMD_DEMOD_GET_PARAM;
					cmd_scu.parameter_len = 0;
					cmd_scu.result_len = 3;
					cmd_scu.parameter = NULL;
					cmd_scu.result = cmd_result;
					rc = scu_command(dev_addr, &cmd_scu);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}

					channel->interleavemode =
					    (enum drx_interleave_mode) (cmd_scu.
								    result[2]);
				}

				switch (ext_attr->constellation) {
				case DRX_CONSTELLATION_QAM256:
					channel->constellation =
					    DRX_CONSTELLATION_QAM256;
					break;
				case DRX_CONSTELLATION_QAM128:
					channel->constellation =
					    DRX_CONSTELLATION_QAM128;
					break;
				case DRX_CONSTELLATION_QAM64:
					channel->constellation =
					    DRX_CONSTELLATION_QAM64;
					break;
				case DRX_CONSTELLATION_QAM32:
					channel->constellation =
					    DRX_CONSTELLATION_QAM32;
					break;
				case DRX_CONSTELLATION_QAM16:
					channel->constellation =
					    DRX_CONSTELLATION_QAM16;
					break;
				default:
					channel->constellation =
					    DRX_CONSTELLATION_UNKNOWN;
					return -EIO;
				}
			}
			break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
		case DRX_STANDARD_NTSC:	/* fall trough */
		case DRX_STANDARD_PAL_SECAM_BG:
		case DRX_STANDARD_PAL_SECAM_DK:
		case DRX_STANDARD_PAL_SECAM_I:
		case DRX_STANDARD_PAL_SECAM_L:
		case DRX_STANDARD_PAL_SECAM_LP:
		case DRX_STANDARD_FM:
			rc = get_atv_channel(demod, channel, standard);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
#endif
		case DRX_STANDARD_UNKNOWN:	/* fall trough */
		default:
			return -EIO;
		}		/* switch ( standard ) */

		if (lock_status == DRX_LOCKED)
			channel->mirror = ext_attr->mirror;
	}
	/* if ( lock_status == DRX_LOCKED ) */
	return 0;
rw_error:
	return -EIO;
}

/*=============================================================================
  ===== SigQuality() ==========================================================
  ===========================================================================*/

static u16
mer2indicator(u16 mer, u16 min_mer, u16 threshold_mer, u16 max_mer)
{
	u16 indicator = 0;

	if (mer < min_mer) {
		indicator = 0;
	} else if (mer < threshold_mer) {
		if ((threshold_mer - min_mer) != 0)
			indicator = 25 * (mer - min_mer) / (threshold_mer - min_mer);
	} else if (mer < max_mer) {
		if ((max_mer - threshold_mer) != 0)
			indicator = 25 + 75 * (mer - threshold_mer) / (max_mer - threshold_mer);
		else
			indicator = 25;
	} else {
		indicator = 100;
	}

	return indicator;
}

/**
* \fn int ctrl_sig_quality()
* \brief Retreive signal quality form device.
* \param devmod Pointer to demodulator instance.
* \param sig_quality Pointer to signal quality data.
* \return int.
* \retval 0 sig_quality contains valid data.
* \retval -EINVAL sig_quality is NULL.
* \retval -EIO Erroneous data, sig_quality contains invalid data.

*/
static int
ctrl_sig_quality(struct drx_demod_instance *demod, struct drx_sig_quality *sig_quality)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
	enum drx_lock_status lock_status = DRX_NOT_LOCKED;
	u16 min_mer = 0;
	u16 max_mer = 0;
	u16 threshold_mer = 0;

	/* Check arguments */
	if ((sig_quality == NULL) || (demod == NULL))
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	standard = ext_attr->standard;

	/* get basic information */
	dev_addr = demod->my_i2c_dev_addr;
	rc = ctrl_lock_status(demod, &lock_status);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	switch (standard) {
	case DRX_STANDARD_8VSB:
#ifdef DRXJ_SIGNAL_ACCUM_ERR
		rc = get_acc_pkt_err(demod, &sig_quality->packet_error);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
#else
		rc = get_vsb_post_rs_pck_err(dev_addr, &sig_quality->packet_error);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
#endif
		if (lock_status != DRXJ_DEMOD_LOCK && lock_status != DRX_LOCKED) {
			sig_quality->post_viterbi_ber = 500000;
			sig_quality->MER = 20;
			sig_quality->pre_viterbi_ber = 0;
		} else {
			/* PostViterbi is compute in steps of 10^(-6) */
			rc = get_vs_bpre_viterbi_ber(dev_addr, &sig_quality->pre_viterbi_ber);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = get_vs_bpost_viterbi_ber(dev_addr, &sig_quality->post_viterbi_ber);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = get_vsbmer(dev_addr, &sig_quality->MER);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		min_mer = 20;
		max_mer = 360;
		threshold_mer = 145;
		sig_quality->post_reed_solomon_ber = 0;
		sig_quality->scale_factor_ber = 1000000;
		sig_quality->indicator =
		    mer2indicator(sig_quality->MER, min_mer, threshold_mer,
				  max_mer);
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		rc = ctrl_get_qam_sig_quality(demod, sig_quality);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (lock_status != DRXJ_DEMOD_LOCK && lock_status != DRX_LOCKED) {
			switch (ext_attr->constellation) {
			case DRX_CONSTELLATION_QAM256:
				sig_quality->MER = 210;
				break;
			case DRX_CONSTELLATION_QAM128:
				sig_quality->MER = 180;
				break;
			case DRX_CONSTELLATION_QAM64:
				sig_quality->MER = 150;
				break;
			case DRX_CONSTELLATION_QAM32:
				sig_quality->MER = 120;
				break;
			case DRX_CONSTELLATION_QAM16:
				sig_quality->MER = 90;
				break;
			default:
				sig_quality->MER = 0;
				return -EIO;
			}
		}

		switch (ext_attr->constellation) {
		case DRX_CONSTELLATION_QAM256:
			min_mer = 210;
			threshold_mer = 270;
			max_mer = 380;
			break;
		case DRX_CONSTELLATION_QAM64:
			min_mer = 150;
			threshold_mer = 210;
			max_mer = 380;
			break;
		case DRX_CONSTELLATION_QAM128:
		case DRX_CONSTELLATION_QAM32:
		case DRX_CONSTELLATION_QAM16:
			break;
		default:
			return -EIO;
		}
		sig_quality->indicator =
		    mer2indicator(sig_quality->MER, min_mer, threshold_mer,
				  max_mer);
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
	case DRX_STANDARD_NTSC:
		rc = atv_sig_quality(demod, sig_quality);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
	case DRX_STANDARD_FM:
		rc = fm_sig_quality(demod, sig_quality);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_lock_status()
* \brief Retreive lock status .
* \param dev_addr Pointer to demodulator device address.
* \param lock_stat Pointer to lock status structure.
* \return int.
*
*/
static int
ctrl_lock_status(struct drx_demod_instance *demod, enum drx_lock_status *lock_stat)
{
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	struct drxjscu_cmd cmd_scu = { /* command      */ 0,
		/* parameter_len */ 0,
		/* result_len    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	int rc;
	u16 cmd_result[2] = { 0, 0 };
	u16 demod_lock = SCU_RAM_PARAM_1_RES_DEMOD_GET_LOCK_DEMOD_LOCKED;

	/* check arguments */
	if ((demod == NULL) || (lock_stat == NULL))
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	standard = ext_attr->standard;

	*lock_stat = DRX_NOT_LOCKED;

	/* define the SCU command code */
	switch (standard) {
	case DRX_STANDARD_8VSB:
		cmd_scu.command = SCU_RAM_COMMAND_STANDARD_VSB |
		    SCU_RAM_COMMAND_CMD_DEMOD_GET_LOCK;
		demod_lock |= 0x6;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		cmd_scu.command = SCU_RAM_COMMAND_STANDARD_QAM |
		    SCU_RAM_COMMAND_CMD_DEMOD_GET_LOCK;
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
		cmd_scu.command = SCU_RAM_COMMAND_STANDARD_ATV |
		    SCU_RAM_COMMAND_CMD_DEMOD_GET_LOCK;
		break;
	case DRX_STANDARD_FM:
		return fm_lock_status(demod, lock_stat);
#endif
	case DRX_STANDARD_UNKNOWN:	/* fallthrough */
	default:
		return -EIO;
	}

	/* define the SCU command paramters and execute the command */
	cmd_scu.parameter_len = 0;
	cmd_scu.result_len = 2;
	cmd_scu.parameter = NULL;
	cmd_scu.result = cmd_result;
	rc = scu_command(dev_addr, &cmd_scu);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* set the lock status */
	if (cmd_scu.result[1] < demod_lock) {
		/* 0x0000 NOT LOCKED */
		*lock_stat = DRX_NOT_LOCKED;
	} else if (cmd_scu.result[1] < SCU_RAM_PARAM_1_RES_DEMOD_GET_LOCK_LOCKED) {
		*lock_stat = DRXJ_DEMOD_LOCK;
	} else if (cmd_scu.result[1] <
		   SCU_RAM_PARAM_1_RES_DEMOD_GET_LOCK_NEVER_LOCK) {
		/* 0x8000 DEMOD + FEC LOCKED (system lock) */
		*lock_stat = DRX_LOCKED;
	} else {
		/* 0xC000 NEVER LOCKED */
		/* (system will never be able to lock to the signal) */
		*lock_stat = DRX_NEVER_LOCK;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_constel()
* \brief Retreive a constellation point via I2C.
* \param demod Pointer to demodulator instance.
* \param complex_nr Pointer to the structure in which to store the
		   constellation point.
* \return int.
*/
static int
ctrl_constel(struct drx_demod_instance *demod, struct drx_complex *complex_nr)
{
	int rc;
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
						     /**< active standard */

	/* check arguments */
	if ((demod == NULL) || (complex_nr == NULL))
		return -EINVAL;

	/* read device info */
	standard = ((struct drxj_data *) demod->my_ext_attr)->standard;

	/* Read constellation point  */
	switch (standard) {
	case DRX_STANDARD_8VSB:
		rc = ctrl_get_vsb_constel(demod, complex_nr);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		rc = ctrl_get_qam_constel(demod, complex_nr);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
	case DRX_STANDARD_UNKNOWN:
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_set_standard()
* \brief Set modulation standard to be used.
* \param standard Modulation standard.
* \return int.
*
* Setup stuff for the desired demodulation standard.
* Disable and power down the previous selected demodulation standard
*
*/
static int
ctrl_set_standard(struct drx_demod_instance *demod, enum drx_standard *standard)
{
	struct drxj_data *ext_attr = NULL;
	int rc;
	enum drx_standard prev_standard;

	/* check arguments */
	if ((standard == NULL) || (demod == NULL))
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	prev_standard = ext_attr->standard;

	/*
	   Stop and power down previous standard
	 */
	switch (prev_standard) {
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		rc = power_down_qam(demod, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
	case DRX_STANDARD_8VSB:
		rc = power_down_vsb(demod, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:
		rc = power_down_atv(demod, prev_standard, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
	case DRX_STANDARD_UNKNOWN:
		/* Do nothing */
		break;
	case DRX_STANDARD_AUTO:	/* fallthrough */
	default:
		return -EINVAL;
	}

	/*
	   Initialize channel independent registers
	   Power up new standard
	 */
	ext_attr->standard = *standard;

	switch (*standard) {
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		do {
			u16 dummy;
			rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SCU_RAM_VERSION_HI__A, &dummy, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} while (0);
		break;
#endif
	case DRX_STANDARD_8VSB:
		rc = set_vsb_leak_n_gain(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:
		rc = set_atv_standard(demod, standard);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = power_up_atv(demod, *standard);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
	default:
		ext_attr->standard = DRX_STANDARD_UNKNOWN;
		return -EINVAL;
		break;
	}

	return 0;
rw_error:
	/* Don't know what the standard is now ... try again */
	ext_attr->standard = DRX_STANDARD_UNKNOWN;
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_get_standard()
* \brief Get modulation standard currently used to demodulate.
* \param standard Modulation standard.
* \return int.
*
* Returns 8VSB, NTSC, QAM only.
*
*/
static int
ctrl_get_standard(struct drx_demod_instance *demod, enum drx_standard *standard)
{
	struct drxj_data *ext_attr = NULL;
	int rc;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* check arguments */
	if (standard == NULL)
		return -EINVAL;

	*standard = ext_attr->standard;
	do {
		u16 dummy;
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SCU_RAM_VERSION_HI__A, &dummy, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} while (0);

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_get_cfg_symbol_clock_offset()
* \brief Get frequency offsets of STR.
* \param pointer to s32.
* \return int.
*
*/
static int
ctrl_get_cfg_symbol_clock_offset(struct drx_demod_instance *demod, s32 *rate_offset)
{
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
	int rc;
	struct drxj_data *ext_attr = NULL;

	/* check arguments */
	if (rate_offset == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	standard = ext_attr->standard;

	switch (standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
		rc = get_str_freq_offset(demod, rate_offset);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_UNKNOWN:
	default:
		return -EINVAL;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

static void drxj_reset_mode(struct drxj_data *ext_attr)
{
	/* Initialize default AFE configuartion for QAM */
	if (ext_attr->has_lna) {
		/* IF AGC off, PGA active */
#ifndef DRXJ_VSB_ONLY
		ext_attr->qam_if_agc_cfg.standard = DRX_STANDARD_ITU_B;
		ext_attr->qam_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_OFF;
		ext_attr->qam_pga_cfg = 140 + (11 * 13);
#endif
		ext_attr->vsb_if_agc_cfg.standard = DRX_STANDARD_8VSB;
		ext_attr->vsb_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_OFF;
		ext_attr->vsb_pga_cfg = 140 + (11 * 13);
	} else {
		/* IF AGC on, PGA not active */
#ifndef DRXJ_VSB_ONLY
		ext_attr->qam_if_agc_cfg.standard = DRX_STANDARD_ITU_B;
		ext_attr->qam_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_AUTO;
		ext_attr->qam_if_agc_cfg.min_output_level = 0;
		ext_attr->qam_if_agc_cfg.max_output_level = 0x7FFF;
		ext_attr->qam_if_agc_cfg.speed = 3;
		ext_attr->qam_if_agc_cfg.top = 1297;
		ext_attr->qam_pga_cfg = 140;
#endif
		ext_attr->vsb_if_agc_cfg.standard = DRX_STANDARD_8VSB;
		ext_attr->vsb_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_AUTO;
		ext_attr->vsb_if_agc_cfg.min_output_level = 0;
		ext_attr->vsb_if_agc_cfg.max_output_level = 0x7FFF;
		ext_attr->vsb_if_agc_cfg.speed = 3;
		ext_attr->vsb_if_agc_cfg.top = 1024;
		ext_attr->vsb_pga_cfg = 140;
	}
	/* TODO: remove min_output_level and max_output_level for both QAM and VSB after */
	/* mc has not used them */
#ifndef DRXJ_VSB_ONLY
	ext_attr->qam_rf_agc_cfg.standard = DRX_STANDARD_ITU_B;
	ext_attr->qam_rf_agc_cfg.ctrl_mode = DRX_AGC_CTRL_AUTO;
	ext_attr->qam_rf_agc_cfg.min_output_level = 0;
	ext_attr->qam_rf_agc_cfg.max_output_level = 0x7FFF;
	ext_attr->qam_rf_agc_cfg.speed = 3;
	ext_attr->qam_rf_agc_cfg.top = 9500;
	ext_attr->qam_rf_agc_cfg.cut_off_current = 4000;
	ext_attr->qam_pre_saw_cfg.standard = DRX_STANDARD_ITU_B;
	ext_attr->qam_pre_saw_cfg.reference = 0x07;
	ext_attr->qam_pre_saw_cfg.use_pre_saw = true;
#endif
	/* Initialize default AFE configuartion for VSB */
	ext_attr->vsb_rf_agc_cfg.standard = DRX_STANDARD_8VSB;
	ext_attr->vsb_rf_agc_cfg.ctrl_mode = DRX_AGC_CTRL_AUTO;
	ext_attr->vsb_rf_agc_cfg.min_output_level = 0;
	ext_attr->vsb_rf_agc_cfg.max_output_level = 0x7FFF;
	ext_attr->vsb_rf_agc_cfg.speed = 3;
	ext_attr->vsb_rf_agc_cfg.top = 9500;
	ext_attr->vsb_rf_agc_cfg.cut_off_current = 4000;
	ext_attr->vsb_pre_saw_cfg.standard = DRX_STANDARD_8VSB;
	ext_attr->vsb_pre_saw_cfg.reference = 0x07;
	ext_attr->vsb_pre_saw_cfg.use_pre_saw = true;

#ifndef DRXJ_DIGITAL_ONLY
	/* Initialize default AFE configuartion for ATV */
	ext_attr->atv_rf_agc_cfg.standard = DRX_STANDARD_NTSC;
	ext_attr->atv_rf_agc_cfg.ctrl_mode = DRX_AGC_CTRL_AUTO;
	ext_attr->atv_rf_agc_cfg.top = 9500;
	ext_attr->atv_rf_agc_cfg.cut_off_current = 4000;
	ext_attr->atv_rf_agc_cfg.speed = 3;
	ext_attr->atv_if_agc_cfg.standard = DRX_STANDARD_NTSC;
	ext_attr->atv_if_agc_cfg.ctrl_mode = DRX_AGC_CTRL_AUTO;
	ext_attr->atv_if_agc_cfg.speed = 3;
	ext_attr->atv_if_agc_cfg.top = 2400;
	ext_attr->atv_pre_saw_cfg.reference = 0x0007;
	ext_attr->atv_pre_saw_cfg.use_pre_saw = true;
	ext_attr->atv_pre_saw_cfg.standard = DRX_STANDARD_NTSC;
#endif
}

/**
* \fn int ctrl_power_mode()
* \brief Set the power mode of the device to the specified power mode
* \param demod Pointer to demodulator instance.
* \param mode  Pointer to new power mode.
* \return int.
* \retval 0          Success
* \retval -EIO       I2C error or other failure
* \retval -EINVAL Invalid mode argument.
*
*
*/
static int
ctrl_power_mode(struct drx_demod_instance *demod, enum drx_power_mode *mode)
{
	struct drx_common_attr *common_attr = (struct drx_common_attr *) NULL;
	struct drxj_data *ext_attr = (struct drxj_data *) NULL;
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)NULL;
	int rc;
	u16 sio_cc_pwd_mode = 0;

	common_attr = (struct drx_common_attr *) demod->my_common_attr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	dev_addr = demod->my_i2c_dev_addr;

	/* Check arguments */
	if (mode == NULL)
		return -EINVAL;

	/* If already in requested power mode, do nothing */
	if (common_attr->current_power_mode == *mode)
		return 0;

	switch (*mode) {
	case DRX_POWER_UP:
	case DRXJ_POWER_DOWN_MAIN_PATH:
		sio_cc_pwd_mode = SIO_CC_PWD_MODE_LEVEL_NONE;
		break;
	case DRXJ_POWER_DOWN_CORE:
		sio_cc_pwd_mode = SIO_CC_PWD_MODE_LEVEL_CLOCK;
		break;
	case DRXJ_POWER_DOWN_PLL:
		sio_cc_pwd_mode = SIO_CC_PWD_MODE_LEVEL_PLL;
		break;
	case DRX_POWER_DOWN:
		sio_cc_pwd_mode = SIO_CC_PWD_MODE_LEVEL_OSC;
		break;
	default:
		/* Unknow sleep mode */
		return -EINVAL;
		break;
	}

	/* Check if device needs to be powered up */
	if ((common_attr->current_power_mode != DRX_POWER_UP)) {
		rc = power_up_device(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	if ((*mode == DRX_POWER_UP)) {
		/* Restore analog & pin configuartion */

		/* Initialize default AFE configuartion for VSB */
		drxj_reset_mode(ext_attr);
	} else {
		/* Power down to requested mode */
		/* Backup some register settings */
		/* Set pins with possible pull-ups connected to them in input mode */
		/* Analog power down */
		/* ADC power down */
		/* Power down device */
		/* stop all comm_exec */
		/*
		   Stop and power down previous standard
		 */

		switch (ext_attr->standard) {
		case DRX_STANDARD_ITU_A:
		case DRX_STANDARD_ITU_B:
		case DRX_STANDARD_ITU_C:
			rc = power_down_qam(demod, true);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_STANDARD_8VSB:
			rc = power_down_vsb(demod, true);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
		case DRX_STANDARD_NTSC:	/* fallthrough */
		case DRX_STANDARD_FM:
			rc = power_down_atv(demod, ext_attr->standard, true);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		case DRX_STANDARD_UNKNOWN:
			/* Do nothing */
			break;
		case DRX_STANDARD_AUTO:	/* fallthrough */
		default:
			return -EIO;
		}

		if (*mode != DRXJ_POWER_DOWN_MAIN_PATH) {
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_CC_PWD_MODE__A, sio_cc_pwd_mode, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			/* Initialize HI, wakeup key especially before put IC to sleep */
			rc = init_hi(demod);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			ext_attr->hi_cfg_ctrl |= SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ;
			rc = hi_cfg_command(demod);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
	}

	common_attr->current_power_mode = *mode;

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_version()
* \brief Report version of microcode and if possible version of device
* \param demod Pointer to demodulator instance.
* \param version_list Pointer to pointer of linked list of versions.
* \return int.
*
* Using static structures so no allocation of memory is needed.
* Filling in all the fields each time, cause you don't know if they are
* changed by the application.
*
* For device:
* Major version number will be last two digits of family number.
* Minor number will be full respin number
* Patch will be metal fix number+1
* Examples:
* DRX3942J A2 => number: 42.1.2 text: "DRX3942J:A2"
* DRX3933J B1 => number: 33.2.1 text: "DRX3933J:B1"
*
*/
static int
ctrl_version(struct drx_demod_instance *demod, struct drx_version_list **version_list)
{
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	struct drx_common_attr *common_attr = (struct drx_common_attr *) (NULL);
	int rc;
	u16 ucode_major_minor = 0;	/* BCD Ma:Ma:Ma:Mi */
	u16 ucode_patch = 0;	/* BCD Pa:Pa:Pa:Pa */
	u16 major = 0;
	u16 minor = 0;
	u16 patch = 0;
	u16 idx = 0;
	u32 jtag = 0;
	u16 subtype = 0;
	u16 mfx = 0;
	u16 bid = 0;
	u16 key = 0;
	static char ucode_name[] = "Microcode";
	static char device_name[] = "Device";

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;

	/* Microcode version *************************************** */

	ext_attr->v_version[0].module_type = DRX_MODULE_MICROCODE;
	ext_attr->v_version[0].module_name = ucode_name;
	ext_attr->v_version[0].v_string = ext_attr->v_text[0];

	if (common_attr->is_opened == true) {
		rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_VERSION_HI__A, &ucode_major_minor, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_VERSION_LO__A, &ucode_patch, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* Translate BCD to numbers and string */
		/* TODO: The most significant Ma and Pa will be ignored, check with spec */
		minor = (ucode_major_minor & 0xF);
		ucode_major_minor >>= 4;
		major = (ucode_major_minor & 0xF);
		ucode_major_minor >>= 4;
		major += (10 * (ucode_major_minor & 0xF));
		patch = (ucode_patch & 0xF);
		ucode_patch >>= 4;
		patch += (10 * (ucode_patch & 0xF));
		ucode_patch >>= 4;
		patch += (100 * (ucode_patch & 0xF));
	} else {
		/* No microcode uploaded, No Rom existed, set version to 0.0.0 */
		patch = 0;
		minor = 0;
		major = 0;
	}
	ext_attr->v_version[0].v_major = major;
	ext_attr->v_version[0].v_minor = minor;
	ext_attr->v_version[0].v_patch = patch;

	if (major / 10 != 0) {
		ext_attr->v_version[0].v_string[idx++] =
		    ((char)(major / 10)) + '0';
		major %= 10;
	}
	ext_attr->v_version[0].v_string[idx++] = ((char)major) + '0';
	ext_attr->v_version[0].v_string[idx++] = '.';
	ext_attr->v_version[0].v_string[idx++] = ((char)minor) + '0';
	ext_attr->v_version[0].v_string[idx++] = '.';
	if (patch / 100 != 0) {
		ext_attr->v_version[0].v_string[idx++] =
		    ((char)(patch / 100)) + '0';
		patch %= 100;
	}
	if (patch / 10 != 0) {
		ext_attr->v_version[0].v_string[idx++] =
		    ((char)(patch / 10)) + '0';
		patch %= 10;
	}
	ext_attr->v_version[0].v_string[idx++] = ((char)patch) + '0';
	ext_attr->v_version[0].v_string[idx] = '\0';

	ext_attr->v_list_elements[0].version = &(ext_attr->v_version[0]);
	ext_attr->v_list_elements[0].next = &(ext_attr->v_list_elements[1]);

	/* Device version *************************************** */
	/* Check device id */
	rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, &key, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, 0xFABA, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg32func(dev_addr, SIO_TOP_JTAGID_LO__A, &jtag, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, SIO_PDR_UIO_IN_HI__A, &bid, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_TOP_COMM_KEY__A, key, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	ext_attr->v_version[1].module_type = DRX_MODULE_DEVICE;
	ext_attr->v_version[1].module_name = device_name;
	ext_attr->v_version[1].v_string = ext_attr->v_text[1];
	ext_attr->v_version[1].v_string[0] = 'D';
	ext_attr->v_version[1].v_string[1] = 'R';
	ext_attr->v_version[1].v_string[2] = 'X';
	ext_attr->v_version[1].v_string[3] = '3';
	ext_attr->v_version[1].v_string[4] = '9';
	ext_attr->v_version[1].v_string[7] = 'J';
	ext_attr->v_version[1].v_string[8] = ':';
	ext_attr->v_version[1].v_string[11] = '\0';

	/* DRX39xxJ type Ax */
	/* TODO semantics of mfx and spin are unclear */
	subtype = (u16) ((jtag >> 12) & 0xFF);
	mfx = (u16) (jtag >> 29);
	ext_attr->v_version[1].v_minor = 1;
	if (mfx == 0x03)
		ext_attr->v_version[1].v_patch = mfx + 2;
	else
		ext_attr->v_version[1].v_patch = mfx + 1;
	ext_attr->v_version[1].v_string[6] = ((char)(subtype & 0xF)) + '0';
	ext_attr->v_version[1].v_major = (subtype & 0x0F);
	subtype >>= 4;
	ext_attr->v_version[1].v_string[5] = ((char)(subtype & 0xF)) + '0';
	ext_attr->v_version[1].v_major += 10 * subtype;
	ext_attr->v_version[1].v_string[9] = 'A';
	if (mfx == 0x03)
		ext_attr->v_version[1].v_string[10] = ((char)(mfx & 0xF)) + '2';
	else
		ext_attr->v_version[1].v_string[10] = ((char)(mfx & 0xF)) + '1';

	ext_attr->v_list_elements[1].version = &(ext_attr->v_version[1]);
	ext_attr->v_list_elements[1].next = (struct drx_version_list *) (NULL);

	*version_list = &(ext_attr->v_list_elements[0]);

	return 0;

rw_error:
	*version_list = (struct drx_version_list *) (NULL);
	return -EIO;

}

/*============================================================================*/

/**
* \fn int ctrl_probe_device()
* \brief Probe device, check if it is present
* \param demod Pointer to demodulator instance.
* \return int.
* \retval 0    a drx39xxj device has been detected.
* \retval -EIO no drx39xxj device detected.
*
* This funtion can be caled before open() and after close().
*
*/

static int ctrl_probe_device(struct drx_demod_instance *demod)
{
	enum drx_power_mode org_power_mode = DRX_POWER_UP;
	int ret_status = 0;
	struct drx_common_attr *common_attr = (struct drx_common_attr *) (NULL);
	int rc;

	common_attr = (struct drx_common_attr *) demod->my_common_attr;

	if (common_attr->is_opened == false
	    || common_attr->current_power_mode != DRX_POWER_UP) {
		struct i2c_device_addr *dev_addr = NULL;
		enum drx_power_mode power_mode = DRX_POWER_UP;
		u32 jtag = 0;

		dev_addr = demod->my_i2c_dev_addr;

		/* Remeber original power mode */
		org_power_mode = common_attr->current_power_mode;

		if (demod->my_common_attr->is_opened == false) {
			rc = power_up_device(demod);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			common_attr->current_power_mode = DRX_POWER_UP;
		} else {
			/* Wake-up device, feedback from device */
			rc = ctrl_power_mode(demod, &power_mode);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		/* Initialize HI, wakeup key especially */
		rc = init_hi(demod);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* Check device id */
		rc = DRXJ_DAP.read_reg32func(dev_addr, SIO_TOP_JTAGID_LO__A, &jtag, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		jtag = (jtag >> 12) & 0xFFFF;
		switch (jtag) {
		case 0x3931:	/* fallthrough */
		case 0x3932:	/* fallthrough */
		case 0x3933:	/* fallthrough */
		case 0x3934:	/* fallthrough */
		case 0x3941:	/* fallthrough */
		case 0x3942:	/* fallthrough */
		case 0x3943:	/* fallthrough */
		case 0x3944:	/* fallthrough */
		case 0x3945:	/* fallthrough */
		case 0x3946:
			/* ok , do nothing */
			break;
		default:
			ret_status = -EIO;
			break;
		}

		/* Device was not opened, return to orginal powermode,
		   feedback from device */
		rc = ctrl_power_mode(demod, &org_power_mode);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		/* dummy read to make this function fail in case device
		   suddenly disappears after a succesful drx_open */
		do {
			u16 dummy;
			rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SCU_RAM_VERSION_HI__A, &dummy, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} while (0);
	}

	return ret_status;

rw_error:
	common_attr->current_power_mode = org_power_mode;
	return -EIO;
}

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
/*============================================================================*/

/**
* \fn int is_mc_block_audio()
* \brief Check if MC block is Audio or not Audio.
* \param addr        Pointer to demodulator instance.
* \param audioUpload true  if MC block is Audio
		     false if MC block not Audio
* \return bool.
*/
bool is_mc_block_audio(u32 addr)
{
	if ((addr == AUD_XFP_PRAM_4K__A) || (addr == AUD_XDFP_PRAM_4K__A))
		return true;

	return false;
}

/*============================================================================*/

/**
* \fn int ctrl_u_code_upload()
* \brief Handle Audio or !Audio part of microcode upload.
* \param demod          Pointer to demodulator instance.
* \param mc_info         Pointer to information about microcode data.
* \param action         Either UCODE_UPLOAD or UCODE_VERIFY.
* \param upload_audio_mc  true  if Audio MC need to be uploaded.
			false if !Audio MC need to be uploaded.
* \return int.
*/
static int
ctrl_u_code_upload(struct drx_demod_instance *demod,
		   struct drxu_code_info *mc_info,
		   enum drxu_code_actionaction, bool upload_audio_mc)
{
	u16 i = 0;
	u16 mc_nr_of_blks = 0;
	u16 mc_magic_word = 0;
	u8 *mc_data = (u8 *)(NULL);
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);
	struct drxj_data *ext_attr = (struct drxj_data *) (NULL);
	int rc;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* Check arguments */
	if (!mc_info || !mc_info->mc_data) {
		return -EINVAL;
	}

	mc_data = mc_info->mc_data;

	/* Check data */
	mc_magic_word = u_code_read16(mc_data);
	mc_data += sizeof(u16);
	mc_nr_of_blks = u_code_read16(mc_data);
	mc_data += sizeof(u16);

	if ((mc_magic_word != DRXJ_UCODE_MAGIC_WORD) || (mc_nr_of_blks == 0)) {
		/* wrong endianess or wrong data ? */
		return -EINVAL;
	}

	/* Process microcode blocks */
	for (i = 0; i < mc_nr_of_blks; i++) {
		struct drxu_code_block_hdr block_hdr;
		u16 mc_block_nr_bytes = 0;

		/* Process block header */
		block_hdr.addr = u_code_read32(mc_data);
		mc_data += sizeof(u32);
		block_hdr.size = u_code_read16(mc_data);
		mc_data += sizeof(u16);
		block_hdr.flags = u_code_read16(mc_data);
		mc_data += sizeof(u16);
		block_hdr.CRC = u_code_read16(mc_data);
		mc_data += sizeof(u16);

		/* Check block header on:
		   - no data
		   - data larger then 64Kb
		   - if CRC enabled check CRC
		 */
		if ((block_hdr.size == 0) ||
		    (block_hdr.size > 0x7FFF) ||
		    (((block_hdr.flags & DRXJ_UCODE_CRC_FLAG) != 0) &&
		     (block_hdr.CRC != u_code_compute_crc(mc_data, block_hdr.size)))
		    ) {
			/* Wrong data ! */
			return -EINVAL;
		}

		mc_block_nr_bytes = block_hdr.size * sizeof(u16);

		/* Perform the desired action */
		/* Check which part of MC need to be uploaded - Audio or not Audio */
		if (is_mc_block_audio(block_hdr.addr) == upload_audio_mc) {
			switch (action) {
	    /*===================================================================*/
			case UCODE_UPLOAD:
				{
					/* Upload microcode */
					if (demod->my_access_funct->
					    write_block_func(dev_addr,
							   (dr_xaddr_t) block_hdr.
							   addr, mc_block_nr_bytes,
							   mc_data,
							   0x0000) !=
					    0) {
						return -EIO;
					}
				}
				break;

	    /*===================================================================*/
			case UCODE_VERIFY:
				{
					int result = 0;
					u8 mc_data_buffer
					    [DRXJ_UCODE_MAX_BUF_SIZE];
					u32 bytes_to_compare = 0;
					u32 bytes_left_to_compare = 0;
					u32 curr_addr = (dr_xaddr_t) 0;
					u8 *curr_ptr = NULL;

					bytes_left_to_compare = mc_block_nr_bytes;
					curr_addr = block_hdr.addr;
					curr_ptr = mc_data;

					while (bytes_left_to_compare != 0) {
						if (bytes_left_to_compare > ((u32)DRXJ_UCODE_MAX_BUF_SIZE))
							bytes_to_compare = ((u32)DRXJ_UCODE_MAX_BUF_SIZE);
						else
							bytes_to_compare = bytes_left_to_compare;

						if (demod->my_access_funct->
						    read_block_func(dev_addr,
								  curr_addr,
								  (u16)
								  bytes_to_compare,
								  (u8 *)
								  mc_data_buffer,
								  0x0000) !=
						    0) {
							return -EIO;
						}

						result =
						    drxbsp_hst_memcmp(curr_ptr,
								      mc_data_buffer,
								      bytes_to_compare);

						if (result != 0)
							return -EIO;

						curr_addr +=
						    ((dr_xaddr_t)
						     (bytes_to_compare / 2));
						curr_ptr =
						    &(curr_ptr[bytes_to_compare]);
						bytes_left_to_compare -=
						    ((u32) bytes_to_compare);
					}	/* while( bytes_to_compare > DRXJ_UCODE_MAX_BUF_SIZE ) */
				}
				break;

	    /*===================================================================*/
			default:
				return -EINVAL;
				break;

			}	/* switch ( action ) */
		}

		/* if( is_mc_block_audio( block_hdr.addr ) == upload_audio_mc ) */
		/* Next block */
		mc_data += mc_block_nr_bytes;
	}			/* for( i = 0 ; i<mc_nr_of_blks ; i++ ) */

	if (!upload_audio_mc)
		ext_attr->flag_aud_mc_uploaded = false;

	return 0;
}
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

/*============================================================================*/
/*== CTRL Set/Get Config related functions ===================================*/
/*============================================================================*/

/*===== SigStrength() =========================================================*/
/**
* \fn int ctrl_sig_strength()
* \brief Retrieve signal strength.
* \param devmod Pointer to demodulator instance.
* \param sig_quality Pointer to signal strength data; range 0, .. , 100.
* \return int.
* \retval 0 sig_strength contains valid data.
* \retval -EINVAL sig_strength is NULL.
* \retval -EIO Erroneous data, sig_strength contains invalid data.

*/
static int
ctrl_sig_strength(struct drx_demod_instance *demod, u16 *sig_strength)
{
	struct drxj_data *ext_attr = NULL;
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
	int rc;

	/* Check arguments */
	if ((sig_strength == NULL) || (demod == NULL))
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	standard = ext_attr->standard;
	*sig_strength = 0;

	/* Signal strength indication for each standard */
	switch (standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
		rc = get_sig_strength(demod, sig_strength);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:
		rc = get_atv_sig_strength(demod, sig_strength);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
#endif
	case DRX_STANDARD_UNKNOWN:	/* fallthrough */
	default:
		return -EINVAL;
	}

	/* TODO */
	/* find out if signal strength is calculated in the same way for all standards */
	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \fn int ctrl_get_cfg_oob_misc()
* \brief Get current state information of OOB.
* \param pointer to struct drxj_cfg_oob_misc.
* \return int.
*
*/
#ifndef DRXJ_DIGITAL_ONLY
static int
ctrl_get_cfg_oob_misc(struct drx_demod_instance *demod, struct drxj_cfg_oob_misc *misc)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	u16 lock = 0U;
	u16 state = 0U;
	u16 data = 0U;
	u16 digital_agc_mant = 0U;
	u16 digital_agc_exp = 0U;

	/* check arguments */
	if (misc == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;

	/* TODO */
	/* check if the same registers are used for all standards (QAM/VSB/ATV) */
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_NSU_TUN_IFGAIN_W__A, &misc->agc.IFAGC, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_NSU_TUN_RFGAIN_W__A, &misc->agc.RFAGC, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, ORX_FWP_SRC_DGN_W__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	digital_agc_mant = data & ORX_FWP_SRC_DGN_W_MANT__M;
	digital_agc_exp = (data & ORX_FWP_SRC_DGN_W_EXP__M)
	    >> ORX_FWP_SRC_DGN_W_EXP__B;
	misc->agc.digital_agc = digital_agc_mant << digital_agc_exp;

	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ORX_SCU_LOCK__A, &lock, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	misc->ana_gain_lock = ((lock & 0x0001) ? true : false);
	misc->dig_gain_lock = ((lock & 0x0002) ? true : false);
	misc->freq_lock = ((lock & 0x0004) ? true : false);
	misc->phase_lock = ((lock & 0x0008) ? true : false);
	misc->sym_timing_lock = ((lock & 0x0010) ? true : false);
	misc->eq_lock = ((lock & 0x0020) ? true : false);

	rc = drxj_dap_scu_atomic_read_reg16(dev_addr, SCU_RAM_ORX_SCU_STATE__A, &state, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	misc->state = (state >> 8) & 0xff;

	return 0;
rw_error:
	return -EIO;
}
#endif

/**
* \fn int ctrl_get_cfg_vsb_misc()
* \brief Get current state information of OOB.
* \param pointer to struct drxj_cfg_oob_misc.
* \return int.
*
*/
static int
ctrl_get_cfg_vsb_misc(struct drx_demod_instance *demod, struct drxj_cfg_vsb_misc *misc)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;

	/* check arguments */
	if (misc == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;

	rc = get_vsb_symb_err(dev_addr, &misc->symb_error);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_set_cfg_agc_if()
* \brief Set IF AGC.
* \param demod demod instance
* \param agc_settings If agc configuration
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_set_cfg_agc_if(struct drx_demod_instance *demod, struct drxj_cfg_agc *agc_settings)
{
	/* check arguments */
	if (agc_settings == NULL)
		return -EINVAL;

	switch (agc_settings->ctrl_mode) {
	case DRX_AGC_CTRL_AUTO:	/* fallthrough */
	case DRX_AGC_CTRL_USER:	/* fallthrough */
	case DRX_AGC_CTRL_OFF:	/* fallthrough */
		break;
	default:
		return -EINVAL;
	}

	/* Distpatch */
	switch (agc_settings->standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:
#endif
		return set_agc_if(demod, agc_settings, true);
	case DRX_STANDARD_UNKNOWN:
	default:
		return -EINVAL;
	}

	return 0;
}

/*============================================================================*/

/**
* \fn int ctrl_get_cfg_agc_if()
* \brief Retrieve IF AGC settings.
* \param demod demod instance
* \param agc_settings If agc configuration
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_get_cfg_agc_if(struct drx_demod_instance *demod, struct drxj_cfg_agc *agc_settings)
{
	/* check arguments */
	if (agc_settings == NULL)
		return -EINVAL;

	/* Distpatch */
	switch (agc_settings->standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:
#endif
		return get_agc_if(demod, agc_settings);
	case DRX_STANDARD_UNKNOWN:
	default:
		return -EINVAL;
	}

	return 0;
}

/*============================================================================*/

/**
* \fn int ctrl_set_cfg_agc_rf()
* \brief Set RF AGC.
* \param demod demod instance
* \param agc_settings rf agc configuration
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_set_cfg_agc_rf(struct drx_demod_instance *demod, struct drxj_cfg_agc *agc_settings)
{
	/* check arguments */
	if (agc_settings == NULL)
		return -EINVAL;

	switch (agc_settings->ctrl_mode) {
	case DRX_AGC_CTRL_AUTO:	/* fallthrough */
	case DRX_AGC_CTRL_USER:	/* fallthrough */
	case DRX_AGC_CTRL_OFF:
		break;
	default:
		return -EINVAL;
	}

	/* Distpatch */
	switch (agc_settings->standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:
#endif
		return set_agc_rf(demod, agc_settings, true);
	case DRX_STANDARD_UNKNOWN:
	default:
		return -EINVAL;
	}

	return 0;
}

/*============================================================================*/

/**
* \fn int ctrl_get_cfg_agc_rf()
* \brief Retrieve RF AGC settings.
* \param demod demod instance
* \param agc_settings Rf agc configuration
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_get_cfg_agc_rf(struct drx_demod_instance *demod, struct drxj_cfg_agc *agc_settings)
{
	/* check arguments */
	if (agc_settings == NULL)
		return -EINVAL;

	/* Distpatch */
	switch (agc_settings->standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:
#endif
		return get_agc_rf(demod, agc_settings);
	case DRX_STANDARD_UNKNOWN:
	default:
		return -EINVAL;
	}

	return 0;
}

/*============================================================================*/

/**
* \fn int ctrl_get_cfg_agc_internal()
* \brief Retrieve internal AGC value.
* \param demod demod instance
* \param u16
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_get_cfg_agc_internal(struct drx_demod_instance *demod, u16 *agc_internal)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;
	enum drx_lock_status lock_status = DRX_NOT_LOCKED;
	struct drxj_data *ext_attr = NULL;
	u16 iqm_cf_scale_sh = 0;
	u16 iqm_cf_power = 0;
	u16 iqm_cf_amp = 0;
	u16 iqm_cf_gain = 0;

	/* check arguments */
	if (agc_internal == NULL)
		return -EINVAL;
	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	rc = ctrl_lock_status(demod, &lock_status);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (lock_status != DRXJ_DEMOD_LOCK && lock_status != DRX_LOCKED) {
		*agc_internal = 0;
		return 0;
	}

	/* Distpatch */
	switch (ext_attr->standard) {
	case DRX_STANDARD_8VSB:
		iqm_cf_gain = 57;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		switch (ext_attr->constellation) {
		case DRX_CONSTELLATION_QAM256:
		case DRX_CONSTELLATION_QAM128:
		case DRX_CONSTELLATION_QAM32:
		case DRX_CONSTELLATION_QAM16:
			iqm_cf_gain = 57;
			break;
		case DRX_CONSTELLATION_QAM64:
			iqm_cf_gain = 56;
			break;
		default:
			return -EIO;
		}
		break;
#endif
	default:
		return -EINVAL;
	}

	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_CF_POW__A, &iqm_cf_power, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_CF_SCALE_SH__A, &iqm_cf_scale_sh, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.read_reg16func(dev_addr, IQM_CF_AMP__A, &iqm_cf_amp, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* IQM_CF_PWR_CORRECTION_dB = 3;
	   P5dB =10*log10(IQM_CF_POW)+12-6*9-IQM_CF_PWR_CORRECTION_dB; */
	/* P4dB = P5dB -20*log10(IQM_CF_AMP)-6*10
	   -IQM_CF_Gain_dB-18+6*(27-IQM_CF_SCALE_SH*2-10)
	   +6*7+10*log10(1+0.115/4); */
	/* PadcdB = P4dB +3 -6 +60; dBmV */
	*agc_internal = (u16) (log1_times100(iqm_cf_power)
				- 2 * log1_times100(iqm_cf_amp)
				- iqm_cf_gain - 120 * iqm_cf_scale_sh + 781);

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_set_cfg_pre_saw()
* \brief Set Pre-saw reference.
* \param demod demod instance
* \param u16 *
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_set_cfg_pre_saw(struct drx_demod_instance *demod, struct drxj_cfg_pre_saw *pre_saw)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* check arguments */
	if ((pre_saw == NULL) || (pre_saw->reference > IQM_AF_PDREF__M)
	    ) {
		return -EINVAL;
	}

	/* Only if standard is currently active */
	if ((ext_attr->standard == pre_saw->standard) ||
	    (DRXJ_ISQAMSTD(ext_attr->standard) &&
	     DRXJ_ISQAMSTD(pre_saw->standard)) ||
	    (DRXJ_ISATVSTD(ext_attr->standard) &&
	     DRXJ_ISATVSTD(pre_saw->standard))) {
		rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_PDREF__A, pre_saw->reference, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* Store pre-saw settings */
	switch (pre_saw->standard) {
	case DRX_STANDARD_8VSB:
		ext_attr->vsb_pre_saw_cfg = *pre_saw;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		ext_attr->qam_pre_saw_cfg = *pre_saw;
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:
		ext_attr->atv_pre_saw_cfg = *pre_saw;
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_set_cfg_afe_gain()
* \brief Set AFE Gain.
* \param demod demod instance
* \param u16 *
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_set_cfg_afe_gain(struct drx_demod_instance *demod, struct drxj_cfg_afe_gain *afe_gain)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	int rc;
	u8 gain = 0;

	/* check arguments */
	if (afe_gain == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	switch (afe_gain->standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
		/* Do nothing */
		break;
	default:
		return -EINVAL;
	}

	/* TODO PGA gain is also written by microcode (at least by QAM and VSB)
	   So I (PJ) think interface requires choice between auto, user mode */

	if (afe_gain->gain >= 329)
		gain = 15;
	else if (afe_gain->gain <= 147)
		gain = 0;
	else
		gain = (afe_gain->gain - 140 + 6) / 13;

	/* Only if standard is currently active */
	if (ext_attr->standard == afe_gain->standard) {
			rc = DRXJ_DAP.write_reg16func(dev_addr, IQM_AF_PGA_GAIN__A, gain, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}

	/* Store AFE Gain settings */
	switch (afe_gain->standard) {
	case DRX_STANDARD_8VSB:
		ext_attr->vsb_pga_cfg = gain * 13 + 140;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		ext_attr->qam_pga_cfg = gain * 13 + 140;
		break;
#endif
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_get_cfg_pre_saw()
* \brief Get Pre-saw reference setting.
* \param demod demod instance
* \param u16 *
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_get_cfg_pre_saw(struct drx_demod_instance *demod, struct drxj_cfg_pre_saw *pre_saw)
{
	struct drxj_data *ext_attr = NULL;

	/* check arguments */
	if (pre_saw == NULL)
		return -EINVAL;

	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	switch (pre_saw->standard) {
	case DRX_STANDARD_8VSB:
		*pre_saw = ext_attr->vsb_pre_saw_cfg;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		*pre_saw = ext_attr->qam_pre_saw_cfg;
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:
		ext_attr->atv_pre_saw_cfg.standard = DRX_STANDARD_NTSC;
		*pre_saw = ext_attr->atv_pre_saw_cfg;
		break;
	case DRX_STANDARD_FM:
		ext_attr->atv_pre_saw_cfg.standard = DRX_STANDARD_FM;
		*pre_saw = ext_attr->atv_pre_saw_cfg;
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

/*============================================================================*/

/**
* \fn int ctrl_get_cfg_afe_gain()
* \brief Get AFE Gain.
* \param demod demod instance
* \param u16 *
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_get_cfg_afe_gain(struct drx_demod_instance *demod, struct drxj_cfg_afe_gain *afe_gain)
{
	struct drxj_data *ext_attr = NULL;

	/* check arguments */
	if (afe_gain == NULL)
		return -EINVAL;

	ext_attr = demod->my_ext_attr;

	switch (afe_gain->standard) {
	case DRX_STANDARD_8VSB:
		afe_gain->gain = ext_attr->vsb_pga_cfg;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		afe_gain->gain = ext_attr->qam_pga_cfg;
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

/*============================================================================*/

/**
* \fn int ctrl_get_fec_meas_seq_count()
* \brief Get FEC measurement sequnce number.
* \param demod demod instance
* \param u16 *
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_get_fec_meas_seq_count(struct drx_demod_instance *demod, u16 *fec_meas_seq_count)
{
	int rc;
	/* check arguments */
	if (fec_meas_seq_count == NULL)
		return -EINVAL;

	rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SCU_RAM_FEC_MEAS_COUNT__A, fec_meas_seq_count, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_get_accum_cr_rs_cw_err()
* \brief Get accumulative corrected RS codeword number.
* \param demod demod instance
* \param u32 *
* \return int.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static int
ctrl_get_accum_cr_rs_cw_err(struct drx_demod_instance *demod, u32 *accum_cr_rs_cw_err)
{
	int rc;
	if (accum_cr_rs_cw_err == NULL)
		return -EINVAL;

	rc = DRXJ_DAP.read_reg32func(demod->my_i2c_dev_addr, SCU_RAM_FEC_ACCUM_CW_CORRECTED_LO__A, accum_cr_rs_cw_err, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/**
* \fn int ctrl_set_cfg()
* \brief Set 'some' configuration of the device.
* \param devmod Pointer to demodulator instance.
* \param config Pointer to configuration parameters (type and data).
* \return int.

*/
static int ctrl_set_cfg(struct drx_demod_instance *demod, struct drx_cfg *config)
{
	int rc;

	if (config == NULL)
		return -EINVAL;

	do {
		u16 dummy;
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SCU_RAM_VERSION_HI__A, &dummy, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} while (0);
	switch (config->cfg_type) {
	case DRX_CFG_MPEG_OUTPUT:
		return ctrl_set_cfg_mpeg_output(demod,
					    (struct drx_cfg_mpeg_output *) config->
					    cfg_data);
	case DRX_CFG_PINS_SAFE_MODE:
		return ctrl_set_cfg_pdr_safe_mode(demod, (bool *)config->cfg_data);
	case DRXJ_CFG_AGC_RF:
		return ctrl_set_cfg_agc_rf(demod, (struct drxj_cfg_agc *) config->cfg_data);
	case DRXJ_CFG_AGC_IF:
		return ctrl_set_cfg_agc_if(demod, (struct drxj_cfg_agc *) config->cfg_data);
	case DRXJ_CFG_PRE_SAW:
		return ctrl_set_cfg_pre_saw(demod,
					(struct drxj_cfg_pre_saw *) config->cfg_data);
	case DRXJ_CFG_AFE_GAIN:
		return ctrl_set_cfg_afe_gain(demod,
					 (struct drxj_cfg_afe_gain *) config->cfg_data);
	case DRXJ_CFG_SMART_ANT:
		return ctrl_set_cfg_smart_ant(demod,
					  (struct drxj_cfg_smart_ant *) (config->
								cfg_data));
	case DRXJ_CFG_RESET_PACKET_ERR:
		return ctrl_set_cfg_reset_pkt_err(demod);
#ifndef DRXJ_DIGITAL_ONLY
	case DRXJ_CFG_OOB_PRE_SAW:
		return ctrl_set_cfg_oob_pre_saw(demod, (u16 *)(config->cfg_data));
	case DRXJ_CFG_OOB_LO_POW:
		return ctrl_set_cfg_oob_lo_power(demod,
					    (enum drxj_cfg_oob_lo_power *) (config->
								    cfg_data));
	case DRXJ_CFG_ATV_MISC:
		return ctrl_set_cfg_atv_misc(demod,
					 (struct drxj_cfg_atv_misc *) config->cfg_data);
	case DRXJ_CFG_ATV_EQU_COEF:
		return ctrl_set_cfg_atv_equ_coef(demod,
					    (struct drxj_cfg_atv_equ_coef *) config->
					    cfg_data);
	case DRXJ_CFG_ATV_OUTPUT:
		return ctrl_set_cfg_atv_output(demod,
					   (struct drxj_cfg_atv_output *) config->
					   cfg_data);
#endif
	case DRXJ_CFG_MPEG_OUTPUT_MISC:
		return ctrl_set_cfg_mpeg_output_misc(demod,
						(struct drxj_cfg_mpeg_output_misc *)
						config->cfg_data);
#ifndef DRXJ_EXCLUDE_AUDIO
	case DRX_CFG_AUD_VOLUME:
		return aud_ctrl_set_cfg_volume(demod,
					   (struct drx_cfg_aud_volume *) config->
					   cfg_data);
	case DRX_CFG_I2S_OUTPUT:
		return aud_ctrl_set_cfg_output_i2s(demod,
					      (struct drx_cfg_i2s_output *) config->
					      cfg_data);
	case DRX_CFG_AUD_AUTOSOUND:
		return aud_ctr_setl_cfg_auto_sound(demod, (enum drx_cfg_aud_auto_sound *)
					      config->cfg_data);
	case DRX_CFG_AUD_ASS_THRES:
		return aud_ctrl_set_cfg_ass_thres(demod, (struct drx_cfg_aud_ass_thres *)
					     config->cfg_data);
	case DRX_CFG_AUD_CARRIER:
		return aud_ctrl_set_cfg_carrier(demod,
					    (struct drx_cfg_aud_carriers *) config->
					    cfg_data);
	case DRX_CFG_AUD_DEVIATION:
		return aud_ctrl_set_cfg_dev(demod,
					(enum drx_cfg_aud_deviation *) config->
					cfg_data);
	case DRX_CFG_AUD_PRESCALE:
		return aud_ctrl_set_cfg_prescale(demod,
					     (struct drx_cfg_aud_prescale *) config->
					     cfg_data);
	case DRX_CFG_AUD_MIXER:
		return aud_ctrl_set_cfg_mixer(demod,
					  (struct drx_cfg_aud_mixer *) config->cfg_data);
	case DRX_CFG_AUD_AVSYNC:
		return aud_ctrl_set_cfg_av_sync(demod,
					   (enum drx_cfg_aud_av_sync *) config->
					   cfg_data);

#endif
	default:
		return -EINVAL;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/

/**
* \fn int ctrl_get_cfg()
* \brief Get 'some' configuration of the device.
* \param devmod Pointer to demodulator instance.
* \param config Pointer to configuration parameters (type and data).
* \return int.
*/

static int ctrl_get_cfg(struct drx_demod_instance *demod, struct drx_cfg *config)
{
	int rc;

	if (config == NULL)
		return -EINVAL;

	do {
		u16 dummy;
		rc = DRXJ_DAP.read_reg16func(demod->my_i2c_dev_addr, SCU_RAM_VERSION_HI__A, &dummy, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} while (0);

	switch (config->cfg_type) {
	case DRX_CFG_MPEG_OUTPUT:
		return ctrl_get_cfg_mpeg_output(demod,
					    (struct drx_cfg_mpeg_output *) config->
					    cfg_data);
	case DRX_CFG_PINS_SAFE_MODE:
		return ctrl_get_cfg_pdr_safe_mode(demod, (bool *)config->cfg_data);
	case DRXJ_CFG_AGC_RF:
		return ctrl_get_cfg_agc_rf(demod, (struct drxj_cfg_agc *) config->cfg_data);
	case DRXJ_CFG_AGC_IF:
		return ctrl_get_cfg_agc_if(demod, (struct drxj_cfg_agc *) config->cfg_data);
	case DRXJ_CFG_AGC_INTERNAL:
		return ctrl_get_cfg_agc_internal(demod, (u16 *)config->cfg_data);
	case DRXJ_CFG_PRE_SAW:
		return ctrl_get_cfg_pre_saw(demod,
					(struct drxj_cfg_pre_saw *) config->cfg_data);
	case DRXJ_CFG_AFE_GAIN:
		return ctrl_get_cfg_afe_gain(demod,
					 (struct drxj_cfg_afe_gain *) config->cfg_data);
	case DRXJ_CFG_ACCUM_CR_RS_CW_ERR:
		return ctrl_get_accum_cr_rs_cw_err(demod, (u32 *)config->cfg_data);
	case DRXJ_CFG_FEC_MERS_SEQ_COUNT:
		return ctrl_get_fec_meas_seq_count(demod, (u16 *)config->cfg_data);
	case DRXJ_CFG_VSB_MISC:
		return ctrl_get_cfg_vsb_misc(demod,
					 (struct drxj_cfg_vsb_misc *) config->cfg_data);
	case DRXJ_CFG_SYMBOL_CLK_OFFSET:
		return ctrl_get_cfg_symbol_clock_offset(demod,
						   (s32 *)config->cfg_data);
#ifndef DRXJ_DIGITAL_ONLY
	case DRXJ_CFG_OOB_MISC:
		return ctrl_get_cfg_oob_misc(demod,
					 (struct drxj_cfg_oob_misc *) config->cfg_data);
	case DRXJ_CFG_OOB_PRE_SAW:
		return ctrl_get_cfg_oob_pre_saw(demod, (u16 *)(config->cfg_data));
	case DRXJ_CFG_OOB_LO_POW:
		return ctrl_get_cfg_oob_lo_power(demod,
					    (enum drxj_cfg_oob_lo_power *) (config->
								    cfg_data));
	case DRXJ_CFG_ATV_EQU_COEF:
		return ctrl_get_cfg_atv_equ_coef(demod,
					    (struct drxj_cfg_atv_equ_coef *) config->
					    cfg_data);
	case DRXJ_CFG_ATV_MISC:
		return ctrl_get_cfg_atv_misc(demod,
					 (struct drxj_cfg_atv_misc *) config->cfg_data);
	case DRXJ_CFG_ATV_OUTPUT:
		return ctrl_get_cfg_atv_output(demod,
					   (struct drxj_cfg_atv_output *) config->
					   cfg_data);
	case DRXJ_CFG_ATV_AGC_STATUS:
		return ctrl_get_cfg_atv_agc_status(demod,
					      (struct drxj_cfg_atv_agc_status *) config->
					      cfg_data);
#endif
	case DRXJ_CFG_MPEG_OUTPUT_MISC:
		return ctrl_get_cfg_mpeg_output_misc(demod,
						(struct drxj_cfg_mpeg_output_misc *)
						config->cfg_data);
	case DRXJ_CFG_HW_CFG:
		return ctrl_get_cfg_hw_cfg(demod,
				       (struct drxj_cfg_hw_cfg *) config->cfg_data);
#ifndef DRXJ_EXCLUDE_AUDIO
	case DRX_CFG_AUD_VOLUME:
		return aud_ctrl_get_cfg_volume(demod,
					   (struct drx_cfg_aud_volume *) config->
					   cfg_data);
	case DRX_CFG_I2S_OUTPUT:
		return aud_ctrl_get_cfg_output_i2s(demod,
					      (struct drx_cfg_i2s_output *) config->
					      cfg_data);

	case DRX_CFG_AUD_RDS:
		return aud_ctrl_get_cfg_rds(demod,
					(struct drx_cfg_aud_rds *) config->cfg_data);
	case DRX_CFG_AUD_AUTOSOUND:
		return aud_ctrl_get_cfg_auto_sound(demod,
					      (enum drx_cfg_aud_auto_sound *) config->
					      cfg_data);
	case DRX_CFG_AUD_ASS_THRES:
		return aud_ctrl_get_cfg_ass_thres(demod,
					     (struct drx_cfg_aud_ass_thres *) config->
					     cfg_data);
	case DRX_CFG_AUD_CARRIER:
		return aud_ctrl_get_cfg_carrier(demod,
					    (struct drx_cfg_aud_carriers *) config->
					    cfg_data);
	case DRX_CFG_AUD_DEVIATION:
		return aud_ctrl_get_cfg_dev(demod,
					(enum drx_cfg_aud_deviation *) config->
					cfg_data);
	case DRX_CFG_AUD_PRESCALE:
		return aud_ctrl_get_cfg_prescale(demod,
					     (struct drx_cfg_aud_prescale *) config->
					     cfg_data);
	case DRX_CFG_AUD_MIXER:
		return aud_ctrl_get_cfg_mixer(demod,
					  (struct drx_cfg_aud_mixer *) config->cfg_data);
	case DRX_CFG_AUD_AVSYNC:
		return aud_ctrl_get_cfg_av_sync(demod,
					   (enum drx_cfg_aud_av_sync *) config->
					   cfg_data);
#endif

	default:
		return -EINVAL;
	}

	return 0;
rw_error:
	return -EIO;
}

/*=============================================================================
===== EXPORTED FUNCTIONS ====================================================*/

/**
* \fn drxj_open()
* \brief Open the demod instance, configure device, configure drxdriver
* \return Status_t Return status.
*
* drxj_open() can be called with a NULL ucode image => no ucode upload.
* This means that drxj_open() must NOT contain SCU commands or, in general,
* rely on SCU or AUD ucode to be present.
*
*/
int drxj_open(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	struct drx_common_attr *common_attr = NULL;
	u32 driver_version = 0;
	struct drxu_code_info ucode_info;
	struct drx_cfg_mpeg_output cfg_mpeg_output;
	int rc;

	/* Check arguments */
	if (demod->my_ext_attr == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;

	rc = power_up_device(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	common_attr->current_power_mode = DRX_POWER_UP;

	/* has to be in front of setIqmAf and setOrxNsuAox */
	rc = get_device_capabilities(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Soft reset of sys- and osc-clockdomain */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_CC_SOFT_RST__A, (SIO_CC_SOFT_RST_SYS__M | SIO_CC_SOFT_RST_OSC__M), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxbsp_hst_sleep(1);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* TODO first make sure that everything keeps working before enabling this */
	/* PowerDownAnalogBlocks() */
	rc = DRXJ_DAP.write_reg16func(dev_addr, ATV_TOP_STDBY__A, (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE) | ATV_TOP_STDBY_SIF_STDBY_STANDBY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = set_iqm_af(demod, false);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = set_orx_nsu_aox(demod, false);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = init_hi(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* disable mpegoutput pins */
	cfg_mpeg_output.enable_mpeg_output = false;
	rc = ctrl_set_cfg_mpeg_output(demod, &cfg_mpeg_output);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* Stop AUD Inform SetAudio it will need to do all setting */
	rc = power_down_aud(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* Stop SCU */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Upload microcode */
	if (common_attr->microcode_file != NULL) {
		/* Dirty trick to use common ucode upload & verify,
		   pretend device is already open */
		common_attr->is_opened = true;
		ucode_info.mc_file = common_attr->microcode_file;

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
		/* Upload microcode without audio part */
		rc = ctrl_u_code_upload(demod, &ucode_info, UCODE_UPLOAD, false);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
#else
		rc = drx_ctrl(demod, DRX_CTRL_LOAD_UCODE, &ucode_info);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */
		if (common_attr->verify_microcode == true) {
#ifdef DRXJ_SPLIT_UCODE_UPLOAD
			rc = ctrl_u_code_upload(demod, &ucode_info, UCODE_VERIFY, false);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
#else
			rc = drx_ctrl(demod, DRX_CTRL_VERIFY_UCODE, &ucode_info);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */
		}
		common_attr->is_opened = false;
	}

	/* Run SCU for a little while to initialize microcode version numbers */
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Open tuner instance */
	if (demod->my_tuner != NULL) {
		demod->my_tuner->my_common_attr->my_user_data = (void *)demod;

		if (common_attr->tuner_port_nr == 1) {
			bool bridge_closed = true;
			rc = ctrl_i2c_bridge(demod, &bridge_closed);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}

		rc = drxbsp_tuner_open(demod->my_tuner);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		if (common_attr->tuner_port_nr == 1) {
			bool bridge_closed = false;
			rc = ctrl_i2c_bridge(demod, &bridge_closed);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		common_attr->tuner_min_freq_rf =
		    ((demod->my_tuner)->my_common_attr->min_freq_rf);
		common_attr->tuner_max_freq_rf =
		    ((demod->my_tuner)->my_common_attr->max_freq_rf);
	}

	/* Initialize scan timeout */
	common_attr->scan_demod_lock_timeout = DRXJ_SCAN_TIMEOUT;
	common_attr->scan_desired_lock = DRX_LOCKED;

	drxj_reset_mode(ext_attr);
	ext_attr->standard = DRX_STANDARD_UNKNOWN;

	rc = smart_ant_init(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Stamp driver version number in SCU data RAM in BCD code
	   Done to enable field application engineers to retreive drxdriver version
	   via I2C from SCU RAM
	 */
	driver_version = (VERSION_MAJOR / 100) % 10;
	driver_version <<= 4;
	driver_version += (VERSION_MAJOR / 10) % 10;
	driver_version <<= 4;
	driver_version += (VERSION_MAJOR % 10);
	driver_version <<= 4;
	driver_version += (VERSION_MINOR % 10);
	driver_version <<= 4;
	driver_version += (VERSION_PATCH / 1000) % 10;
	driver_version <<= 4;
	driver_version += (VERSION_PATCH / 100) % 10;
	driver_version <<= 4;
	driver_version += (VERSION_PATCH / 10) % 10;
	driver_version <<= 4;
	driver_version += (VERSION_PATCH % 10);
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_DRIVER_VER_HI__A, (u16)(driver_version >> 16), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_RAM_DRIVER_VER_LO__A, (u16)(driver_version & 0xFFFF), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* refresh the audio data structure with default */
	ext_attr->aud_data = drxj_default_aud_data_g;

	return 0;
rw_error:
	common_attr->is_opened = false;
	return -EIO;
}

/*============================================================================*/
/**
* \fn drxj_close()
* \brief Close the demod instance, power down the device
* \return Status_t Return status.
*
*/
int drxj_close(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drx_common_attr *common_attr = demod->my_common_attr;
	int rc;
	enum drx_power_mode power_mode = DRX_POWER_UP;

	/* power up */
	rc = ctrl_power_mode(demod, &power_mode);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if (demod->my_tuner != NULL) {
		/* Check if bridge is used */
		if (common_attr->tuner_port_nr == 1) {
			bool bridge_closed = true;
			rc = ctrl_i2c_bridge(demod, &bridge_closed);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		rc = drxbsp_tuner_close(demod->my_tuner);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (common_attr->tuner_port_nr == 1) {
			bool bridge_closed = false;
			rc = ctrl_i2c_bridge(demod, &bridge_closed);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
	}

	rc = DRXJ_DAP.write_reg16func(dev_addr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	power_mode = DRX_POWER_DOWN;
	rc = ctrl_power_mode(demod, &power_mode);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return -EIO;
}

/*============================================================================*/
/**
* \fn drxj_ctrl()
* \brief DRXJ specific control function
* \return Status_t Return status.
*/
int
drxj_ctrl(struct drx_demod_instance *demod, u32 ctrl, void *ctrl_data)
{
	switch (ctrl) {
      /*======================================================================*/
	case DRX_CTRL_SET_CHANNEL:
		{
			return ctrl_set_channel(demod, (struct drx_channel *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_CHANNEL:
		{
			return ctrl_get_channel(demod, (struct drx_channel *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SIG_QUALITY:
		{
			return ctrl_sig_quality(demod,
					      (struct drx_sig_quality *) ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SIG_STRENGTH:
		{
			return ctrl_sig_strength(demod, (u16 *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_CONSTEL:
		{
			return ctrl_constel(demod, (struct drx_complex *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SET_CFG:
		{
			return ctrl_set_cfg(demod, (struct drx_cfg *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_CFG:
		{
			return ctrl_get_cfg(demod, (struct drx_cfg *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_I2C_BRIDGE:
		{
			return ctrl_i2c_bridge(demod, (bool *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_LOCK_STATUS:
		{
			return ctrl_lock_status(demod,
					      (enum drx_lock_status *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SET_STANDARD:
		{
			return ctrl_set_standard(demod,
					       (enum drx_standard *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_STANDARD:
		{
			return ctrl_get_standard(demod,
					       (enum drx_standard *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_POWER_MODE:
		{
			return ctrl_power_mode(demod, (enum drx_power_mode *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_VERSION:
		{
			return ctrl_version(demod,
					   (struct drx_version_list **)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_PROBE_DEVICE:
		{
			return ctrl_probe_device(demod);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SET_OOB:
		{
			return ctrl_set_oob(demod, (struct drxoob *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_OOB:
		{
			return ctrl_get_oob(demod, (struct drxoob_status *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SET_UIO_CFG:
		{
			return ctrl_set_uio_cfg(demod, (struct drxuio_cfg *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_UIO_CFG:
		{
			return ctrl_getuio_cfg(demod, (struct drxuio_cfg *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_UIO_READ:
		{
			return ctrl_uio_read(demod, (struct drxuio_data *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_UIO_WRITE:
		{
			return ctrl_uio_write(demod, (struct drxuio_data *)ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_AUD_SET_STANDARD:
		{
			return aud_ctrl_set_standard(demod,
						  (enum drx_aud_standard *) ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_AUD_GET_STANDARD:
		{
			return aud_ctrl_get_standard(demod,
						  (enum drx_aud_standard *) ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_AUD_GET_STATUS:
		{
			return aud_ctrl_get_status(demod,
						(struct drx_aud_status *) ctrl_data);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_AUD_BEEP:
		{
			return aud_ctrl_beep(demod, (struct drx_aud_beep *)ctrl_data);
		}
		break;

      /*======================================================================*/
	case DRX_CTRL_I2C_READWRITE:
		{
			return ctrl_i2c_write_read(demod,
						(struct drxi2c_data *) ctrl_data);
		}
		break;
#ifdef DRXJ_SPLIT_UCODE_UPLOAD
	case DRX_CTRL_LOAD_UCODE:
		{
			return ctrl_u_code_upload(demod,
					       (p_drxu_code_info_t) ctrl_data,
					       UCODE_UPLOAD, false);
		}
		break;
	case DRX_CTRL_VERIFY_UCODE:
		{
			return ctrl_u_code_upload(demod,
					       (p_drxu_code_info_t) ctrl_data,
					       UCODE_VERIFY, false);
		}
		break;
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */
	case DRX_CTRL_VALIDATE_UCODE:
		{
			return ctrl_validate_u_code(demod);
		}
		break;
	default:
		return -ENOTSUPP;
	}
	return 0;
}

/*
 * Microcode related functions
 */

/**
 * drx_u_code_compute_crc	- Compute CRC of block of microcode data.
 * @block_data: Pointer to microcode data.
 * @nr_words:   Size of microcode block (number of 16 bits words).
 *
 * returns The computed CRC residue.
 */
static u16 drx_u_code_compute_crc(u8 *block_data, u16 nr_words)
{
	u16 i = 0;
	u16 j = 0;
	u32 crc_word = 0;
	u32 carry = 0;

	while (i < nr_words) {
		crc_word |= (u32)be16_to_cpu(*(u32 *)(block_data));
		for (j = 0; j < 16; j++) {
			crc_word <<= 1;
			if (carry != 0)
				crc_word ^= 0x80050000UL;
			carry = crc_word & 0x80000000UL;
		}
		i++;
		block_data += (sizeof(u16));
	}
	return (u16)(crc_word >> 16);
}

/**
 * drx_check_firmware - checks if the loaded firmware is valid
 *
 * @demod:	demod structure
 * @mc_data:	pointer to the start of the firmware
 * @size:	firmware size
 */
static int drx_check_firmware(struct drx_demod_instance *demod, u8 *mc_data,
			  unsigned size)
{
	struct drxu_code_block_hdr block_hdr;
	int i;
	unsigned count = 2 * sizeof(u16);
	u32 mc_dev_type, mc_version, mc_base_version;
	u16 mc_nr_of_blks = be16_to_cpu(*(u32 *)(mc_data + sizeof(u16)));

	/*
	 * Scan microcode blocks first for version info
	 * and firmware check
	 */

	/* Clear version block */
	DRX_ATTR_MCRECORD(demod).aux_type = 0;
	DRX_ATTR_MCRECORD(demod).mc_dev_type = 0;
	DRX_ATTR_MCRECORD(demod).mc_version = 0;
	DRX_ATTR_MCRECORD(demod).mc_base_version = 0;

	for (i = 0; i < mc_nr_of_blks; i++) {
		if (count + 3 * sizeof(u16) + sizeof(u32) > size)
			goto eof;

		/* Process block header */
		block_hdr.addr = be32_to_cpu(*(u32 *)(mc_data + count));
		count += sizeof(u32);
		block_hdr.size = be16_to_cpu(*(u32 *)(mc_data + count));
		count += sizeof(u16);
		block_hdr.flags = be16_to_cpu(*(u32 *)(mc_data + count));
		count += sizeof(u16);
		block_hdr.CRC = be16_to_cpu(*(u32 *)(mc_data + count));
		count += sizeof(u16);

		pr_debug("%u: addr %u, size %u, flags 0x%04x, CRC 0x%04x\n",
			count, block_hdr.addr, block_hdr.size, block_hdr.flags,
			block_hdr.CRC);

		if (block_hdr.flags & 0x8) {
			u8 *auxblk = ((void *)mc_data) + block_hdr.addr;
			u16 auxtype;

			if (block_hdr.addr + sizeof(u16) > size)
				goto eof;

			auxtype = be16_to_cpu(*(u32 *)(auxblk));

			/* Aux block. Check type */
			if (DRX_ISMCVERTYPE(auxtype)) {
				if (block_hdr.addr + 2 * sizeof(u16) + 2 * sizeof (u32) > size)
					goto eof;

				auxblk += sizeof(u16);
				mc_dev_type = be32_to_cpu(*(u32 *)(auxblk));
				auxblk += sizeof(u32);
				mc_version = be32_to_cpu(*(u32 *)(auxblk));
				auxblk += sizeof(u32);
				mc_base_version = be32_to_cpu(*(u32 *)(auxblk));

				DRX_ATTR_MCRECORD(demod).aux_type = auxtype;
				DRX_ATTR_MCRECORD(demod).mc_dev_type = mc_dev_type;
				DRX_ATTR_MCRECORD(demod).mc_version = mc_version;
				DRX_ATTR_MCRECORD(demod).mc_base_version = mc_base_version;

				pr_info("Firmware dev %x, ver %x, base ver %x\n",
					mc_dev_type, mc_version, mc_base_version);

			}
		} else if (count + block_hdr.size * sizeof(u16) > size)
			goto eof;

		count += block_hdr.size * sizeof(u16);
	}
	return 0;
eof:
	pr_err("Firmware is truncated at pos %u/%u\n", count, size);
	return -EINVAL;
}

/**
 * drx_ctrl_u_code - Handle microcode upload or verify.
 * @dev_addr: Address of device.
 * @mc_info:  Pointer to information about microcode data.
 * @action:  Either UCODE_UPLOAD or UCODE_VERIFY
 *
 * This function returns:
 *	0:
 *		- In case of UCODE_UPLOAD: code is successfully uploaded.
 *               - In case of UCODE_VERIFY: image on device is equal to
 *		  image provided to this control function.
 *	-EIO:
 *		- In case of UCODE_UPLOAD: I2C error.
 *		- In case of UCODE_VERIFY: I2C error or image on device
 *		  is not equal to image provided to this control function.
 * 	-EINVAL:
 *		- Invalid arguments.
 *		- Provided image is corrupt
 */
static int drx_ctrl_u_code(struct drx_demod_instance *demod,
		       struct drxu_code_info *mc_info,
		       enum drxu_code_action action)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	u16 i = 0;
	u16 mc_nr_of_blks = 0;
	u16 mc_magic_word = 0;
	const u8 *mc_data_init = NULL;
	u8 *mc_data = NULL;
	unsigned size;
	char *mc_file = mc_info->mc_file;

	/* Check arguments */
	if (!mc_info || !mc_file)
		return -EINVAL;

	if (!demod->firmware) {
		const struct firmware *fw = NULL;

		rc = request_firmware(&fw, mc_file, demod->i2c->dev.parent);
		if (rc < 0) {
			pr_err("Couldn't read firmware %s\n", mc_file);
			return -ENOENT;
		}
		demod->firmware = fw;

		if (demod->firmware->size < 2 * sizeof(u16)) {
			rc = -EINVAL;
			pr_err("Firmware is too short!\n");
			goto release;
		}

		pr_info("Firmware %s, size %zu\n",
			mc_file, demod->firmware->size);
	}

	mc_data_init = demod->firmware->data;
	size = demod->firmware->size;

	mc_data = (void *)mc_data_init;
	/* Check data */
	mc_magic_word = be16_to_cpu(*(u32 *)(mc_data));
	mc_data += sizeof(u16);
	mc_nr_of_blks = be16_to_cpu(*(u32 *)(mc_data));
	mc_data += sizeof(u16);

	if ((mc_magic_word != DRX_UCODE_MAGIC_WORD) || (mc_nr_of_blks == 0)) {
		rc = -EINVAL;
		pr_err("Firmware magic word doesn't match\n");
		goto release;
	}

	if (action == UCODE_UPLOAD) {
		rc = drx_check_firmware(demod, (u8 *)mc_data_init, size);
		if (rc)
			goto release;

		/* After scanning, validate the microcode.
		   It is also valid if no validation control exists.
		 */
		rc = drx_ctrl(demod, DRX_CTRL_VALIDATE_UCODE, NULL);
		if (rc != 0 && rc != -ENOTSUPP) {
			pr_err("Validate ucode not supported\n");
			return rc;
		}
		pr_info("Uploading firmware %s\n", mc_file);
	} else if (action == UCODE_VERIFY) {
		pr_info("Verifying if firmware upload was ok.\n");
	}

	/* Process microcode blocks */
	for (i = 0; i < mc_nr_of_blks; i++) {
		struct drxu_code_block_hdr block_hdr;
		u16 mc_block_nr_bytes = 0;

		/* Process block header */
		block_hdr.addr = be32_to_cpu(*(u32 *)(mc_data));
		mc_data += sizeof(u32);
		block_hdr.size = be16_to_cpu(*(u32 *)(mc_data));
		mc_data += sizeof(u16);
		block_hdr.flags = be16_to_cpu(*(u32 *)(mc_data));
		mc_data += sizeof(u16);
		block_hdr.CRC = be16_to_cpu(*(u32 *)(mc_data));
		mc_data += sizeof(u16);

		pr_debug("%u: addr %u, size %u, flags 0x%04x, CRC 0x%04x\n",
			(unsigned)(mc_data - mc_data_init), block_hdr.addr,
			 block_hdr.size, block_hdr.flags, block_hdr.CRC);

		/* Check block header on:
		   - data larger than 64Kb
		   - if CRC enabled check CRC
		 */
		if ((block_hdr.size > 0x7FFF) ||
		    (((block_hdr.flags & DRX_UCODE_CRC_FLAG) != 0) &&
		     (block_hdr.CRC != drx_u_code_compute_crc(mc_data, block_hdr.size)))
		    ) {
			/* Wrong data ! */
			rc = -EINVAL;
			pr_err("firmware CRC is wrong\n");
			goto release;
		}

		if (!block_hdr.size)
			continue;

		mc_block_nr_bytes = block_hdr.size * ((u16) sizeof(u16));

		/* Perform the desired action */
		switch (action) {
		case UCODE_UPLOAD:	/* Upload microcode */
			if (demod->my_access_funct->write_block_func(dev_addr,
							block_hdr.addr,
							mc_block_nr_bytes,
							mc_data, 0x0000)) {
				rc = -EIO;
				pr_err("error writing firmware at pos %u\n",
				       (unsigned)(mc_data - mc_data_init));
				goto release;
			}
			break;
		case UCODE_VERIFY: {	/* Verify uploaded microcode */
			int result = 0;
			u8 mc_data_buffer[DRX_UCODE_MAX_BUF_SIZE];
			u32 bytes_to_comp = 0;
			u32 bytes_left = mc_block_nr_bytes;
			u32 curr_addr = block_hdr.addr;
			u8 *curr_ptr = mc_data;

			while (bytes_left != 0) {
				if (bytes_left > DRX_UCODE_MAX_BUF_SIZE)
					bytes_to_comp = DRX_UCODE_MAX_BUF_SIZE;
				else
					bytes_to_comp = bytes_left;

				if (demod->my_access_funct->
				    read_block_func(dev_addr,
						    curr_addr,
						    (u16)bytes_to_comp,
						    (u8 *)mc_data_buffer,
						    0x0000)) {
					pr_err("error reading firmware at pos %u\n",
					       (unsigned)(mc_data - mc_data_init));
					return -EIO;
				}

				result =drxbsp_hst_memcmp(curr_ptr,
							  mc_data_buffer,
							  bytes_to_comp);

				if (result) {
					pr_err("error verifying firmware at pos %u\n",
					       (unsigned)(mc_data - mc_data_init));
					return -EIO;
				}

				curr_addr += ((dr_xaddr_t)(bytes_to_comp / 2));
				curr_ptr =&(curr_ptr[bytes_to_comp]);
				bytes_left -=((u32) bytes_to_comp);
			}
			break;
		}
		default:
			return -EINVAL;
			break;

		}
		mc_data += mc_block_nr_bytes;
	}

	return 0;

release:
	release_firmware(demod->firmware);
	demod->firmware = NULL;

	return rc;
}

/*============================================================================*/

/**
 * drx_ctrl_version - Build list of version information.
 * @demod: A pointer to a demodulator instance.
 * @version_list: Pointer to linked list of versions.
 *
 * This function returns:
 *	0:		Version information stored in version_list
 *	-EINVAL:	Invalid arguments.
 */
static int drx_ctrl_version(struct drx_demod_instance *demod,
			struct drx_version_list **version_list)
{
	static char drx_driver_core_module_name[] = "Core driver";
	static char drx_driver_core_version_text[] =
	    DRX_VERSIONSTRING(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

	static struct drx_version drx_driver_core_version;
	static struct drx_version_list drx_driver_core_version_list;

	struct drx_version_list *demod_version_list = NULL;
	int return_status = -EIO;

	/* Check arguments */
	if (version_list == NULL)
		return -EINVAL;

	/* Get version info list from demod */
	return_status = (*(demod->my_demod_funct->ctrl_func)) (demod,
							   DRX_CTRL_VERSION,
							   (void *)
							   &demod_version_list);

	/* Always fill in the information of the driver SW . */
	drx_driver_core_version.module_type = DRX_MODULE_DRIVERCORE;
	drx_driver_core_version.module_name = drx_driver_core_module_name;
	drx_driver_core_version.v_major = VERSION_MAJOR;
	drx_driver_core_version.v_minor = VERSION_MINOR;
	drx_driver_core_version.v_patch = VERSION_PATCH;
	drx_driver_core_version.v_string = drx_driver_core_version_text;

	drx_driver_core_version_list.version = &drx_driver_core_version;
	drx_driver_core_version_list.next = (struct drx_version_list *) (NULL);

	if ((return_status == 0) && (demod_version_list != NULL)) {
		/* Append versioninfo from driver to versioninfo from demod  */
		/* Return version info in "bottom-up" order. This way, multiple
		   devices can be handled without using malloc. */
		struct drx_version_list *current_list_element = demod_version_list;
		while (current_list_element->next != NULL)
			current_list_element = current_list_element->next;
		current_list_element->next = &drx_driver_core_version_list;

		*version_list = demod_version_list;
	} else {
		/* Just return versioninfo from driver */
		*version_list = &drx_driver_core_version_list;
	}

	return 0;
}

/*
 * Exported functions
 */

/**
 * drx_open - Open a demodulator instance.
 * @demod: A pointer to a demodulator instance.
 *
 * This function returns:
 *	0:		Opened demod instance with succes.
 *	-EIO:		Driver not initialized or unable to initialize
 *			demod.
 *	-EINVAL:	Demod instance has invalid content.
 *
 */

int drx_open(struct drx_demod_instance *demod)
{
	int status = 0;

	if ((demod == NULL) ||
	    (demod->my_demod_funct == NULL) ||
	    (demod->my_common_attr == NULL) ||
	    (demod->my_ext_attr == NULL) ||
	    (demod->my_i2c_dev_addr == NULL) ||
	    (demod->my_common_attr->is_opened)) {
		return -EINVAL;
	}

	status = (*(demod->my_demod_funct->open_func)) (demod);

	if (status == 0)
		demod->my_common_attr->is_opened = true;

	return status;
}

/*============================================================================*/

/**
 * drx_close - Close device
 * @demod: A pointer to a demodulator instance.
 *
 * Free resources occupied by device instance.
 * Put device into sleep mode.
 *
 * This function returns:
 *	0:		Closed demod instance with succes.
 *	-EIO:		Driver not initialized or error during close
 *			demod.
 *	-EINVAL:	Demod instance has invalid content.
 */
int drx_close(struct drx_demod_instance *demod)
{
	int status = 0;

	if ((demod == NULL) ||
	    (demod->my_demod_funct == NULL) ||
	    (demod->my_common_attr == NULL) ||
	    (demod->my_ext_attr == NULL) ||
	    (demod->my_i2c_dev_addr == NULL) ||
	    (!demod->my_common_attr->is_opened)) {
		return -EINVAL;
	}

	status = (*(demod->my_demod_funct->close_func)) (demod);

	DRX_ATTR_ISOPENED(demod) = false;

	return status;
}
/**
 * drx_ctrl - Control the device.
 * @demod:    A pointer to a demodulator instance.
 * @ctrl:     Reference to desired control function.
 * @ctrl_data: Pointer to data structure for control function.
 *
 * Data needed or returned by the control function is stored in ctrl_data.
 *
 * This function returns:
 *	0:		Control function completed successfully.
 *	-EIO:		Driver not initialized or error during control demod.
 *	-EINVAL:	Demod instance or ctrl_data has invalid content.
 *	-ENOTSUPP:	Specified control function is not available.
 */

int drx_ctrl(struct drx_demod_instance *demod, u32 ctrl, void *ctrl_data)
{
	int status = -EIO;

	if ((demod == NULL) ||
	    (demod->my_demod_funct == NULL) ||
	    (demod->my_common_attr == NULL) ||
	    (demod->my_ext_attr == NULL) || (demod->my_i2c_dev_addr == NULL)
	    ) {
		return -EINVAL;
	}

	if (((!demod->my_common_attr->is_opened) &&
	     (ctrl != DRX_CTRL_PROBE_DEVICE) && (ctrl != DRX_CTRL_VERSION))
	    ) {
		return -EINVAL;
	}

	if ((DRX_ISPOWERDOWNMODE(demod->my_common_attr->current_power_mode) &&
	     (ctrl != DRX_CTRL_POWER_MODE) &&
	     (ctrl != DRX_CTRL_PROBE_DEVICE) &&
	     (ctrl != DRX_CTRL_NOP) && (ctrl != DRX_CTRL_VERSION)
	    )
	    ) {
		return -ENOTSUPP;
	}

	/* Fixed control functions */
	switch (ctrl) {
      /*======================================================================*/
	case DRX_CTRL_NOP:
		/* No operation */
		return 0;
		break;

      /*======================================================================*/
	case DRX_CTRL_VERSION:
		return drx_ctrl_version(demod, (struct drx_version_list **)ctrl_data);
		break;

      /*======================================================================*/
	default:
		/* Do nothing */
		break;
	}

	/* Virtual functions */
	/* First try calling function from derived class */
	status = (*(demod->my_demod_funct->ctrl_func)) (demod, ctrl, ctrl_data);
	if (status == -ENOTSUPP) {
		/* Now try calling a the base class function */
		switch (ctrl) {
	 /*===================================================================*/
		case DRX_CTRL_LOAD_UCODE:
			return drx_ctrl_u_code(demod,
					 (struct drxu_code_info *)ctrl_data,
					 UCODE_UPLOAD);
			break;

	 /*===================================================================*/
		case DRX_CTRL_VERIFY_UCODE:
			{
				return drx_ctrl_u_code(demod,
						 (struct drxu_code_info *)ctrl_data,
						 UCODE_VERIFY);
			}
			break;

	 /*===================================================================*/
		default:
			pr_err("control %d not supported\n", ctrl);
			return -ENOTSUPP;
		}
	} else {
		return status;
	}

	return 0;
}