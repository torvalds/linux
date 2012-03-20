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
*/

/**
* \file $Id: drxj.c,v 1.637 2010/01/18 17:21:10 dingtao Exp $
*
* \brief DRXJ specific implementation of DRX driver
*
* \author Dragan Savic, Milos Nikolic, Mihajlo Katona, Tao Ding, Paul Janssen
*/

/*-----------------------------------------------------------------------------
INCLUDE FILES
----------------------------------------------------------------------------*/

#include "drxj.h"
#include "drxj_map.h"

#ifdef DRXJ_OPTIONS_H
#include "drxj_options.h"
#endif

/*============================================================================*/
/*=== DEFINES ================================================================*/
/*============================================================================*/

/**
* \brief Maximum u32_t value.
*/
#ifndef MAX_U32
#define MAX_U32  ((u32_t) (0xFFFFFFFFL))
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

//#define DRX_DEBUG
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
#define DRXJ_WAKE_UP_KEY (demod -> myI2CDevAddr -> i2cAddr)
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
#define DRXJ_DAP drxDapDRXJFunct_g

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
#define DRXJ_ATV_CHANGED_COEF          ( 0x00000001UL )
#define DRXJ_ATV_CHANGED_PEAK_FLT      ( 0x00000008UL )
#define DRXJ_ATV_CHANGED_NOISE_FLT     ( 0x00000010UL )
#define DRXJ_ATV_CHANGED_OUTPUT        ( 0x00000020UL )
#define DRXJ_ATV_CHANGED_SIF_ATT       ( 0x00000040UL )

/* UIO define */
#define DRX_UIO_MODE_FIRMWARE_SMA DRX_UIO_MODE_FIRMWARE0
#define DRX_UIO_MODE_FIRMWARE_SAW DRX_UIO_MODE_FIRMWARE1

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
#define DRXJ_UCODE_MAGIC_WORD         ((((u16_t)'H')<<8)+((u16_t)'L'))
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
#ifdef  AUD_DEM_WR_FM_MATRIX__A
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

#ifdef DRXJDRIVER_DEBUG
#include <stdio.h>
#define CHK_ERROR( s ) \
	do{ \
	    if ( (s) != DRX_STS_OK ) \
	    { \
	       fprintf(stderr, \
		       "ERROR[\n file    : %s\n line    : %d\n]\n", \
		       __FILE__,__LINE__); \
	       goto rw_error; }; \
	    } \
	while (0 != 0)
#else
#define CHK_ERROR( s ) \
   do{ \
      if ( (s) != DRX_STS_OK ) { goto rw_error; } \
   } while (0 != 0)
#endif

#define CHK_ZERO( s ) \
   do{ \
      if ( (s) == 0 ) return DRX_STS_ERROR; \
   } while (0)

#define DUMMY_READ() \
   do{ \
      u16_t dummy; \
      RR16( demod->myI2CDevAddr, SCU_RAM_VERSION_HI__A, &dummy ); \
   } while (0)

#define WR16( dev, addr, val) \
   CHK_ERROR( DRXJ_DAP.writeReg16Func( (dev), (addr), (val), 0 ) )

#define RR16( dev, addr, val) \
   CHK_ERROR( DRXJ_DAP.readReg16Func( (dev), (addr), (val), 0 ) )

#define WR32( dev, addr, val) \
   CHK_ERROR( DRXJ_DAP.writeReg32Func( (dev), (addr), (val), 0 ) )

#define RR32( dev, addr, val) \
   CHK_ERROR( DRXJ_DAP.readReg32Func( (dev), (addr), (val), 0 ) )

#define WRB( dev, addr, len, block ) \
   CHK_ERROR( DRXJ_DAP.writeBlockFunc( (dev), (addr), (len), (block), 0 ) )

#define RRB( dev, addr, len, block ) \
   CHK_ERROR( DRXJ_DAP.readBlockFunc( (dev), (addr), (len), (block), 0 ) )

#define BCWR16( dev, addr, val ) \
   CHK_ERROR( DRXJ_DAP.writeReg16Func( (dev), (addr), (val), DRXDAP_FASI_BROADCAST ) )

#define ARR32( dev, addr, val) \
   CHK_ERROR( DRXJ_DAP_AtomicReadReg32( (dev), (addr), (val), 0 ) )

#define SARR16( dev, addr, val) \
   CHK_ERROR( DRXJ_DAP_SCU_AtomicReadReg16( (dev), (addr), (val), 0 ) )

#define SAWR16( dev, addr, val) \
   CHK_ERROR( DRXJ_DAP_SCU_AtomicWriteReg16( (dev), (addr), (val), 0 ) )

/**
* This macro is used to create byte arrays for block writes.
* Block writes speed up I2C traffic between host and demod.
* The macro takes care of the required byte order in a 16 bits word.
* x -> lowbyte(x), highbyte(x)
*/
#define DRXJ_16TO8( x ) ((u8_t) (((u16_t)x)    &0xFF)), \
			((u8_t)((((u16_t)x)>>8)&0xFF))
/**
* This macro is used to convert byte array to 16 bit register value for block read.
* Block read speed up I2C traffic between host and demod.
* The macro takes care of the required byte order in a 16 bits word.
*/
#define DRXJ_8TO16( x ) ((u16_t) (x[0] | (x[1] << 8)))

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

#define DRXJ_ISATVSTD( std ) ( ( std == DRX_STANDARD_PAL_SECAM_BG ) || \
			       ( std == DRX_STANDARD_PAL_SECAM_DK ) || \
			       ( std == DRX_STANDARD_PAL_SECAM_I  ) || \
			       ( std == DRX_STANDARD_PAL_SECAM_L  ) || \
			       ( std == DRX_STANDARD_PAL_SECAM_LP ) || \
			       ( std == DRX_STANDARD_NTSC ) || \
			       ( std == DRX_STANDARD_FM ) )

#define DRXJ_ISQAMSTD( std ) ( ( std == DRX_STANDARD_ITU_A ) || \
			       ( std == DRX_STANDARD_ITU_B ) || \
			       ( std == DRX_STANDARD_ITU_C ) || \
			       ( std == DRX_STANDARD_ITU_D ))

/*-----------------------------------------------------------------------------
STATIC VARIABLES
----------------------------------------------------------------------------*/
DRXStatus_t DRXJ_Open(pDRXDemodInstance_t demod);
DRXStatus_t DRXJ_Close(pDRXDemodInstance_t demod);
DRXStatus_t DRXJ_Ctrl(pDRXDemodInstance_t demod,
		      DRXCtrlIndex_t ctrl, void *ctrlData);

/*-----------------------------------------------------------------------------
GLOBAL VARIABLES
----------------------------------------------------------------------------*/
/*
 * DRXJ DAP structures
 */

static DRXStatus_t DRXJ_DAP_ReadBlock(struct i2c_device_addr *devAddr,
				      DRXaddr_t addr,
				      u16_t datasize,
				      pu8_t data, DRXflags_t flags);

static DRXStatus_t DRXJ_DAP_ReadModifyWriteReg8(struct i2c_device_addr *devAddr,
						DRXaddr_t waddr,
						DRXaddr_t raddr,
						u8_t wdata, pu8_t rdata);

static DRXStatus_t DRXJ_DAP_ReadModifyWriteReg16(struct i2c_device_addr *devAddr,
						 DRXaddr_t waddr,
						 DRXaddr_t raddr,
						 u16_t wdata, pu16_t rdata);

static DRXStatus_t DRXJ_DAP_ReadModifyWriteReg32(struct i2c_device_addr *devAddr,
						 DRXaddr_t waddr,
						 DRXaddr_t raddr,
						 u32_t wdata, pu32_t rdata);

static DRXStatus_t DRXJ_DAP_ReadReg8(struct i2c_device_addr *devAddr,
				     DRXaddr_t addr,
				     pu8_t data, DRXflags_t flags);

static DRXStatus_t DRXJ_DAP_ReadReg16(struct i2c_device_addr *devAddr,
				      DRXaddr_t addr,
				      pu16_t data, DRXflags_t flags);

static DRXStatus_t DRXJ_DAP_ReadReg32(struct i2c_device_addr *devAddr,
				      DRXaddr_t addr,
				      pu32_t data, DRXflags_t flags);

static DRXStatus_t DRXJ_DAP_WriteBlock(struct i2c_device_addr *devAddr,
				       DRXaddr_t addr,
				       u16_t datasize,
				       pu8_t data, DRXflags_t flags);

static DRXStatus_t DRXJ_DAP_WriteReg8(struct i2c_device_addr *devAddr,
				      DRXaddr_t addr,
				      u8_t data, DRXflags_t flags);

static DRXStatus_t DRXJ_DAP_WriteReg16(struct i2c_device_addr *devAddr,
				       DRXaddr_t addr,
				       u16_t data, DRXflags_t flags);

static DRXStatus_t DRXJ_DAP_WriteReg32(struct i2c_device_addr *devAddr,
				       DRXaddr_t addr,
				       u32_t data, DRXflags_t flags);

/* The version structure of this protocol implementation */
char drxDapDRXJModuleName[] = "DRXJ Data Access Protocol";
char drxDapDRXJVersionText[] = "0.0.0";

DRXVersion_t drxDapDRXJVersion = {
	DRX_MODULE_DAP,	      /**< type identifier of the module  */
	drxDapDRXJModuleName, /**< name or description of module  */

	0,		      /**< major version number           */
	0,		      /**< minor version number           */
	0,		      /**< patch version number           */
	drxDapDRXJVersionText /**< version as text string         */
};

/* The structure containing the protocol interface */
DRXAccessFunc_t drxDapDRXJFunct_g = {
	&drxDapDRXJVersion,
	DRXJ_DAP_WriteBlock,	/* Supported       */
	DRXJ_DAP_ReadBlock,	/* Supported       */
	DRXJ_DAP_WriteReg8,	/* Not supported   */
	DRXJ_DAP_ReadReg8,	/* Not supported   */
	DRXJ_DAP_ReadModifyWriteReg8,	/* Not supported   */
	DRXJ_DAP_WriteReg16,	/* Supported       */
	DRXJ_DAP_ReadReg16,	/* Supported       */
	DRXJ_DAP_ReadModifyWriteReg16,	/* Supported       */
	DRXJ_DAP_WriteReg32,	/* Supported       */
	DRXJ_DAP_ReadReg32,	/* Supported       */
	DRXJ_DAP_ReadModifyWriteReg32,	/* Not supported   */
};

/**
* /var DRXJ_Func_g
* /brief The driver functions of the drxj
*/
DRXDemodFunc_t DRXJFunctions_g = {
	DRXJ_TYPE_ID,
	DRXJ_Open,
	DRXJ_Close,
	DRXJ_Ctrl
};

DRXJData_t DRXJData_g = {
	FALSE,			/* hasLNA : TRUE if LNA (aka PGA) present      */
	FALSE,			/* hasOOB : TRUE if OOB supported              */
	FALSE,			/* hasNTSC: TRUE if NTSC supported             */
	FALSE,			/* hasBTSC: TRUE if BTSC supported             */
	FALSE,			/* hasSMATX: TRUE if SMA_TX pin is available   */
	FALSE,			/* hasSMARX: TRUE if SMA_RX pin is available   */
	FALSE,			/* hasGPIO : TRUE if GPIO pin is available     */
	FALSE,			/* hasIRQN : TRUE if IRQN pin is available     */
	0,			/* mfx A1/A2/A... */

	/* tuner settings */
	FALSE,			/* tuner mirrors RF signal    */
	/* standard/channel settings */
	DRX_STANDARD_UNKNOWN,	/* current standard           */
	DRX_CONSTELLATION_AUTO,	/* constellation              */
	0,			/* frequency in KHz           */
	DRX_BANDWIDTH_UNKNOWN,	/* currBandwidth              */
	DRX_MIRROR_NO,		/* mirror                     */

	/* signal quality information: */
	/* default values taken from the QAM Programming guide */
	/*   fecBitsDesired should not be less than 4000000    */
	4000000,		/* fecBitsDesired    */
	5,			/* fecVdPlen         */
	4,			/* qamVdPrescale     */
	0xFFFF,			/* qamVDPeriod       */
	204 * 8,		/* fecRsPlen annex A */
	1,			/* fecRsPrescale     */
	FEC_RS_MEASUREMENT_PERIOD,	/* fecRsPeriod     */
	TRUE,			/* resetPktErrAcc    */
	0,			/* pktErrAccStart    */

	/* HI configuration */
	0,			/* HICfgTimingDiv    */
	0,			/* HICfgBridgeDelay  */
	0,			/* HICfgWakeUpKey    */
	0,			/* HICfgCtrl         */
	0,			/* HICfgTimeout      */
	/* UIO configuartion */
	DRX_UIO_MODE_DISABLE,	/* uioSmaRxMode      */
	DRX_UIO_MODE_DISABLE,	/* uioSmaTxMode      */
	DRX_UIO_MODE_DISABLE,	/* uioASELMode       */
	DRX_UIO_MODE_DISABLE,	/* uioIRQNMode       */
	/* FS setting */
	0UL,			/* iqmFsRateOfs      */
	FALSE,			/* posImage          */
	/* RC setting */
	0UL,			/* iqmRcRateOfs      */
	/* AUD information */
/*   FALSE,                  * flagSetAUDdone    */
/*   FALSE,                  * detectedRDS       */
/*   TRUE,                   * flagASDRequest    */
/*   FALSE,                  * flagHDevClear     */
/*   FALSE,                  * flagHDevSet       */
/*   (u16_t) 0xFFF,          * rdsLastCount      */

/*#ifdef DRXJ_SPLIT_UCODE_UPLOAD
   FALSE,                  * flagAudMcUploaded */
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
	FALSE,			/* flag: TRUE=bypass             */
	ATV_TOP_VID_PEAK__PRE,	/* shadow of ATV_TOP_VID_PEAK__A */
	ATV_TOP_NOISE_TH__PRE,	/* shadow of ATV_TOP_NOISE_TH__A */
	TRUE,			/* flag CVBS ouput enable        */
	FALSE,			/* flag SIF ouput enable         */
	DRXJ_SIF_ATTENUATION_0DB,	/* current SIF att setting       */
	{			/* qamRfAgcCfg */
	 DRX_STANDARD_ITU_B,	/* standard            */
	 DRX_AGC_CTRL_AUTO,	/* ctrlMode            */
	 0,			/* outputLevel         */
	 0,			/* minOutputLevel      */
	 0xFFFF,		/* maxOutputLevel      */
	 0x0000,		/* speed               */
	 0x0000,		/* top                 */
	 0x0000			/* c.o.c.              */
	 },
	{			/* qamIfAgcCfg */
	 DRX_STANDARD_ITU_B,	/* standard            */
	 DRX_AGC_CTRL_AUTO,	/* ctrlMode            */
	 0,			/* outputLevel         */
	 0,			/* minOutputLevel      */
	 0xFFFF,		/* maxOutputLevel      */
	 0x0000,		/* speed               */
	 0x0000,		/* top    (don't care) */
	 0x0000			/* c.o.c. (don't care) */
	 },
	{			/* vsbRfAgcCfg */
	 DRX_STANDARD_8VSB,	/* standard       */
	 DRX_AGC_CTRL_AUTO,	/* ctrlMode       */
	 0,			/* outputLevel    */
	 0,			/* minOutputLevel */
	 0xFFFF,		/* maxOutputLevel */
	 0x0000,		/* speed          */
	 0x0000,		/* top    (don't care) */
	 0x0000			/* c.o.c. (don't care) */
	 },
	{			/* vsbIfAgcCfg */
	 DRX_STANDARD_8VSB,	/* standard       */
	 DRX_AGC_CTRL_AUTO,	/* ctrlMode       */
	 0,			/* outputLevel    */
	 0,			/* minOutputLevel */
	 0xFFFF,		/* maxOutputLevel */
	 0x0000,		/* speed          */
	 0x0000,		/* top    (don't care) */
	 0x0000			/* c.o.c. (don't care) */
	 },
	0,			/* qamPgaCfg */
	0,			/* vsbPgaCfg */
	{			/* qamPreSawCfg */
	 DRX_STANDARD_ITU_B,	/* standard  */
	 0,			/* reference */
	 FALSE			/* usePreSaw */
	 },
	{			/* vsbPreSawCfg */
	 DRX_STANDARD_8VSB,	/* standard  */
	 0,			/* reference */
	 FALSE			/* usePreSaw */
	 },

	/* Version information */
#ifndef _CH_
	{
	 "01234567890",		/* human readable version microcode             */
	 "01234567890"		/* human readable version device specific code  */
	 },
	{
	 {			/* DRXVersion_t for microcode                   */
	  DRX_MODULE_UNKNOWN,
	  (char *)(NULL),
	  0,
	  0,
	  0,
	  (char *)(NULL)
	  },
	 {			/* DRXVersion_t for device specific code */
	  DRX_MODULE_UNKNOWN,
	  (char *)(NULL),
	  0,
	  0,
	  0,
	  (char *)(NULL)
	  }
	 },
	{
	 {			/* DRXVersionList_t for microcode */
	  (pDRXVersion_t) (NULL),
	  (pDRXVersionList_t) (NULL)
	  },
	 {			/* DRXVersionList_t for device specific code */
	  (pDRXVersion_t) (NULL),
	  (pDRXVersionList_t) (NULL)
	  }
	 },
#endif
	FALSE,			/* smartAntInverted */
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
	FALSE,			/* oobPowerOn           */
	0,			/* mpegTsStaticBitrate  */
	FALSE,			/* disableTEIhandling   */
	FALSE,			/* bitReverseMpegOutout */
	DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO,	/* mpegOutputClockRate */
	DRXJ_MPEG_START_WIDTH_1CLKCYC,	/* mpegStartWidth */

	/* Pre SAW & Agc configuration for ATV */
	{
	 DRX_STANDARD_NTSC,	/* standard     */
	 7,			/* reference    */
	 TRUE			/* usePreSaw    */
	 },
	{			/* ATV RF-AGC */
	 DRX_STANDARD_NTSC,	/* standard              */
	 DRX_AGC_CTRL_AUTO,	/* ctrlMode              */
	 0,			/* outputLevel           */
	 0,			/* minOutputLevel (d.c.) */
	 0,			/* maxOutputLevel (d.c.) */
	 3,			/* speed                 */
	 9500,			/* top                   */
	 4000			/* cut-off current       */
	 },
	{			/* ATV IF-AGC */
	 DRX_STANDARD_NTSC,	/* standard              */
	 DRX_AGC_CTRL_AUTO,	/* ctrlMode              */
	 0,			/* outputLevel           */
	 0,			/* minOutputLevel (d.c.) */
	 0,			/* maxOutputLevel (d.c.) */
	 3,			/* speed                 */
	 2400,			/* top                   */
	 0			/* c.o.c.         (d.c.) */
	 },
	140,			/* ATV PGA config */
	0,			/* currSymbolRate */

	FALSE,			/* pdrSafeMode     */
	SIO_PDR_GPIO_CFG__PRE,	/* pdrSafeRestoreValGpio  */
	SIO_PDR_VSYNC_CFG__PRE,	/* pdrSafeRestoreValVSync */
	SIO_PDR_SMA_RX_CFG__PRE,	/* pdrSafeRestoreValSmaRx */
	SIO_PDR_SMA_TX_CFG__PRE,	/* pdrSafeRestoreValSmaTx */

	4,			/* oobPreSaw            */
	DRXJ_OOB_LO_POW_MINUS10DB,	/* oobLoPow             */
	{
	 FALSE			/* audData, only first member */
	 },
};

/**
* \var DRXJDefaultAddr_g
* \brief Default I2C address and device identifier.
*/
struct i2c_device_addr DRXJDefaultAddr_g = {
	DRXJ_DEF_I2C_ADDR,	/* i2c address */
	DRXJ_DEF_DEMOD_DEV_ID	/* device id */
};

/**
* \var DRXJDefaultCommAttr_g
* \brief Default common attributes of a drxj demodulator instance.
*/
DRXCommonAttr_t DRXJDefaultCommAttr_g = {
	(pu8_t) NULL,		/* ucode ptr            */
	0,			/* ucode size           */
	TRUE,			/* ucode verify switch  */
	{0},			/* version record       */

	44000,			/* IF in kHz in case no tuner instance is used  */
	(151875 - 0),		/* system clock frequency in kHz                */
	0,			/* oscillator frequency kHz                     */
	0,			/* oscillator deviation in ppm, signed          */
	FALSE,			/* If TRUE mirror frequency spectrum            */
	{
	 /* MPEG output configuration */
	 TRUE,			/* If TRUE, enable MPEG ouput    */
	 FALSE,			/* If TRUE, insert RS byte       */
	 TRUE,			/* If TRUE, parallel out otherwise serial */
	 FALSE,			/* If TRUE, invert DATA signals  */
	 FALSE,			/* If TRUE, invert ERR signal    */
	 FALSE,			/* If TRUE, invert STR signals   */
	 FALSE,			/* If TRUE, invert VAL signals   */
	 FALSE,			/* If TRUE, invert CLK signals   */
	 TRUE,			/* If TRUE, static MPEG clockrate will
				   be used, otherwise clockrate will
				   adapt to the bitrate of the TS */
	 19392658UL,		/* Maximum bitrate in b/s in case
				   static clockrate is selected */
	 DRX_MPEG_STR_WIDTH_1	/* MPEG Start width in clock cycles */
	 },
	/* Initilisations below can be ommited, they require no user input and
	   are initialy 0, NULL or FALSE. The compiler will initialize them to these
	   values when ommited.  */
	FALSE,			/* isOpened */

	/* SCAN */
	NULL,			/* no scan params yet               */
	0,			/* current scan index               */
	0,			/* next scan frequency              */
	FALSE,			/* scan ready flag                  */
	0,			/* max channels to scan             */
	0,			/* nr of channels scanned           */
	NULL,			/* default scan function            */
	NULL,			/* default context pointer          */
	0,			/* millisec to wait for demod lock  */
	DRXJ_DEMOD_LOCK,	/* desired lock               */
	FALSE,

	/* Power management */
	DRX_POWER_UP,

	/* Tuner */
	1,			/* nr of I2C port to wich tuner is     */
	0L,			/* minimum RF input frequency, in kHz  */
	0L,			/* maximum RF input frequency, in kHz  */
	FALSE,			/* Rf Agc Polarity                     */
	FALSE,			/* If Agc Polarity                     */
	FALSE,			/* tuner slow mode                     */

	{			/* current channel (all 0)             */
	 0UL			/* channel.frequency */
	 },
	DRX_STANDARD_UNKNOWN,	/* current standard */
	DRX_STANDARD_UNKNOWN,	/* previous standard */
	DRX_STANDARD_UNKNOWN,	/* diCacheStandard   */
	FALSE,			/* useBootloader */
	0UL,			/* capabilities */
	0			/* mfx */
};

/**
* \var DRXJDefaultDemod_g
* \brief Default drxj demodulator instance.
*/
DRXDemodInstance_t DRXJDefaultDemod_g = {
	&DRXJFunctions_g,	/* demod functions */
	&DRXJ_DAP,		/* data access protocol functions */
	NULL,			/* tuner instance */
	&DRXJDefaultAddr_g,	/* i2c address & device id */
	&DRXJDefaultCommAttr_g,	/* demod common attributes */
	&DRXJData_g		/* demod device specific attributes */
};

/**
* \brief Default audio data structure for DRK demodulator instance.
*
* This structure is DRXK specific.
*
*/
DRXAudData_t DRXJDefaultAudData_g = {
	FALSE,			/* audioIsActive */
	DRX_AUD_STANDARD_AUTO,	/* audioStandard  */

	/* i2sdata */
	{
	 FALSE,			/* outputEnable   */
	 48000,			/* frequency      */
	 DRX_I2S_MODE_MASTER,	/* mode           */
	 DRX_I2S_WORDLENGTH_32,	/* wordLength     */
	 DRX_I2S_POLARITY_RIGHT,	/* polarity       */
	 DRX_I2S_FORMAT_WS_WITH_DATA	/* format         */
	 },
	/* volume            */
	{
	 TRUE,			/* mute;          */
	 0,			/* volume         */
	 DRX_AUD_AVC_OFF,	/* avcMode        */
	 0,			/* avcRefLevel    */
	 DRX_AUD_AVC_MAX_GAIN_12DB,	/* avcMaxGain     */
	 DRX_AUD_AVC_MAX_ATTEN_24DB,	/* avcMaxAtten    */
	 0,			/* strengthLeft   */
	 0			/* strengthRight  */
	 },
	DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_ON,	/* autoSound */
	/*  assThresholds */
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
	 DRX_AUD_SRC_STEREO_OR_A,	/* sourceI2S */
	 DRX_AUD_I2S_MATRIX_STEREO,	/* matrixI2S */
	 DRX_AUD_FM_MATRIX_SOUND_A	/* matrixFm  */
	 },
	DRX_AUD_DEVIATION_NORMAL,	/* deviation */
	DRX_AUD_AVSYNC_OFF,	/* avSync */

	/* prescale */
	{
	 DRX_AUD_MAX_FM_DEVIATION,	/* fmDeviation */
	 DRX_AUD_MAX_NICAM_PRESCALE	/* nicamGain */
	 },
	DRX_AUD_FM_DEEMPH_75US,	/* deemph */
	DRX_BTSC_STEREO,	/* btscDetect */
	0,			/* rdsDataCounter */
	FALSE			/* rdsDataPresent */
};

/*-----------------------------------------------------------------------------
STRUCTURES
----------------------------------------------------------------------------*/
typedef struct {
	u16_t eqMSE;
	u8_t eqMode;
	u8_t eqCtrl;
	u8_t eqStat;
} DRXJEQStat_t, *pDRXJEQStat_t;

/* HI command */
typedef struct {
	u16_t cmd;
	u16_t param1;
	u16_t param2;
	u16_t param3;
	u16_t param4;
	u16_t param5;
	u16_t param6;
} DRXJHiCmd_t, *pDRXJHiCmd_t;

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
/*============================================================================*/
/*=== MICROCODE RELATED STRUCTURES ===========================================*/
/*============================================================================*/

typedef struct {
	u32_t addr;
	u16_t size;
	u16_t flags;		/* bit[15..2]=reserved,
				   bit[1]= compression on/off
				   bit[0]= CRC on/off */
	u16_t CRC;
} DRXUCodeBlockHdr_t, *pDRXUCodeBlockHdr_t;
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

/*-----------------------------------------------------------------------------
FUNCTIONS
----------------------------------------------------------------------------*/
/* Some prototypes */
static DRXStatus_t
HICommand(const struct i2c_device_addr *devAddr,
	  const pDRXJHiCmd_t cmd, pu16_t result);

static DRXStatus_t
CtrlLockStatus(pDRXDemodInstance_t demod, pDRXLockStatus_t lockStat);

static DRXStatus_t
CtrlPowerMode(pDRXDemodInstance_t demod, pDRXPowerMode_t mode);

static DRXStatus_t PowerDownAud(pDRXDemodInstance_t demod);

#ifndef DRXJ_DIGITAL_ONLY
static DRXStatus_t PowerUpAud(pDRXDemodInstance_t demod, Bool_t setStandard);
#endif

static DRXStatus_t
AUDCtrlSetStandard(pDRXDemodInstance_t demod, pDRXAudStandard_t standard);

static DRXStatus_t
CtrlSetCfgPreSaw(pDRXDemodInstance_t demod, pDRXJCfgPreSaw_t preSaw);

static DRXStatus_t
CtrlSetCfgAfeGain(pDRXDemodInstance_t demod, pDRXJCfgAfeGain_t afeGain);

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
static DRXStatus_t
CtrlUCodeUpload(pDRXDemodInstance_t demod,
		pDRXUCodeInfo_t mcInfo,
		DRXUCodeAction_t action, Bool_t audioMCUpload);
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

/*============================================================================*/
/*============================================================================*/
/*==                          HELPER FUNCTIONS                              ==*/
/*============================================================================*/
/*============================================================================*/

/**
* \fn void Mult32(u32_t a, u32_t b, pu32_t h, pu32_t l)
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

#define DRX_IS_BOOTH_NEGATIVE(__a)  (((__a) & (1 << (sizeof (u32_t) * 8 - 1))) != 0)

static void Mult32(u32_t a, u32_t b, pu32_t h, pu32_t l)
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
								    ((u32_t)
								     (-
								      ((s32_t)
								       b))));
		case 5:
		case 6:
			*l -= b;
			*h = *h - !DRX_IS_BOOTH_NEGATIVE(b) + !b + (*l <
								    ((u32_t)
								     (-
								      ((s32_t)
								       b))));
			break;
		}
	}
}

/*============================================================================*/

/*
* \fn u32_t Frac28(u32_t N, u32_t D)
* \brief Compute: (1<<28)*N/D
* \param N 32 bits
* \param D 32 bits
* \return (1<<28)*N/D
* This function is used to avoid floating-point calculations as they may
* not be present on the target platform.

* Frac28 performs an unsigned 28/28 bits division to 32-bit fixed point
* fraction used for setting the Frequency Shifter registers.
* N and D can hold numbers up to width: 28-bits.
* The 4 bits integer part and the 28 bits fractional part are calculated.

* Usage condition: ((1<<28)*n)/d < ((1<<32)-1) => (n/d) < 15.999

* N: 0...(1<<28)-1 = 268435454
* D: 0...(1<<28)-1
* Q: 0...(1<<32)-1
*/
static u32_t Frac28(u32_t N, u32_t D)
{
	int i = 0;
	u32_t Q1 = 0;
	u32_t R0 = 0;

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
* \fn u32_t Log10Times100( u32_t x)
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

static u32_t Log10Times100(u32_t x)
{
	static const u8_t scale = 15;
	static const u8_t indexWidth = 5;
	/*
	   log2lut[n] = (1<<scale) * 200 * log2( 1.0 + ( (1.0/(1<<INDEXWIDTH)) * n ))
	   0 <= n < ((1<<INDEXWIDTH)+1)
	 */

	static const u32_t log2lut[] = {
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

	u8_t i = 0;
	u32_t y = 0;
	u32_t d = 0;
	u32_t k = 0;
	u32_t r = 0;

	if (x == 0)
		return (0);

	/* Scale x (normalize) */
	/* computing y in log(x/y) = log(x) - log(y) */
	if ((x & (((u32_t) (-1)) << (scale + 1))) == 0) {
		for (k = scale; k > 0; k--) {
			if (x & (((u32_t) 1) << scale))
				break;
			x <<= 1;
		}
	} else {
		for (k = scale; k < 31; k++) {
			if ((x & (((u32_t) (-1)) << (scale + 1))) == 0)
				break;
			x >>= 1;
		}
	}
	/*
	   Now x has binary point between bit[scale] and bit[scale-1]
	   and 1.0 <= x < 2.0 */

	/* correction for divison: log(x) = log(x/y)+log(y) */
	y = k * ((((u32_t) 1) << scale) * 200);

	/* remove integer part */
	x &= ((((u32_t) 1) << scale) - 1);
	/* get index */
	i = (u8_t) (x >> (scale - indexWidth));
	/* compute delta (x-a) */
	d = x & ((((u32_t) 1) << (scale - indexWidth)) - 1);
	/* compute log, multiplication ( d* (.. )) must be within range ! */
	y += log2lut[i] +
	    ((d * (log2lut[i + 1] - log2lut[i])) >> (scale - indexWidth));
	/* Conver to log10() */
	y /= 108853;		/* (log2(10) << scale) */
	r = (y >> 1);
	/* rounding */
	if (y & ((u32_t) 1))
		r++;

	return (r);

}

/**
* \fn u32_t FracTimes1e6( u16_t N, u32_t D)
* \brief Compute: (N/D) * 1000000.
* \param N nominator 16-bits.
* \param D denominator 32-bits.
* \return u32_t
* \retval ((N/D) * 1000000), 32 bits
*
* No check on D=0!
*/
static u32_t FracTimes1e6(u32_t N, u32_t D)
{
	u32_t remainder = 0;
	u32_t frac = 0;

	/*
	   frac = (N * 1000000) / D
	   To let it fit in a 32 bits computation:
	   frac = (N * (1000000 >> 4)) / (D >> 4)
	   This would result in a problem in case D < 16 (div by 0).
	   So we do it more elaborate as shown below.
	 */
	frac = (((u32_t) N) * (1000000 >> 4)) / D;
	frac <<= 4;
	remainder = (((u32_t) N) * (1000000 >> 4)) % D;
	remainder <<= 4;
	frac += remainder / D;
	remainder = remainder % D;
	if ((remainder * 2) > D) {
		frac++;
	}

	return (frac);
}

/*============================================================================*/

/**
* \brief Compute: 100 * 10^( GdB / 200 ).
* \param  u32_t   GdB      Gain in 0.1dB
* \return u32_t            Gainfactor in 0.01 resolution
*
*/
static u32_t dB2LinTimes100(u32_t GdB)
{
	u32_t result = 0;
	u32_t nr6dBSteps = 0;
	u32_t remainder = 0;
	u32_t remainderFac = 0;

	/* start with factors 2 (6.02dB) */
	nr6dBSteps = GdB * 1000UL / 60206UL;
	if (nr6dBSteps > 17) {
		/* Result max overflow if > log2( maxu32 / 2e4 ) ~= 17.7 */
		return MAX_U32;
	}
	result = (1 << nr6dBSteps);

	/* calculate remaining factor,
	   poly approximation of 10^(GdB/200):

	   y = 1E-04x2 + 0.0106x + 1.0026

	   max deviation < 0.005 for range x = [0 ... 60]
	 */
	remainder = ((GdB * 1000UL) % 60206UL) / 1000UL;
	/* using 1e-4 for poly calculation  */
	remainderFac = 1 * remainder * remainder;
	remainderFac += 106 * remainder;
	remainderFac += 10026;

	/* multiply by remaining factor */
	result *= remainderFac;

	/* conversion from 1e-4 to 1e-2 */
	return ((result + 50) / 100);
}

#ifndef DRXJ_DIGITAL_ONLY
#define FRAC_FLOOR    0
#define FRAC_CEIL     1
#define FRAC_ROUND    2
/**
* \fn u32_t Frac( u32_t N, u32_t D, u16_t RC )
* \brief Compute: N/D.
* \param N nominator 32-bits.
* \param D denominator 32-bits.
* \param RC-result correction: 0-floor; 1-ceil; 2-round
* \return u32_t
* \retval N/D, 32 bits
*
* If D=0 returns 0
*/
static u32_t Frac(u32_t N, u32_t D, u16_t RC)
{
	u32_t remainder = 0;
	u32_t frac = 0;
	u16_t bitCnt = 32;

	if (D == 0) {
		frac = 0;
		remainder = 0;

		return (frac);
	}

	if (D > N) {
		frac = 0;
		remainder = N;
	} else {
		remainder = 0;
		frac = N;
		while (bitCnt-- > 0) {
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

	return (frac);
}
#endif

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
/*============================================================================*/

/**
* \fn u16_t UCodeRead16( pu8_t addr)
* \brief Read a 16 bits word, expect big endian data.
* \return u16_t The data read.
*/
static u16_t UCodeRead16(pu8_t addr)
{
	/* Works fo any host processor */

	u16_t word = 0;

	word = ((u16_t) addr[0]);
	word <<= 8;
	word |= ((u16_t) addr[1]);

	return (word);
}

/*============================================================================*/

/**
* \fn u32_t UCodeRead32( pu8_t addr)
* \brief Read a 32 bits word, expect big endian data.
* \return u32_t The data read.
*/
static u32_t UCodeRead32(pu8_t addr)
{
	/* Works fo any host processor */

	u32_t word = 0;

	word = ((u16_t) addr[0]);
	word <<= 8;
	word |= ((u16_t) addr[1]);
	word <<= 8;
	word |= ((u16_t) addr[2]);
	word <<= 8;
	word |= ((u16_t) addr[3]);

	return (word);
}

/*============================================================================*/

/**
* \fn u16_t UCodeComputeCRC (pu8_t blockData, u16_t nrWords)
* \brief Compute CRC of block of microcode data.
* \param blockData Pointer to microcode data.
* \param nrWords Size of microcode block (number of 16 bits words).
* \return u16_t The computed CRC residu.
*/
static u16_t UCodeComputeCRC(pu8_t blockData, u16_t nrWords)
{
	u16_t i = 0;
	u16_t j = 0;
	u32_t CRCWord = 0;
	u32_t carry = 0;

	while (i < nrWords) {
		CRCWord |= (u32_t) UCodeRead16(blockData);
		for (j = 0; j < 16; j++) {
			CRCWord <<= 1;
			if (carry != 0)
				CRCWord ^= 0x80050000UL;
			carry = CRCWord & 0x80000000UL;
		}
		i++;
		blockData += (sizeof(u16_t));
	}
	return ((u16_t) (CRCWord >> 16));
}
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

/**
* \brief Values for NICAM prescaler gain. Computed from dB to integer
*        and rounded. For calc used formula: 16*10^(prescaleGain[dB]/20).
*
*/
static const u16_t NicamPrescTableVal[43] =
    { 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4,
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

#define DRXJ_ISAUDWRITE( addr ) (((((addr)>>16)&1)==1)?TRUE:FALSE)
#define DRXJ_DAP_AUDTRIF_TIMEOUT 80	/* millisec */
/*============================================================================*/

/**
* \fn Bool_t IsHandledByAudTrIf( DRXaddr_t addr )
* \brief Check if this address is handled by the audio token ring interface.
* \param addr
* \return Bool_t
* \retval TRUE  Yes, handled by audio token ring interface
* \retval FALSE No, not handled by audio token ring interface
*
*/
static
Bool_t IsHandledByAudTrIf(DRXaddr_t addr)
{
	Bool_t retval = FALSE;

	if ((DRXDAP_FASI_ADDR2BLOCK(addr) == 4) &&
	    (DRXDAP_FASI_ADDR2BANK(addr) > 1) &&
	    (DRXDAP_FASI_ADDR2BANK(addr) < 6)) {
		retval = TRUE;
	}

	return (retval);
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_ReadBlock(struct i2c_device_addr *devAddr,
				      DRXaddr_t addr,
				      u16_t datasize,
				      pu8_t data, DRXflags_t flags)
{
	return drxDapFASIFunct_g.readBlockFunc(devAddr,
					       addr, datasize, data, flags);
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_ReadModifyWriteReg8(struct i2c_device_addr *devAddr,
						DRXaddr_t waddr,
						DRXaddr_t raddr,
						u8_t wdata, pu8_t rdata)
{
	return drxDapFASIFunct_g.readModifyWriteReg8Func(devAddr,
							 waddr,
							 raddr, wdata, rdata);
}

/*============================================================================*/

/**
* \fn DRXStatus_t DRXJ_DAP_RMWriteReg16Short
* \brief Read modify write 16 bits audio register using short format only.
* \param devAddr
* \param waddr    Address to write to
* \param raddr    Address to read from (usually SIO_HI_RA_RAM_S0_RMWBUF__A)
* \param wdata    Data to write
* \param rdata    Buffer for data to read
* \return DRXStatus_t
* \retval DRX_STS_OK Succes
* \retval DRX_STS_ERROR Timeout, I2C error, illegal bank
*
* 16 bits register read modify write access using short addressing format only.
* Requires knowledge of the registermap, thus device dependent.
* Using DAP FASI directly to avoid endless recursion of RMWs to audio registers.
*
*/

/* TODO correct define should be #if ( DRXDAPFASI_SHORT_ADDR_ALLOWED==1 )
   See comments DRXJ_DAP_ReadModifyWriteReg16 */
#if ( DRXDAPFASI_LONG_ADDR_ALLOWED == 0 )
static DRXStatus_t DRXJ_DAP_RMWriteReg16Short(struct i2c_device_addr *devAddr,
					      DRXaddr_t waddr,
					      DRXaddr_t raddr,
					      u16_t wdata, pu16_t rdata)
{
	DRXStatus_t rc;

	if (rdata == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	/* Set RMW flag */
	rc = drxDapFASIFunct_g.writeReg16Func(devAddr,
					      SIO_HI_RA_RAM_S0_FLG_ACC__A,
					      SIO_HI_RA_RAM_S0_FLG_ACC_S0_RWM__M,
					      0x0000);
	if (rc == DRX_STS_OK) {
		/* Write new data: triggers RMW */
		rc = drxDapFASIFunct_g.writeReg16Func(devAddr, waddr, wdata,
						      0x0000);
	}
	if (rc == DRX_STS_OK) {
		/* Read old data */
		rc = drxDapFASIFunct_g.readReg16Func(devAddr, raddr, rdata,
						     0x0000);
	}
	if (rc == DRX_STS_OK) {
		/* Reset RMW flag */
		rc = drxDapFASIFunct_g.writeReg16Func(devAddr,
						      SIO_HI_RA_RAM_S0_FLG_ACC__A,
						      0, 0x0000);
	}

	return rc;
}
#endif

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_ReadModifyWriteReg16(struct i2c_device_addr *devAddr,
						 DRXaddr_t waddr,
						 DRXaddr_t raddr,
						 u16_t wdata, pu16_t rdata)
{
	/* TODO: correct short/long addressing format decision,
	   now long format has higher prio then short because short also
	   needs virt bnks (not impl yet) for certain audio registers */
#if ( DRXDAPFASI_LONG_ADDR_ALLOWED==1 )
	return drxDapFASIFunct_g.readModifyWriteReg16Func(devAddr,
							  waddr,
							  raddr, wdata, rdata);
#else
	return DRXJ_DAP_RMWriteReg16Short(devAddr, waddr, raddr, wdata, rdata);
#endif
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_ReadModifyWriteReg32(struct i2c_device_addr *devAddr,
						 DRXaddr_t waddr,
						 DRXaddr_t raddr,
						 u32_t wdata, pu32_t rdata)
{
	return drxDapFASIFunct_g.readModifyWriteReg32Func(devAddr,
							  waddr,
							  raddr, wdata, rdata);
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_ReadReg8(struct i2c_device_addr *devAddr,
				     DRXaddr_t addr,
				     pu8_t data, DRXflags_t flags)
{
	return drxDapFASIFunct_g.readReg8Func(devAddr, addr, data, flags);
}

/*============================================================================*/

/**
* \fn DRXStatus_t DRXJ_DAP_ReadAudReg16
* \brief Read 16 bits audio register
* \param devAddr
* \param addr
* \param data
* \return DRXStatus_t
* \retval DRX_STS_OK Succes
* \retval DRX_STS_ERROR Timeout, I2C error, illegal bank
*
* 16 bits register read access via audio token ring interface.
*
*/
static DRXStatus_t DRXJ_DAP_ReadAudReg16(struct i2c_device_addr *devAddr,
					 DRXaddr_t addr, pu16_t data)
{
	u32_t startTimer = 0;
	u32_t currentTimer = 0;
	u32_t deltaTimer = 0;
	u16_t trStatus = 0;
	DRXStatus_t stat = DRX_STS_ERROR;

	/* No read possible for bank 3, return with error */
	if (DRXDAP_FASI_ADDR2BANK(addr) == 3) {
		stat = DRX_STS_INVALID_ARG;
	} else {
		const DRXaddr_t writeBit = ((DRXaddr_t) 1) << 16;

		/* Force reset write bit */
		addr &= (~writeBit);

		/* Set up read */
		startTimer = DRXBSP_HST_Clock();
		do {
			/* RMW to aud TR IF until request is granted or timeout */
			stat = DRXJ_DAP_ReadModifyWriteReg16(devAddr,
							     addr,
							     SIO_HI_RA_RAM_S0_RMWBUF__A,
							     0x0000, &trStatus);

			if (stat != DRX_STS_OK) {
				break;
			};

			currentTimer = DRXBSP_HST_Clock();
			deltaTimer = currentTimer - startTimer;
			if (deltaTimer > DRXJ_DAP_AUDTRIF_TIMEOUT) {
				stat = DRX_STS_ERROR;
				break;
			};

		} while (((trStatus & AUD_TOP_TR_CTR_FIFO_LOCK__M) ==
			  AUD_TOP_TR_CTR_FIFO_LOCK_LOCKED) ||
			 ((trStatus & AUD_TOP_TR_CTR_FIFO_FULL__M) ==
			  AUD_TOP_TR_CTR_FIFO_FULL_FULL));
	}			/* if ( DRXDAP_FASI_ADDR2BANK(addr)!=3 ) */

	/* Wait for read ready status or timeout */
	if (stat == DRX_STS_OK) {
		startTimer = DRXBSP_HST_Clock();

		while ((trStatus & AUD_TOP_TR_CTR_FIFO_RD_RDY__M) !=
		       AUD_TOP_TR_CTR_FIFO_RD_RDY_READY) {
			stat = DRXJ_DAP_ReadReg16(devAddr,
						  AUD_TOP_TR_CTR__A,
						  &trStatus, 0x0000);
			if (stat != DRX_STS_OK) {
				break;
			};

			currentTimer = DRXBSP_HST_Clock();
			deltaTimer = currentTimer - startTimer;
			if (deltaTimer > DRXJ_DAP_AUDTRIF_TIMEOUT) {
				stat = DRX_STS_ERROR;
				break;
			};
		}		/* while ( ... ) */
	}

	/* if { stat == DRX_STS_OK ) */
	/* Read value */
	if (stat == DRX_STS_OK) {
		stat = DRXJ_DAP_ReadModifyWriteReg16(devAddr,
						     AUD_TOP_TR_RD_REG__A,
						     SIO_HI_RA_RAM_S0_RMWBUF__A,
						     0x0000, data);
	}
	/* if { stat == DRX_STS_OK ) */
	return stat;
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_ReadReg16(struct i2c_device_addr *devAddr,
				      DRXaddr_t addr,
				      pu16_t data, DRXflags_t flags)
{
	DRXStatus_t stat = DRX_STS_ERROR;

	/* Check param */
	if ((devAddr == NULL) || (data == NULL)) {
		return DRX_STS_INVALID_ARG;
	}

	if (IsHandledByAudTrIf(addr)) {
		stat = DRXJ_DAP_ReadAudReg16(devAddr, addr, data);
	} else {
		stat = drxDapFASIFunct_g.readReg16Func(devAddr,
						       addr, data, flags);
	}

	return stat;
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_ReadReg32(struct i2c_device_addr *devAddr,
				      DRXaddr_t addr,
				      pu32_t data, DRXflags_t flags)
{
	return drxDapFASIFunct_g.readReg32Func(devAddr, addr, data, flags);
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_WriteBlock(struct i2c_device_addr *devAddr,
				       DRXaddr_t addr,
				       u16_t datasize,
				       pu8_t data, DRXflags_t flags)
{
	return drxDapFASIFunct_g.writeBlockFunc(devAddr,
						addr, datasize, data, flags);
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_WriteReg8(struct i2c_device_addr *devAddr,
				      DRXaddr_t addr,
				      u8_t data, DRXflags_t flags)
{
	return drxDapFASIFunct_g.writeReg8Func(devAddr, addr, data, flags);
}

/*============================================================================*/

/**
* \fn DRXStatus_t DRXJ_DAP_WriteAudReg16
* \brief Write 16 bits audio register
* \param devAddr
* \param addr
* \param data
* \return DRXStatus_t
* \retval DRX_STS_OK Succes
* \retval DRX_STS_ERROR Timeout, I2C error, illegal bank
*
* 16 bits register write access via audio token ring interface.
*
*/
static DRXStatus_t DRXJ_DAP_WriteAudReg16(struct i2c_device_addr *devAddr,
					  DRXaddr_t addr, u16_t data)
{
	DRXStatus_t stat = DRX_STS_ERROR;

	/* No write possible for bank 2, return with error */
	if (DRXDAP_FASI_ADDR2BANK(addr) == 2) {
		stat = DRX_STS_INVALID_ARG;
	} else {
		u32_t startTimer = 0;
		u32_t currentTimer = 0;
		u32_t deltaTimer = 0;
		u16_t trStatus = 0;
		const DRXaddr_t writeBit = ((DRXaddr_t) 1) << 16;

		/* Force write bit */
		addr |= writeBit;
		startTimer = DRXBSP_HST_Clock();
		do {
			/* RMW to aud TR IF until request is granted or timeout */
			stat = DRXJ_DAP_ReadModifyWriteReg16(devAddr,
							     addr,
							     SIO_HI_RA_RAM_S0_RMWBUF__A,
							     data, &trStatus);
			if (stat != DRX_STS_OK) {
				break;
			};

			currentTimer = DRXBSP_HST_Clock();
			deltaTimer = currentTimer - startTimer;
			if (deltaTimer > DRXJ_DAP_AUDTRIF_TIMEOUT) {
				stat = DRX_STS_ERROR;
				break;
			};

		} while (((trStatus & AUD_TOP_TR_CTR_FIFO_LOCK__M) ==
			  AUD_TOP_TR_CTR_FIFO_LOCK_LOCKED) ||
			 ((trStatus & AUD_TOP_TR_CTR_FIFO_FULL__M) ==
			  AUD_TOP_TR_CTR_FIFO_FULL_FULL));

	}			/* if ( DRXDAP_FASI_ADDR2BANK(addr)!=2 ) */

	return stat;
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_WriteReg16(struct i2c_device_addr *devAddr,
				       DRXaddr_t addr,
				       u16_t data, DRXflags_t flags)
{
	DRXStatus_t stat = DRX_STS_ERROR;

	/* Check param */
	if (devAddr == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	if (IsHandledByAudTrIf(addr)) {
		stat = DRXJ_DAP_WriteAudReg16(devAddr, addr, data);
	} else {
		stat = drxDapFASIFunct_g.writeReg16Func(devAddr,
							addr, data, flags);
	}

	return stat;
}

/*============================================================================*/

static DRXStatus_t DRXJ_DAP_WriteReg32(struct i2c_device_addr *devAddr,
				       DRXaddr_t addr,
				       u32_t data, DRXflags_t flags)
{
	return drxDapFASIFunct_g.writeReg32Func(devAddr, addr, data, flags);
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
* \fn DRXStatus_t DRXJ_DAP_AtomicReadWriteBlock()
* \brief Basic access routine for atomic read or write access
* \param devAddr  pointer to i2c dev address
* \param addr     destination/source address
* \param datasize size of data buffer in bytes
* \param data     pointer to data buffer
* \return DRXStatus_t
* \retval DRX_STS_OK Succes
* \retval DRX_STS_ERROR Timeout, I2C error, illegal bank
*
*/
static
DRXStatus_t DRXJ_DAP_AtomicReadWriteBlock(struct i2c_device_addr *devAddr,
					  DRXaddr_t addr,
					  u16_t datasize,
					  pu8_t data, Bool_t readFlag)
{
	DRXJHiCmd_t hiCmd;

	u16_t word;
	u16_t dummy = 0;
	u16_t i = 0;

	/* Parameter check */
	if ((data == NULL) ||
	    (devAddr == NULL) || ((datasize % 2) != 0) || ((datasize / 2) > 8)
	    ) {
		return (DRX_STS_INVALID_ARG);
	}

	/* Set up HI parameters to read or write n bytes */
	hiCmd.cmd = SIO_HI_RA_RAM_CMD_ATOMIC_COPY;
	hiCmd.param1 =
	    (u16_t) ((DRXDAP_FASI_ADDR2BLOCK(DRXJ_HI_ATOMIC_BUF_START) << 6) +
		     DRXDAP_FASI_ADDR2BANK(DRXJ_HI_ATOMIC_BUF_START));
	hiCmd.param2 =
	    (u16_t) DRXDAP_FASI_ADDR2OFFSET(DRXJ_HI_ATOMIC_BUF_START);
	hiCmd.param3 = (u16_t) ((datasize / 2) - 1);
	if (readFlag == FALSE) {
		hiCmd.param3 |= DRXJ_HI_ATOMIC_WRITE;
	} else {
		hiCmd.param3 |= DRXJ_HI_ATOMIC_READ;
	}
	hiCmd.param4 = (u16_t) ((DRXDAP_FASI_ADDR2BLOCK(addr) << 6) +
				DRXDAP_FASI_ADDR2BANK(addr));
	hiCmd.param5 = (u16_t) DRXDAP_FASI_ADDR2OFFSET(addr);

	if (readFlag == FALSE) {
		/* write data to buffer */
		for (i = 0; i < (datasize / 2); i++) {

			word = ((u16_t) data[2 * i]);
			word += (((u16_t) data[(2 * i) + 1]) << 8);
			DRXJ_DAP_WriteReg16(devAddr,
					    (DRXJ_HI_ATOMIC_BUF_START + i),
					    word, 0);
		}
	}

	CHK_ERROR(HICommand(devAddr, &hiCmd, &dummy));

	if (readFlag == TRUE) {
		/* read data from buffer */
		for (i = 0; i < (datasize / 2); i++) {
			DRXJ_DAP_ReadReg16(devAddr,
					   (DRXJ_HI_ATOMIC_BUF_START + i),
					   &word, 0);
			data[2 * i] = (u8_t) (word & 0xFF);
			data[(2 * i) + 1] = (u8_t) (word >> 8);
		}
	}

	return DRX_STS_OK;

rw_error:
	return (DRX_STS_ERROR);

}

/*============================================================================*/

/**
* \fn DRXStatus_t DRXJ_DAP_AtomicReadReg32()
* \brief Atomic read of 32 bits words
*/
static
DRXStatus_t DRXJ_DAP_AtomicReadReg32(struct i2c_device_addr *devAddr,
				     DRXaddr_t addr,
				     pu32_t data, DRXflags_t flags)
{
	u8_t buf[sizeof(*data)];
	DRXStatus_t rc = DRX_STS_ERROR;
	u32_t word = 0;

	if (!data) {
		return DRX_STS_INVALID_ARG;
	}

	rc = DRXJ_DAP_AtomicReadWriteBlock(devAddr, addr,
					   sizeof(*data), buf, TRUE);

	word = (u32_t) buf[3];
	word <<= 8;
	word |= (u32_t) buf[2];
	word <<= 8;
	word |= (u32_t) buf[1];
	word <<= 8;
	word |= (u32_t) buf[0];

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
* \fn DRXStatus_t HICfgCommand()
* \brief Configure HI with settings stored in the demod structure.
* \param demod Demodulator.
* \return DRXStatus_t.
*
* This routine was created because to much orthogonal settings have
* been put into one HI API function (configure). Especially the I2C bridge
* enable/disable should not need re-configuration of the HI.
*
*/
static DRXStatus_t HICfgCommand(const pDRXDemodInstance_t demod)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	DRXJHiCmd_t hiCmd;
	u16_t result = 0;

	extAttr = (pDRXJData_t) demod->myExtAttr;

	hiCmd.cmd = SIO_HI_RA_RAM_CMD_CONFIG;
	hiCmd.param1 = SIO_HI_RA_RAM_PAR_1_PAR1_SEC_KEY;
	hiCmd.param2 = extAttr->HICfgTimingDiv;
	hiCmd.param3 = extAttr->HICfgBridgeDelay;
	hiCmd.param4 = extAttr->HICfgWakeUpKey;
	hiCmd.param5 = extAttr->HICfgCtrl;
	hiCmd.param6 = extAttr->HICfgTransmit;

	CHK_ERROR(HICommand(demod->myI2CDevAddr, &hiCmd, &result));

	/* Reset power down flag (set one call only) */
	extAttr->HICfgCtrl &= (~(SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ));

	return (DRX_STS_OK);

rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t HICommand()
* \brief Configure HI with settings stored in the demod structure.
* \param devAddr I2C address.
* \param cmd HI command.
* \param result HI command result.
* \return DRXStatus_t.
*
* Sends command to HI
*
*/
static DRXStatus_t
HICommand(const struct i2c_device_addr *devAddr, const pDRXJHiCmd_t cmd, pu16_t result)
{
	u16_t waitCmd = 0;
	u16_t nrRetries = 0;
	Bool_t powerdown_cmd = FALSE;

	/* Write parameters */
	switch (cmd->cmd) {

	case SIO_HI_RA_RAM_CMD_CONFIG:
	case SIO_HI_RA_RAM_CMD_ATOMIC_COPY:
		WR16(devAddr, SIO_HI_RA_RAM_PAR_6__A, cmd->param6);
		WR16(devAddr, SIO_HI_RA_RAM_PAR_5__A, cmd->param5);
		WR16(devAddr, SIO_HI_RA_RAM_PAR_4__A, cmd->param4);
		WR16(devAddr, SIO_HI_RA_RAM_PAR_3__A, cmd->param3);
		/* fallthrough */
	case SIO_HI_RA_RAM_CMD_BRDCTRL:
		WR16(devAddr, SIO_HI_RA_RAM_PAR_2__A, cmd->param2);
		WR16(devAddr, SIO_HI_RA_RAM_PAR_1__A, cmd->param1);
		/* fallthrough */
	case SIO_HI_RA_RAM_CMD_NULL:
		/* No parameters */
		break;

	default:
		return (DRX_STS_INVALID_ARG);
		break;
	}

	/* Write command */
	WR16(devAddr, SIO_HI_RA_RAM_CMD__A, cmd->cmd);

	if ((cmd->cmd) == SIO_HI_RA_RAM_CMD_RESET) {
		/* Allow for HI to reset */
		DRXBSP_HST_Sleep(1);
	}

	/* Detect power down to ommit reading result */
	powerdown_cmd = (Bool_t) ((cmd->cmd == SIO_HI_RA_RAM_CMD_CONFIG) &&
				  (((cmd->
				     param5) & SIO_HI_RA_RAM_PAR_5_CFG_SLEEP__M)
				   == SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ));
	if (powerdown_cmd == FALSE) {
		/* Wait until command rdy */
		do {
			nrRetries++;
			if (nrRetries > DRXJ_MAX_RETRIES) {
				goto rw_error;
			};

			RR16(devAddr, SIO_HI_RA_RAM_CMD__A, &waitCmd);
		} while (waitCmd != 0);

		/* Read result */
		RR16(devAddr, SIO_HI_RA_RAM_RES__A, result);

	}
	/* if ( powerdown_cmd == TRUE ) */
	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t InitHI( const pDRXDemodInstance_t demod )
* \brief Initialise and configurate HI.
* \param demod pointer to demod data.
* \return DRXStatus_t Return status.
* \retval DRX_STS_OK Success.
* \retval DRX_STS_ERROR Failure.
*
* Needs to know Psys (System Clock period) and Posc (Osc Clock period)
* Need to store configuration in driver because of the way I2C
* bridging is controlled.
*
*/
static DRXStatus_t InitHI(const pDRXDemodInstance_t demod)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) (NULL);
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);

	extAttr = (pDRXJData_t) demod->myExtAttr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	devAddr = demod->myI2CDevAddr;

	/* PATCH for bug 5003, HI ucode v3.1.0 */
	WR16(devAddr, 0x4301D7, 0x801);

	/* Timing div, 250ns/Psys */
	/* Timing div, = ( delay (nano seconds) * sysclk (kHz) )/ 1000 */
	extAttr->HICfgTimingDiv =
	    (u16_t) ((commonAttr->sysClockFreq / 1000) * HI_I2C_DELAY) / 1000;
	/* Clipping */
	if ((extAttr->HICfgTimingDiv) > SIO_HI_RA_RAM_PAR_2_CFG_DIV__M) {
		extAttr->HICfgTimingDiv = SIO_HI_RA_RAM_PAR_2_CFG_DIV__M;
	}
	/* Bridge delay, uses oscilator clock */
	/* Delay = ( delay (nano seconds) * oscclk (kHz) )/ 1000 */
	/* SDA brdige delay */
	extAttr->HICfgBridgeDelay =
	    (u16_t) ((commonAttr->oscClockFreq / 1000) * HI_I2C_BRIDGE_DELAY) /
	    1000;
	/* Clipping */
	if ((extAttr->HICfgBridgeDelay) > SIO_HI_RA_RAM_PAR_3_CFG_DBL_SDA__M) {
		extAttr->HICfgBridgeDelay = SIO_HI_RA_RAM_PAR_3_CFG_DBL_SDA__M;
	}
	/* SCL bridge delay, same as SDA for now */
	extAttr->HICfgBridgeDelay += ((extAttr->HICfgBridgeDelay) <<
				      SIO_HI_RA_RAM_PAR_3_CFG_DBL_SCL__B);
	/* Wakeup key, setting the read flag (as suggest in the documentation) does
	   not always result into a working solution (barebones worked VI2C failed).
	   Not setting the bit works in all cases . */
	extAttr->HICfgWakeUpKey = DRXJ_WAKE_UP_KEY;
	/* port/bridge/power down ctrl */
	extAttr->HICfgCtrl = (SIO_HI_RA_RAM_PAR_5_CFG_SLV0_SLAVE);
	/* transit mode time out delay and watch dog divider */
	extAttr->HICfgTransmit = SIO_HI_RA_RAM_PAR_6__PRE;

	CHK_ERROR(HICfgCommand(demod));

	return (DRX_STS_OK);

rw_error:
	return (DRX_STS_ERROR);
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
* \fn DRXStatus_t GetDeviceCapabilities()
* \brief Get and store device capabilities.
* \param demod  Pointer to demodulator instance.
* \return DRXStatus_t.
* \return DRX_STS_OK    Success
* \retval DRX_STS_ERROR Failure
*
* Depending on pulldowns on MDx pins the following internals are set:
*  * commonAttr->oscClockFreq
*  * extAttr->hasLNA
*  * extAttr->hasNTSC
*  * extAttr->hasBTSC
*  * extAttr->hasOOB
*
*/
static DRXStatus_t GetDeviceCapabilities(pDRXDemodInstance_t demod)
{
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) (NULL);
	pDRXJData_t extAttr = (pDRXJData_t) NULL;
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);
	u16_t sioPdrOhwCfg = 0;
	u32_t sioTopJtagidLo = 0;
	u16_t bid = 0;

	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = demod->myI2CDevAddr;

	WR16(devAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
	RR16(devAddr, SIO_PDR_OHW_CFG__A, &sioPdrOhwCfg);
	WR16(devAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY__PRE);

	switch ((sioPdrOhwCfg & SIO_PDR_OHW_CFG_FREF_SEL__M)) {
	case 0:
		/* ignore (bypass ?) */
		break;
	case 1:
		/* 27 MHz */
		commonAttr->oscClockFreq = 27000;
		break;
	case 2:
		/* 20.25 MHz */
		commonAttr->oscClockFreq = 20250;
		break;
	case 3:
		/* 4 MHz */
		commonAttr->oscClockFreq = 4000;
		break;
	default:
		return (DRX_STS_ERROR);
	}

	/*
	   Determine device capabilities
	   Based on pinning v47
	 */
	RR32(devAddr, SIO_TOP_JTAGID_LO__A, &sioTopJtagidLo);
	extAttr->mfx = (u8_t) ((sioTopJtagidLo >> 29) & 0xF);

	switch ((sioTopJtagidLo >> 12) & 0xFF) {
	case 0x31:
		WR16(devAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
		RR16(devAddr, SIO_PDR_UIO_IN_HI__A, &bid);
		bid = (bid >> 10) & 0xf;
		WR16(devAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY__PRE);

		extAttr->hasLNA = TRUE;
		extAttr->hasNTSC = FALSE;
		extAttr->hasBTSC = FALSE;
		extAttr->hasOOB = FALSE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = FALSE;
		extAttr->hasGPIO = FALSE;
		extAttr->hasIRQN = FALSE;
		break;
	case 0x33:
		extAttr->hasLNA = FALSE;
		extAttr->hasNTSC = FALSE;
		extAttr->hasBTSC = FALSE;
		extAttr->hasOOB = FALSE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = FALSE;
		extAttr->hasGPIO = FALSE;
		extAttr->hasIRQN = FALSE;
		break;
	case 0x45:
		extAttr->hasLNA = TRUE;
		extAttr->hasNTSC = TRUE;
		extAttr->hasBTSC = FALSE;
		extAttr->hasOOB = FALSE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = TRUE;
		extAttr->hasGPIO = TRUE;
		extAttr->hasIRQN = FALSE;
		break;
	case 0x46:
		extAttr->hasLNA = FALSE;
		extAttr->hasNTSC = TRUE;
		extAttr->hasBTSC = FALSE;
		extAttr->hasOOB = FALSE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = TRUE;
		extAttr->hasGPIO = TRUE;
		extAttr->hasIRQN = FALSE;
		break;
	case 0x41:
		extAttr->hasLNA = TRUE;
		extAttr->hasNTSC = TRUE;
		extAttr->hasBTSC = TRUE;
		extAttr->hasOOB = FALSE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = TRUE;
		extAttr->hasGPIO = TRUE;
		extAttr->hasIRQN = FALSE;
		break;
	case 0x43:
		extAttr->hasLNA = FALSE;
		extAttr->hasNTSC = TRUE;
		extAttr->hasBTSC = TRUE;
		extAttr->hasOOB = FALSE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = TRUE;
		extAttr->hasGPIO = TRUE;
		extAttr->hasIRQN = FALSE;
		break;
	case 0x32:
		extAttr->hasLNA = TRUE;
		extAttr->hasNTSC = FALSE;
		extAttr->hasBTSC = FALSE;
		extAttr->hasOOB = TRUE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = TRUE;
		extAttr->hasGPIO = TRUE;
		extAttr->hasIRQN = TRUE;
		break;
	case 0x34:
		extAttr->hasLNA = FALSE;
		extAttr->hasNTSC = TRUE;
		extAttr->hasBTSC = TRUE;
		extAttr->hasOOB = TRUE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = TRUE;
		extAttr->hasGPIO = TRUE;
		extAttr->hasIRQN = TRUE;
		break;
	case 0x42:
		extAttr->hasLNA = TRUE;
		extAttr->hasNTSC = TRUE;
		extAttr->hasBTSC = TRUE;
		extAttr->hasOOB = TRUE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = TRUE;
		extAttr->hasGPIO = TRUE;
		extAttr->hasIRQN = TRUE;
		break;
	case 0x44:
		extAttr->hasLNA = FALSE;
		extAttr->hasNTSC = TRUE;
		extAttr->hasBTSC = TRUE;
		extAttr->hasOOB = TRUE;
		extAttr->hasSMATX = TRUE;
		extAttr->hasSMARX = TRUE;
		extAttr->hasGPIO = TRUE;
		extAttr->hasIRQN = TRUE;
		break;
	default:
		/* Unknown device variant */
		return (DRX_STS_ERROR);
		break;
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t PowerUpDevice()
* \brief Power up device.
* \param demod  Pointer to demodulator instance.
* \return DRXStatus_t.
* \return DRX_STS_OK    Success
* \retval DRX_STS_ERROR Failure, I2C or max retries reached
*
*/

#ifndef DRXJ_MAX_RETRIES_POWERUP
#define DRXJ_MAX_RETRIES_POWERUP 10
#endif

static DRXStatus_t PowerUpDevice(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);
	u8_t data = 0;
	u16_t retryCount = 0;
	struct i2c_device_addr wakeUpAddr;

	devAddr = demod->myI2CDevAddr;
	wakeUpAddr.i2cAddr = DRXJ_WAKE_UP_KEY;
	wakeUpAddr.i2cDevId = devAddr->i2cDevId;
	wakeUpAddr.userData = devAddr->userData;
	/* CHK_ERROR macro not used, I2C access may fail in this case: no ack
	   dummy write must be used to wake uop device, dummy read must be used to
	   reset HI state machine (avoiding actual writes) */
	do {
		data = 0;
		DRXBSP_I2C_WriteRead(&wakeUpAddr, 1, &data,
				     (struct i2c_device_addr *) (NULL), 0,
				     (pu8_t) (NULL));
		DRXBSP_HST_Sleep(10);
		retryCount++;
	} while ((DRXBSP_I2C_WriteRead
		  ((struct i2c_device_addr *) (NULL), 0, (pu8_t) (NULL), devAddr, 1,
		   &data)
		  != DRX_STS_OK) && (retryCount < DRXJ_MAX_RETRIES_POWERUP));

	/* Need some recovery time .... */
	DRXBSP_HST_Sleep(10);

	if (retryCount == DRXJ_MAX_RETRIES_POWERUP) {
		return (DRX_STS_ERROR);
	}

	return (DRX_STS_OK);
}

/*----------------------------------------------------------------------------*/
/* MPEG Output Configuration Functions - begin                                */
/*----------------------------------------------------------------------------*/
/**
* \fn DRXStatus_t CtrlSetCfgMPEGOutput()
* \brief Set MPEG output configuration of the device.
* \param devmod  Pointer to demodulator instance.
* \param cfgData Pointer to mpeg output configuaration.
* \return DRXStatus_t.
*
*  Configure MPEG output parameters.
*
*/
static DRXStatus_t
CtrlSetCfgMPEGOutput(pDRXDemodInstance_t demod, pDRXCfgMPEGOutput_t cfgData)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) (NULL);
	u16_t fecOcRegMode = 0;
	u16_t fecOcRegIprMode = 0;
	u16_t fecOcRegIprInvert = 0;
	u32_t maxBitRate = 0;
	u32_t rcnRate = 0;
	u32_t nrBits = 0;
	u16_t sioPdrMdCfg = 0;
	/* data mask for the output data byte */
	u16_t InvertDataMask =
	    FEC_OC_IPR_INVERT_MD7__M | FEC_OC_IPR_INVERT_MD6__M |
	    FEC_OC_IPR_INVERT_MD5__M | FEC_OC_IPR_INVERT_MD4__M |
	    FEC_OC_IPR_INVERT_MD3__M | FEC_OC_IPR_INVERT_MD2__M |
	    FEC_OC_IPR_INVERT_MD1__M | FEC_OC_IPR_INVERT_MD0__M;
	/* check arguments */
	if ((demod == NULL) || (cfgData == NULL)) {
		return (DRX_STS_INVALID_ARG);
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;

	if (cfgData->enableMPEGOutput == TRUE) {
		/* quick and dirty patch to set MPEG incase current std is not
		   producing MPEG */
		switch (extAttr->standard) {
		case DRX_STANDARD_8VSB:
		case DRX_STANDARD_ITU_A:
		case DRX_STANDARD_ITU_B:
		case DRX_STANDARD_ITU_C:
			break;
		default:
			/* not an MPEG producing std, just store MPEG cfg */
			commonAttr->mpegCfg.enableMPEGOutput =
			    cfgData->enableMPEGOutput;
			commonAttr->mpegCfg.insertRSByte =
			    cfgData->insertRSByte;
			commonAttr->mpegCfg.enableParallel =
			    cfgData->enableParallel;
			commonAttr->mpegCfg.invertDATA = cfgData->invertDATA;
			commonAttr->mpegCfg.invertERR = cfgData->invertERR;
			commonAttr->mpegCfg.invertSTR = cfgData->invertSTR;
			commonAttr->mpegCfg.invertVAL = cfgData->invertVAL;
			commonAttr->mpegCfg.invertCLK = cfgData->invertCLK;
			commonAttr->mpegCfg.staticCLK = cfgData->staticCLK;
			commonAttr->mpegCfg.bitrate = cfgData->bitrate;
			return (DRX_STS_OK);
		}

		WR16(devAddr, FEC_OC_OCR_INVERT__A, 0);
		switch (extAttr->standard) {
		case DRX_STANDARD_8VSB:
			WR16(devAddr, FEC_OC_FCT_USAGE__A, 7);	/* 2048 bytes fifo ram */
			WR16(devAddr, FEC_OC_TMD_CTL_UPD_RATE__A, 10);
			WR16(devAddr, FEC_OC_TMD_INT_UPD_RATE__A, 10);
			WR16(devAddr, FEC_OC_AVR_PARM_A__A, 5);
			WR16(devAddr, FEC_OC_AVR_PARM_B__A, 7);
			WR16(devAddr, FEC_OC_RCN_GAIN__A, 10);
			/* Low Water Mark for synchronization  */
			WR16(devAddr, FEC_OC_SNC_LWM__A, 3);
			/* High Water Mark for synchronization */
			WR16(devAddr, FEC_OC_SNC_HWM__A, 5);
			break;
		case DRX_STANDARD_ITU_A:
		case DRX_STANDARD_ITU_C:
			switch (extAttr->constellation) {
			case DRX_CONSTELLATION_QAM256:
				nrBits = 8;
				break;
			case DRX_CONSTELLATION_QAM128:
				nrBits = 7;
				break;
			case DRX_CONSTELLATION_QAM64:
				nrBits = 6;
				break;
			case DRX_CONSTELLATION_QAM32:
				nrBits = 5;
				break;
			case DRX_CONSTELLATION_QAM16:
				nrBits = 4;
				break;
			default:
				return (DRX_STS_ERROR);
			}	/* extAttr->constellation */
			/* maxBitRate = symbolRate * nrBits * coef */
			/* coef = 188/204                          */
			maxBitRate =
			    (extAttr->currSymbolRate / 8) * nrBits * 188;
			/* pass through b/c Annex A/c need following settings */
		case DRX_STANDARD_ITU_B:
			WR16(devAddr, FEC_OC_FCT_USAGE__A,
			     FEC_OC_FCT_USAGE__PRE);
			WR16(devAddr, FEC_OC_TMD_CTL_UPD_RATE__A,
			     FEC_OC_TMD_CTL_UPD_RATE__PRE);
			WR16(devAddr, FEC_OC_TMD_INT_UPD_RATE__A, 5);
			WR16(devAddr, FEC_OC_AVR_PARM_A__A,
			     FEC_OC_AVR_PARM_A__PRE);
			WR16(devAddr, FEC_OC_AVR_PARM_B__A,
			     FEC_OC_AVR_PARM_B__PRE);
			if (cfgData->staticCLK == TRUE) {
				WR16(devAddr, FEC_OC_RCN_GAIN__A, 0xD);
			} else {
				WR16(devAddr, FEC_OC_RCN_GAIN__A,
				     FEC_OC_RCN_GAIN__PRE);
			}
			WR16(devAddr, FEC_OC_SNC_LWM__A, 2);
			WR16(devAddr, FEC_OC_SNC_HWM__A, 12);
			break;
		default:
			break;
		}		/* swtich (standard) */

		/* Check insertion of the Reed-Solomon parity bytes */
		RR16(devAddr, FEC_OC_MODE__A, &fecOcRegMode);
		RR16(devAddr, FEC_OC_IPR_MODE__A, &fecOcRegIprMode);
		if (cfgData->insertRSByte == TRUE) {
			/* enable parity symbol forward */
			fecOcRegMode |= FEC_OC_MODE_PARITY__M;
			/* MVAL disable during parity bytes */
			fecOcRegIprMode |= FEC_OC_IPR_MODE_MVAL_DIS_PAR__M;
			switch (extAttr->standard) {
			case DRX_STANDARD_8VSB:
				rcnRate = 0x004854D3;
				break;
			case DRX_STANDARD_ITU_B:
				fecOcRegMode |= FEC_OC_MODE_TRANSPARENT__M;
				switch (extAttr->constellation) {
				case DRX_CONSTELLATION_QAM256:
					rcnRate = 0x008945E7;
					break;
				case DRX_CONSTELLATION_QAM64:
					rcnRate = 0x005F64D4;
					break;
				default:
					return (DRX_STS_ERROR);
				}
				break;
			case DRX_STANDARD_ITU_A:
			case DRX_STANDARD_ITU_C:
				/* insertRSByte = TRUE -> coef = 188/188 -> 1, RS bits are in MPEG output */
				rcnRate =
				    (Frac28
				     (maxBitRate,
				      (u32_t) (commonAttr->sysClockFreq / 8))) /
				    188;
				break;
			default:
				return (DRX_STS_ERROR);
			}	/* extAttr->standard */
		} else {	/* insertRSByte == FALSE */

			/* disable parity symbol forward */
			fecOcRegMode &= (~FEC_OC_MODE_PARITY__M);
			/* MVAL enable during parity bytes */
			fecOcRegIprMode &= (~FEC_OC_IPR_MODE_MVAL_DIS_PAR__M);
			switch (extAttr->standard) {
			case DRX_STANDARD_8VSB:
				rcnRate = 0x0041605C;
				break;
			case DRX_STANDARD_ITU_B:
				fecOcRegMode &= (~FEC_OC_MODE_TRANSPARENT__M);
				switch (extAttr->constellation) {
				case DRX_CONSTELLATION_QAM256:
					rcnRate = 0x0082D6A0;
					break;
				case DRX_CONSTELLATION_QAM64:
					rcnRate = 0x005AEC1A;
					break;
				default:
					return (DRX_STS_ERROR);
				}
				break;
			case DRX_STANDARD_ITU_A:
			case DRX_STANDARD_ITU_C:
				/* insertRSByte = FALSE -> coef = 188/204, RS bits not in MPEG output */
				rcnRate =
				    (Frac28
				     (maxBitRate,
				      (u32_t) (commonAttr->sysClockFreq / 8))) /
				    204;
				break;
			default:
				return (DRX_STS_ERROR);
			}	/* extAttr->standard */
		}

		if (cfgData->enableParallel == TRUE) {	/* MPEG data output is paralel -> clear ipr_mode[0] */
			fecOcRegIprMode &= (~(FEC_OC_IPR_MODE_SERIAL__M));
		} else {	/* MPEG data output is serial -> set ipr_mode[0] */
			fecOcRegIprMode |= FEC_OC_IPR_MODE_SERIAL__M;
		}

		/* Control slective inversion of output bits */
		if (cfgData->invertDATA == TRUE) {
			fecOcRegIprInvert |= InvertDataMask;
		} else {
			fecOcRegIprInvert &= (~(InvertDataMask));
		}

		if (cfgData->invertERR == TRUE) {
			fecOcRegIprInvert |= FEC_OC_IPR_INVERT_MERR__M;
		} else {
			fecOcRegIprInvert &= (~(FEC_OC_IPR_INVERT_MERR__M));
		}

		if (cfgData->invertSTR == TRUE) {
			fecOcRegIprInvert |= FEC_OC_IPR_INVERT_MSTRT__M;
		} else {
			fecOcRegIprInvert &= (~(FEC_OC_IPR_INVERT_MSTRT__M));
		}

		if (cfgData->invertVAL == TRUE) {
			fecOcRegIprInvert |= FEC_OC_IPR_INVERT_MVAL__M;
		} else {
			fecOcRegIprInvert &= (~(FEC_OC_IPR_INVERT_MVAL__M));
		}

		if (cfgData->invertCLK == TRUE) {
			fecOcRegIprInvert |= FEC_OC_IPR_INVERT_MCLK__M;
		} else {
			fecOcRegIprInvert &= (~(FEC_OC_IPR_INVERT_MCLK__M));
		}

		if (cfgData->staticCLK == TRUE) {	/* Static mode */
			u32_t dtoRate = 0;
			u32_t bitRate = 0;
			u16_t fecOcDtoBurstLen = 0;
			u16_t fecOcDtoPeriod = 0;

			fecOcDtoBurstLen = FEC_OC_DTO_BURST_LEN__PRE;

			switch (extAttr->standard) {
			case DRX_STANDARD_8VSB:
				fecOcDtoPeriod = 4;
				if (cfgData->insertRSByte == TRUE) {
					fecOcDtoBurstLen = 208;
				}
				break;
			case DRX_STANDARD_ITU_A:
				{
					u32_t symbolRateTh = 6400000;
					if (cfgData->insertRSByte == TRUE) {
						fecOcDtoBurstLen = 204;
						symbolRateTh = 5900000;
					}
					if (extAttr->currSymbolRate >=
					    symbolRateTh) {
						fecOcDtoPeriod = 0;
					} else {
						fecOcDtoPeriod = 1;
					}
				}
				break;
			case DRX_STANDARD_ITU_B:
				fecOcDtoPeriod = 1;
				if (cfgData->insertRSByte == TRUE) {
					fecOcDtoBurstLen = 128;
				}
				break;
			case DRX_STANDARD_ITU_C:
				fecOcDtoPeriod = 1;
				if (cfgData->insertRSByte == TRUE) {
					fecOcDtoBurstLen = 204;
				}
				break;
			default:
				return (DRX_STS_ERROR);
			}
			bitRate =
			    commonAttr->sysClockFreq * 1000 / (fecOcDtoPeriod +
							       2);
			dtoRate =
			    Frac28(bitRate, commonAttr->sysClockFreq * 1000);
			dtoRate >>= 3;
			WR16(devAddr, FEC_OC_DTO_RATE_HI__A,
			     (u16_t) ((dtoRate >> 16) & FEC_OC_DTO_RATE_HI__M));
			WR16(devAddr, FEC_OC_DTO_RATE_LO__A,
			     (u16_t) (dtoRate & FEC_OC_DTO_RATE_LO_RATE_LO__M));
			WR16(devAddr, FEC_OC_DTO_MODE__A,
			     FEC_OC_DTO_MODE_DYNAMIC__M |
			     FEC_OC_DTO_MODE_OFFSET_ENABLE__M);
			WR16(devAddr, FEC_OC_FCT_MODE__A,
			     FEC_OC_FCT_MODE_RAT_ENA__M |
			     FEC_OC_FCT_MODE_VIRT_ENA__M);
			WR16(devAddr, FEC_OC_DTO_BURST_LEN__A,
			     fecOcDtoBurstLen);
			if (extAttr->mpegOutputClockRate !=
			    DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO)
				fecOcDtoPeriod =
				    extAttr->mpegOutputClockRate - 1;
			WR16(devAddr, FEC_OC_DTO_PERIOD__A, fecOcDtoPeriod);
		} else {	/* Dynamic mode */

			WR16(devAddr, FEC_OC_DTO_MODE__A,
			     FEC_OC_DTO_MODE_DYNAMIC__M);
			WR16(devAddr, FEC_OC_FCT_MODE__A, 0);
		}

		WR32(devAddr, FEC_OC_RCN_CTL_RATE_LO__A, rcnRate);

		/* Write appropriate registers with requested configuration */
		WR16(devAddr, FEC_OC_MODE__A, fecOcRegMode);
		WR16(devAddr, FEC_OC_IPR_MODE__A, fecOcRegIprMode);
		WR16(devAddr, FEC_OC_IPR_INVERT__A, fecOcRegIprInvert);

		/* enabling for both parallel and serial now */
		/*  Write magic word to enable pdr reg write */
		WR16(devAddr, SIO_TOP_COMM_KEY__A, 0xFABA);
		/*  Set MPEG TS pads to outputmode */
		WR16(devAddr, SIO_PDR_MSTRT_CFG__A, 0x0013);
		WR16(devAddr, SIO_PDR_MERR_CFG__A, 0x0013);
		WR16(devAddr, SIO_PDR_MCLK_CFG__A,
		     MPEG_OUTPUT_CLK_DRIVE_STRENGTH << SIO_PDR_MCLK_CFG_DRIVE__B
		     | 0x03 << SIO_PDR_MCLK_CFG_MODE__B);
		WR16(devAddr, SIO_PDR_MVAL_CFG__A, 0x0013);
		sioPdrMdCfg =
		    MPEG_SERIAL_OUTPUT_PIN_DRIVE_STRENGTH <<
		    SIO_PDR_MD0_CFG_DRIVE__B | 0x03 << SIO_PDR_MD0_CFG_MODE__B;
		WR16(devAddr, SIO_PDR_MD0_CFG__A, sioPdrMdCfg);
		if (cfgData->enableParallel == TRUE) {	/* MPEG data output is paralel -> set MD1 to MD7 to output mode */
			sioPdrMdCfg =
			    MPEG_PARALLEL_OUTPUT_PIN_DRIVE_STRENGTH <<
			    SIO_PDR_MD0_CFG_DRIVE__B | 0x03 <<
			    SIO_PDR_MD0_CFG_MODE__B;
			WR16(devAddr, SIO_PDR_MD0_CFG__A, sioPdrMdCfg);
			WR16(devAddr, SIO_PDR_MD1_CFG__A, sioPdrMdCfg);
			WR16(devAddr, SIO_PDR_MD2_CFG__A, sioPdrMdCfg);
			WR16(devAddr, SIO_PDR_MD3_CFG__A, sioPdrMdCfg);
			WR16(devAddr, SIO_PDR_MD4_CFG__A, sioPdrMdCfg);
			WR16(devAddr, SIO_PDR_MD5_CFG__A, sioPdrMdCfg);
			WR16(devAddr, SIO_PDR_MD6_CFG__A, sioPdrMdCfg);
			WR16(devAddr, SIO_PDR_MD7_CFG__A, sioPdrMdCfg);
		} else {	/* MPEG data output is serial -> set MD1 to MD7 to tri-state */
			WR16(devAddr, SIO_PDR_MD1_CFG__A, 0x0000);
			WR16(devAddr, SIO_PDR_MD2_CFG__A, 0x0000);
			WR16(devAddr, SIO_PDR_MD3_CFG__A, 0x0000);
			WR16(devAddr, SIO_PDR_MD4_CFG__A, 0x0000);
			WR16(devAddr, SIO_PDR_MD5_CFG__A, 0x0000);
			WR16(devAddr, SIO_PDR_MD6_CFG__A, 0x0000);
			WR16(devAddr, SIO_PDR_MD7_CFG__A, 0x0000);
		}
		/*  Enable Monitor Bus output over MPEG pads and ctl input */
		WR16(devAddr, SIO_PDR_MON_CFG__A, 0x0000);
		/*  Write nomagic word to enable pdr reg write */
		WR16(devAddr, SIO_TOP_COMM_KEY__A, 0x0000);
	} else {
		/*  Write magic word to enable pdr reg write */
		WR16(devAddr, SIO_TOP_COMM_KEY__A, 0xFABA);
		/*  Set MPEG TS pads to inputmode */
		WR16(devAddr, SIO_PDR_MSTRT_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MERR_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MCLK_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MVAL_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MD0_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MD1_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MD2_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MD3_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MD4_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MD5_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MD6_CFG__A, 0x0000);
		WR16(devAddr, SIO_PDR_MD7_CFG__A, 0x0000);
		/* Enable Monitor Bus output over MPEG pads and ctl input */
		WR16(devAddr, SIO_PDR_MON_CFG__A, 0x0000);
		/* Write nomagic word to enable pdr reg write */
		WR16(devAddr, SIO_TOP_COMM_KEY__A, 0x0000);
	}

	/* save values for restore after re-acquire */
	commonAttr->mpegCfg.enableMPEGOutput = cfgData->enableMPEGOutput;
	commonAttr->mpegCfg.insertRSByte = cfgData->insertRSByte;
	commonAttr->mpegCfg.enableParallel = cfgData->enableParallel;
	commonAttr->mpegCfg.invertDATA = cfgData->invertDATA;
	commonAttr->mpegCfg.invertERR = cfgData->invertERR;
	commonAttr->mpegCfg.invertSTR = cfgData->invertSTR;
	commonAttr->mpegCfg.invertVAL = cfgData->invertVAL;
	commonAttr->mpegCfg.invertCLK = cfgData->invertCLK;
	commonAttr->mpegCfg.staticCLK = cfgData->staticCLK;
	commonAttr->mpegCfg.bitrate = cfgData->bitrate;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*----------------------------------------------------------------------------*/

/**
* \fn DRXStatus_t CtrlGetCfgMPEGOutput()
* \brief Get MPEG output configuration of the device.
* \param devmod  Pointer to demodulator instance.
* \param cfgData Pointer to MPEG output configuaration struct.
* \return DRXStatus_t.
*
*  Retrieve MPEG output configuartion.
*
*/
static DRXStatus_t
CtrlGetCfgMPEGOutput(pDRXDemodInstance_t demod, pDRXCfgMPEGOutput_t cfgData)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) (NULL);
	DRXLockStatus_t lockStatus = DRX_NOT_LOCKED;
	u32_t rateReg = 0;
	u32_t data64Hi = 0;
	u32_t data64Lo = 0;

	if (cfgData == NULL) {
		return (DRX_STS_INVALID_ARG);
	}
	devAddr = demod->myI2CDevAddr;
	commonAttr = demod->myCommonAttr;

	cfgData->enableMPEGOutput = commonAttr->mpegCfg.enableMPEGOutput;
	cfgData->insertRSByte = commonAttr->mpegCfg.insertRSByte;
	cfgData->enableParallel = commonAttr->mpegCfg.enableParallel;
	cfgData->invertDATA = commonAttr->mpegCfg.invertDATA;
	cfgData->invertERR = commonAttr->mpegCfg.invertERR;
	cfgData->invertSTR = commonAttr->mpegCfg.invertSTR;
	cfgData->invertVAL = commonAttr->mpegCfg.invertVAL;
	cfgData->invertCLK = commonAttr->mpegCfg.invertCLK;
	cfgData->staticCLK = commonAttr->mpegCfg.staticCLK;
	cfgData->bitrate = 0;

	CHK_ERROR(CtrlLockStatus(demod, &lockStatus));
	if ((lockStatus == DRX_LOCKED)) {
		RR32(devAddr, FEC_OC_RCN_DYN_RATE_LO__A, &rateReg);
		/* Frcn_rate = rateReg * Fsys / 2 ^ 25 */
		Mult32(rateReg, commonAttr->sysClockFreq * 1000, &data64Hi,
		       &data64Lo);
		cfgData->bitrate = (data64Hi << 7) | (data64Lo >> 25);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*----------------------------------------------------------------------------*/
/* MPEG Output Configuration Functions - end                                  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* miscellaneous configuartions - begin                           */
/*----------------------------------------------------------------------------*/

/**
* \fn DRXStatus_t SetMPEGTEIHandling()
* \brief Activate MPEG TEI handling settings.
* \param devmod  Pointer to demodulator instance.
* \return DRXStatus_t.
*
* This routine should be called during a set channel of QAM/VSB
*
*/
static DRXStatus_t SetMPEGTEIHandling(pDRXDemodInstance_t demod)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);
	u16_t fecOcDprMode = 0;
	u16_t fecOcSncMode = 0;
	u16_t fecOcEmsMode = 0;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	RR16(devAddr, FEC_OC_DPR_MODE__A, &fecOcDprMode);
	RR16(devAddr, FEC_OC_SNC_MODE__A, &fecOcSncMode);
	RR16(devAddr, FEC_OC_EMS_MODE__A, &fecOcEmsMode);

	/* reset to default, allow TEI bit to be changed */
	fecOcDprMode &= (~FEC_OC_DPR_MODE_ERR_DISABLE__M);
	fecOcSncMode &= (~(FEC_OC_SNC_MODE_ERROR_CTL__M |
			   FEC_OC_SNC_MODE_CORR_DISABLE__M));
	fecOcEmsMode &= (~FEC_OC_EMS_MODE_MODE__M);

	if (extAttr->disableTEIhandling == TRUE) {
		/* do not change TEI bit */
		fecOcDprMode |= FEC_OC_DPR_MODE_ERR_DISABLE__M;
		fecOcSncMode |= FEC_OC_SNC_MODE_CORR_DISABLE__M |
		    ((0x2) << (FEC_OC_SNC_MODE_ERROR_CTL__B));
		fecOcEmsMode |= ((0x01) << (FEC_OC_EMS_MODE_MODE__B));
	}

	WR16(devAddr, FEC_OC_DPR_MODE__A, fecOcDprMode);
	WR16(devAddr, FEC_OC_SNC_MODE__A, fecOcSncMode);
	WR16(devAddr, FEC_OC_EMS_MODE__A, fecOcEmsMode);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*----------------------------------------------------------------------------*/
/**
* \fn DRXStatus_t BitReverseMPEGOutput()
* \brief Set MPEG output bit-endian settings.
* \param devmod  Pointer to demodulator instance.
* \return DRXStatus_t.
*
* This routine should be called during a set channel of QAM/VSB
*
*/
static DRXStatus_t BitReverseMPEGOutput(pDRXDemodInstance_t demod)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);
	u16_t fecOcIprMode = 0;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	RR16(devAddr, FEC_OC_IPR_MODE__A, &fecOcIprMode);

	/* reset to default (normal bit order) */
	fecOcIprMode &= (~FEC_OC_IPR_MODE_REVERSE_ORDER__M);

	if (extAttr->bitReverseMpegOutout == TRUE) {
		/* reverse bit order */
		fecOcIprMode |= FEC_OC_IPR_MODE_REVERSE_ORDER__M;
	}

	WR16(devAddr, FEC_OC_IPR_MODE__A, fecOcIprMode);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*----------------------------------------------------------------------------*/
/**
* \fn DRXStatus_t SetMPEGOutputClockRate()
* \brief Set MPEG output clock rate.
* \param devmod  Pointer to demodulator instance.
* \return DRXStatus_t.
*
* This routine should be called during a set channel of QAM/VSB
*
*/
static DRXStatus_t SetMPEGOutputClockRate(pDRXDemodInstance_t demod)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	if (extAttr->mpegOutputClockRate != DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO) {
		WR16(devAddr, FEC_OC_DTO_PERIOD__A,
		     extAttr->mpegOutputClockRate - 1);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*----------------------------------------------------------------------------*/
/**
* \fn DRXStatus_t SetMPEGStartWidth()
* \brief Set MPEG start width.
* \param devmod  Pointer to demodulator instance.
* \return DRXStatus_t.
*
* This routine should be called during a set channel of QAM/VSB
*
*/
static DRXStatus_t SetMPEGStartWidth(pDRXDemodInstance_t demod)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);
	u16_t fecOcCommMb = 0;
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	commonAttr = demod->myCommonAttr;

	if ((commonAttr->mpegCfg.staticCLK == TRUE)
	    && (commonAttr->mpegCfg.enableParallel == FALSE)) {
		RR16(devAddr, FEC_OC_COMM_MB__A, &fecOcCommMb);
		fecOcCommMb &= ~FEC_OC_COMM_MB_CTL_ON;
		if (extAttr->mpegStartWidth == DRXJ_MPEG_START_WIDTH_8CLKCYC) {
			fecOcCommMb |= FEC_OC_COMM_MB_CTL_ON;
		}
		WR16(devAddr, FEC_OC_COMM_MB__A, fecOcCommMb);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*----------------------------------------------------------------------------*/
/**
* \fn DRXStatus_t CtrlSetCfgMpegOutputMisc()
* \brief Set miscellaneous configuartions
* \param devmod  Pointer to demodulator instance.
* \param cfgData pDRXJCfgMisc_t
* \return DRXStatus_t.
*
*  This routine can be used to set configuartion options that are DRXJ
*  specific and/or added to the requirements at a late stage.
*
*/
static DRXStatus_t
CtrlSetCfgMpegOutputMisc(pDRXDemodInstance_t demod,
			 pDRXJCfgMpegOutputMisc_t cfgData)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);

	if (cfgData == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;

	/*
	   Set disable TEI bit handling flag.
	   TEI must be left untouched by device in case of BER measurements using
	   external equipment that is unable to ignore the TEI bit in the TS.
	   Default will FALSE (enable TEI bit handling).
	   Reverse output bit order. Default is FALSE (msb on MD7 (parallel) or out first (serial)).
	   Set clock rate. Default is auto that is derived from symbol rate.
	   The flags and values will also be used to set registers during a set channel.
	 */
	extAttr->disableTEIhandling = cfgData->disableTEIHandling;
	extAttr->bitReverseMpegOutout = cfgData->bitReverseMpegOutout;
	extAttr->mpegOutputClockRate = cfgData->mpegOutputClockRate;
	extAttr->mpegStartWidth = cfgData->mpegStartWidth;
	/* Don't care what the active standard is, activate setting immediatly */
	CHK_ERROR(SetMPEGTEIHandling(demod));
	CHK_ERROR(BitReverseMPEGOutput(demod));
	CHK_ERROR(SetMPEGOutputClockRate(demod));
	CHK_ERROR(SetMPEGStartWidth(demod));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*----------------------------------------------------------------------------*/

/**
* \fn DRXStatus_t CtrlGetCfgMpegOutputMisc()
* \brief Get miscellaneous configuartions.
* \param devmod  Pointer to demodulator instance.
* \param cfgData Pointer to DRXJCfgMisc_t.
* \return DRXStatus_t.
*
*  This routine can be used to retreive the current setting of the configuartion
*  options that are DRXJ specific and/or added to the requirements at a
*  late stage.
*
*/
static DRXStatus_t
CtrlGetCfgMpegOutputMisc(pDRXDemodInstance_t demod,
			 pDRXJCfgMpegOutputMisc_t cfgData)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	u16_t data = 0;

	if (cfgData == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	cfgData->disableTEIHandling = extAttr->disableTEIhandling;
	cfgData->bitReverseMpegOutout = extAttr->bitReverseMpegOutout;
	cfgData->mpegStartWidth = extAttr->mpegStartWidth;
	if (extAttr->mpegOutputClockRate != DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO) {
		cfgData->mpegOutputClockRate = extAttr->mpegOutputClockRate;
	} else {
		RR16(demod->myI2CDevAddr, FEC_OC_DTO_PERIOD__A, &data);
		cfgData->mpegOutputClockRate =
		    (DRXJMpegOutputClockRate_t) (data + 1);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*----------------------------------------------------------------------------*/

/**
* \fn DRXStatus_t CtrlGetCfgHwCfg()
* \brief Get HW configuartions.
* \param devmod  Pointer to demodulator instance.
* \param cfgData Pointer to Bool.
* \return DRXStatus_t.
*
*  This routine can be used to retreive the current setting of the configuartion
*  options that are DRXJ specific and/or added to the requirements at a
*  late stage.
*
*/
static DRXStatus_t
CtrlGetCfgHwCfg(pDRXDemodInstance_t demod, pDRXJCfgHwCfg_t cfgData)
{
	u16_t data = 0;
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);

	if (cfgData == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, 0xFABA);
	RR16(demod->myI2CDevAddr, SIO_PDR_OHW_CFG__A, &data);
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, 0x0000);

	cfgData->i2cSpeed = (DRXJI2CSpeed_t) ((data >> 6) & 0x1);
	cfgData->xtalFreq = (DRXJXtalFreq_t) (data & 0x3);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*----------------------------------------------------------------------------*/
/* miscellaneous configuartions - end                             */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* UIO Configuration Functions - begin                                        */
/*----------------------------------------------------------------------------*/
/**
* \fn DRXStatus_t CtrlSetUIOCfg()
* \brief Configure modus oprandi UIO.
* \param demod Pointer to demodulator instance.
* \param UIOCfg Pointer to a configuration setting for a certain UIO.
* \return DRXStatus_t.
*/
static DRXStatus_t CtrlSetUIOCfg(pDRXDemodInstance_t demod, pDRXUIOCfg_t UIOCfg)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);

	if ((UIOCfg == NULL) || (demod == NULL)) {
		return DRX_STS_INVALID_ARG;
	}
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/*  Write magic word to enable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
	switch (UIOCfg->uio) {
      /*====================================================================*/
	case DRX_UIO1:
		/* DRX_UIO1: SMA_TX UIO-1 */
		if (extAttr->hasSMATX != TRUE)
			return DRX_STS_ERROR;
		switch (UIOCfg->mode) {
		case DRX_UIO_MODE_FIRMWARE_SMA:	/* falltrough */
		case DRX_UIO_MODE_FIRMWARE_SAW:	/* falltrough */
		case DRX_UIO_MODE_READWRITE:
			extAttr->uioSmaTxMode = UIOCfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			extAttr->uioSmaTxMode = UIOCfg->mode;
			/* pad configuration register is set 0 - input mode */
			WR16(demod->myI2CDevAddr, SIO_PDR_SMA_TX_CFG__A, 0);
			break;
		default:
			return DRX_STS_INVALID_ARG;
		}		/* switch ( UIOCfg->mode ) */
		break;
      /*====================================================================*/
	case DRX_UIO2:
		/* DRX_UIO2: SMA_RX UIO-2 */
		if (extAttr->hasSMARX != TRUE)
			return DRX_STS_ERROR;
		switch (UIOCfg->mode) {
		case DRX_UIO_MODE_FIRMWARE0:	/* falltrough */
		case DRX_UIO_MODE_READWRITE:
			extAttr->uioSmaRxMode = UIOCfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			extAttr->uioSmaRxMode = UIOCfg->mode;
			/* pad configuration register is set 0 - input mode */
			WR16(demod->myI2CDevAddr, SIO_PDR_SMA_RX_CFG__A, 0);
			break;
		default:
			return DRX_STS_INVALID_ARG;
			break;
		}		/* switch ( UIOCfg->mode ) */
		break;
      /*====================================================================*/
	case DRX_UIO3:
		/* DRX_UIO3: GPIO UIO-3 */
		if (extAttr->hasGPIO != TRUE)
			return DRX_STS_ERROR;
		switch (UIOCfg->mode) {
		case DRX_UIO_MODE_FIRMWARE0:	/* falltrough */
		case DRX_UIO_MODE_READWRITE:
			extAttr->uioGPIOMode = UIOCfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			extAttr->uioGPIOMode = UIOCfg->mode;
			/* pad configuration register is set 0 - input mode */
			WR16(demod->myI2CDevAddr, SIO_PDR_GPIO_CFG__A, 0);
			break;
		default:
			return DRX_STS_INVALID_ARG;
			break;
		}		/* switch ( UIOCfg->mode ) */
		break;
      /*====================================================================*/
	case DRX_UIO4:
		/* DRX_UIO4: IRQN UIO-4 */
		if (extAttr->hasIRQN != TRUE)
			return DRX_STS_ERROR;
		switch (UIOCfg->mode) {
		case DRX_UIO_MODE_READWRITE:
			extAttr->uioIRQNMode = UIOCfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			/* pad configuration register is set 0 - input mode */
			WR16(demod->myI2CDevAddr, SIO_PDR_IRQN_CFG__A, 0);
			extAttr->uioIRQNMode = UIOCfg->mode;
			break;
		case DRX_UIO_MODE_FIRMWARE0:	/* falltrough */
		default:
			return DRX_STS_INVALID_ARG;
			break;
		}		/* switch ( UIOCfg->mode ) */
		break;
      /*====================================================================*/
	default:
		return DRX_STS_INVALID_ARG;
	}			/* switch ( UIOCfg->uio ) */

	/*  Write magic word to disable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, 0x0000);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/
/**
* \fn DRXStatus_t CtrlGetUIOCfg()
* \brief Get modus oprandi UIO.
* \param demod Pointer to demodulator instance.
* \param UIOCfg Pointer to a configuration setting for a certain UIO.
* \return DRXStatus_t.
*/
static DRXStatus_t CtrlGetUIOCfg(pDRXDemodInstance_t demod, pDRXUIOCfg_t UIOCfg)
{

	pDRXJData_t extAttr = (pDRXJData_t) NULL;
	pDRXUIOMode_t UIOMode[4] = { NULL };
	pBool_t UIOAvailable[4] = { NULL };

	extAttr = demod->myExtAttr;

	UIOMode[DRX_UIO1] = &extAttr->uioSmaTxMode;
	UIOMode[DRX_UIO2] = &extAttr->uioSmaRxMode;
	UIOMode[DRX_UIO3] = &extAttr->uioGPIOMode;
	UIOMode[DRX_UIO4] = &extAttr->uioIRQNMode;

	UIOAvailable[DRX_UIO1] = &extAttr->hasSMATX;
	UIOAvailable[DRX_UIO2] = &extAttr->hasSMARX;
	UIOAvailable[DRX_UIO3] = &extAttr->hasGPIO;
	UIOAvailable[DRX_UIO4] = &extAttr->hasIRQN;

	if (UIOCfg == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	if ((UIOCfg->uio > DRX_UIO4) || (UIOCfg->uio < DRX_UIO1)) {
		return DRX_STS_INVALID_ARG;
	}

	if (*UIOAvailable[UIOCfg->uio] == FALSE) {
		return DRX_STS_ERROR;
	}

	UIOCfg->mode = *UIOMode[UIOCfg->uio];

	return DRX_STS_OK;
}

/**
* \fn DRXStatus_t CtrlUIOWrite()
* \brief Write to a UIO.
* \param demod Pointer to demodulator instance.
* \param UIOData Pointer to data container for a certain UIO.
* \return DRXStatus_t.
*/
static DRXStatus_t
CtrlUIOWrite(pDRXDemodInstance_t demod, pDRXUIOData_t UIOData)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	u16_t pinCfgValue = 0;
	u16_t value = 0;

	if ((UIOData == NULL) || (demod == NULL)) {
		return DRX_STS_INVALID_ARG;
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;

	/*  Write magic word to enable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
	switch (UIOData->uio) {
      /*====================================================================*/
	case DRX_UIO1:
		/* DRX_UIO1: SMA_TX UIO-1 */
		if (extAttr->hasSMATX != TRUE)
			return DRX_STS_ERROR;
		if ((extAttr->uioSmaTxMode != DRX_UIO_MODE_READWRITE)
		    && (extAttr->uioSmaTxMode != DRX_UIO_MODE_FIRMWARE_SAW)) {
			return DRX_STS_ERROR;
		}
		pinCfgValue = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pinCfgValue |= 0x0113;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - output mode */
		WR16(demod->myI2CDevAddr, SIO_PDR_SMA_TX_CFG__A, pinCfgValue);

		/* use corresponding bit in io data output registar */
		RR16(demod->myI2CDevAddr, SIO_PDR_UIO_OUT_LO__A, &value);
		if (UIOData->value == FALSE) {
			value &= 0x7FFF;	/* write zero to 15th bit - 1st UIO */
		} else {
			value |= 0x8000;	/* write one to 15th bit - 1st UIO */
		}
		/* write back to io data output register */
		WR16(demod->myI2CDevAddr, SIO_PDR_UIO_OUT_LO__A, value);
		break;
   /*======================================================================*/
	case DRX_UIO2:
		/* DRX_UIO2: SMA_RX UIO-2 */
		if (extAttr->hasSMARX != TRUE)
			return DRX_STS_ERROR;
		if (extAttr->uioSmaRxMode != DRX_UIO_MODE_READWRITE) {
			return DRX_STS_ERROR;
		}
		pinCfgValue = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pinCfgValue |= 0x0113;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - output mode */
		WR16(demod->myI2CDevAddr, SIO_PDR_SMA_RX_CFG__A, pinCfgValue);

		/* use corresponding bit in io data output registar */
		RR16(demod->myI2CDevAddr, SIO_PDR_UIO_OUT_LO__A, &value);
		if (UIOData->value == FALSE) {
			value &= 0xBFFF;	/* write zero to 14th bit - 2nd UIO */
		} else {
			value |= 0x4000;	/* write one to 14th bit - 2nd UIO */
		}
		/* write back to io data output register */
		WR16(demod->myI2CDevAddr, SIO_PDR_UIO_OUT_LO__A, value);
		break;
   /*====================================================================*/
	case DRX_UIO3:
		/* DRX_UIO3: ASEL UIO-3 */
		if (extAttr->hasGPIO != TRUE)
			return DRX_STS_ERROR;
		if (extAttr->uioGPIOMode != DRX_UIO_MODE_READWRITE) {
			return DRX_STS_ERROR;
		}
		pinCfgValue = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pinCfgValue |= 0x0113;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - output mode */
		WR16(demod->myI2CDevAddr, SIO_PDR_GPIO_CFG__A, pinCfgValue);

		/* use corresponding bit in io data output registar */
		RR16(demod->myI2CDevAddr, SIO_PDR_UIO_OUT_HI__A, &value);
		if (UIOData->value == FALSE) {
			value &= 0xFFFB;	/* write zero to 2nd bit - 3rd UIO */
		} else {
			value |= 0x0004;	/* write one to 2nd bit - 3rd UIO */
		}
		/* write back to io data output register */
		WR16(demod->myI2CDevAddr, SIO_PDR_UIO_OUT_HI__A, value);
		break;
   /*=====================================================================*/
	case DRX_UIO4:
		/* DRX_UIO4: IRQN UIO-4 */
		if (extAttr->hasIRQN != TRUE)
			return DRX_STS_ERROR;

		if (extAttr->uioIRQNMode != DRX_UIO_MODE_READWRITE) {
			return DRX_STS_ERROR;
		}
		pinCfgValue = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pinCfgValue |= 0x0113;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - output mode */
		WR16(demod->myI2CDevAddr, SIO_PDR_IRQN_CFG__A, pinCfgValue);

		/* use corresponding bit in io data output registar */
		RR16(demod->myI2CDevAddr, SIO_PDR_UIO_OUT_LO__A, &value);
		if (UIOData->value == FALSE) {
			value &= 0xEFFF;	/* write zero to 12th bit - 4th UIO */
		} else {
			value |= 0x1000;	/* write one to 12th bit - 4th UIO */
		}
		/* write back to io data output register */
		WR16(demod->myI2CDevAddr, SIO_PDR_UIO_OUT_LO__A, value);
		break;
      /*=====================================================================*/
	default:
		return DRX_STS_INVALID_ARG;
	}			/* switch ( UIOData->uio ) */

	/*  Write magic word to disable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, 0x0000);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
*\fn DRXStatus_t CtrlUIORead
*\brief Read from a UIO.
* \param demod Pointer to demodulator instance.
* \param UIOData Pointer to data container for a certain UIO.
* \return DRXStatus_t.
*/
static DRXStatus_t CtrlUIORead(pDRXDemodInstance_t demod, pDRXUIOData_t UIOData)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	u16_t pinCfgValue = 0;
	u16_t value = 0;

	if ((UIOData == NULL) || (demod == NULL)) {
		return DRX_STS_INVALID_ARG;
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;

	/*  Write magic word to enable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
	switch (UIOData->uio) {
      /*====================================================================*/
	case DRX_UIO1:
		/* DRX_UIO1: SMA_TX UIO-1 */
		if (extAttr->hasSMATX != TRUE)
			return DRX_STS_ERROR;

		if (extAttr->uioSmaTxMode != DRX_UIO_MODE_READWRITE) {
			return DRX_STS_ERROR;
		}
		pinCfgValue = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pinCfgValue |= 0x0110;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - input mode */
		WR16(demod->myI2CDevAddr, SIO_PDR_SMA_TX_CFG__A, pinCfgValue);

		RR16(demod->myI2CDevAddr, SIO_PDR_UIO_IN_LO__A, &value);
		if ((value & 0x8000) != 0) {	/* check 15th bit - 1st UIO */
			UIOData->value = TRUE;
		} else {
			UIOData->value = FALSE;
		}
		break;
   /*======================================================================*/
	case DRX_UIO2:
		/* DRX_UIO2: SMA_RX UIO-2 */
		if (extAttr->hasSMARX != TRUE)
			return DRX_STS_ERROR;

		if (extAttr->uioSmaRxMode != DRX_UIO_MODE_READWRITE) {
			return DRX_STS_ERROR;
		}
		pinCfgValue = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pinCfgValue |= 0x0110;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - input mode */
		WR16(demod->myI2CDevAddr, SIO_PDR_SMA_RX_CFG__A, pinCfgValue);

		RR16(demod->myI2CDevAddr, SIO_PDR_UIO_IN_LO__A, &value);

		if ((value & 0x4000) != 0) {	/* check 14th bit - 2nd UIO */
			UIOData->value = TRUE;
		} else {
			UIOData->value = FALSE;
		}
		break;
   /*=====================================================================*/
	case DRX_UIO3:
		/* DRX_UIO3: GPIO UIO-3 */
		if (extAttr->hasGPIO != TRUE)
			return DRX_STS_ERROR;

		if (extAttr->uioGPIOMode != DRX_UIO_MODE_READWRITE) {
			return DRX_STS_ERROR;
		}
		pinCfgValue = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pinCfgValue |= 0x0110;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - input mode */
		WR16(demod->myI2CDevAddr, SIO_PDR_GPIO_CFG__A, pinCfgValue);

		/* read io input data registar */
		RR16(demod->myI2CDevAddr, SIO_PDR_UIO_IN_HI__A, &value);
		if ((value & 0x0004) != 0) {	/* check 2nd bit - 3rd UIO */
			UIOData->value = TRUE;
		} else {
			UIOData->value = FALSE;
		}
		break;
   /*=====================================================================*/
	case DRX_UIO4:
		/* DRX_UIO4: IRQN UIO-4 */
		if (extAttr->hasIRQN != TRUE)
			return DRX_STS_ERROR;

		if (extAttr->uioIRQNMode != DRX_UIO_MODE_READWRITE) {
			return DRX_STS_ERROR;
		}
		pinCfgValue = 0;
		/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
		pinCfgValue |= 0x0110;
		/* io_pad_cfg_mode output mode is drive always */
		/* io_pad_cfg_drive is set to power 2 (23 mA) */

		/* write to io pad configuration register - input mode */
		WR16(demod->myI2CDevAddr, SIO_PDR_IRQN_CFG__A, pinCfgValue);

		/* read io input data registar */
		RR16(demod->myI2CDevAddr, SIO_PDR_UIO_IN_LO__A, &value);
		if ((value & 0x1000) != 0) {	/* check 12th bit - 4th UIO */
			UIOData->value = TRUE;
		} else {
			UIOData->value = FALSE;
		}
		break;
      /*====================================================================*/
	default:
		return DRX_STS_INVALID_ARG;
	}			/* switch ( UIOData->uio ) */

	/*  Write magic word to disable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, 0x0000);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*---------------------------------------------------------------------------*/
/* UIO Configuration Functions - end                                         */
/*---------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* I2C Bridge Functions - begin                                               */
/*----------------------------------------------------------------------------*/
/**
* \fn DRXStatus_t CtrlI2CBridge()
* \brief Open or close the I2C switch to tuner.
* \param demod Pointer to demodulator instance.
* \param bridgeClosed Pointer to bool indication if bridge is closed not.
* \return DRXStatus_t.

*/
static DRXStatus_t
CtrlI2CBridge(pDRXDemodInstance_t demod, pBool_t bridgeClosed)
{
	DRXJHiCmd_t hiCmd;
	u16_t result = 0;

	/* check arguments */
	if (bridgeClosed == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	hiCmd.cmd = SIO_HI_RA_RAM_CMD_BRDCTRL;
	hiCmd.param1 = SIO_HI_RA_RAM_PAR_1_PAR1_SEC_KEY;
	if (*bridgeClosed == TRUE) {
		hiCmd.param2 = SIO_HI_RA_RAM_PAR_2_BRD_CFG_CLOSED;
	} else {
		hiCmd.param2 = SIO_HI_RA_RAM_PAR_2_BRD_CFG_OPEN;
	}

	return HICommand(demod->myI2CDevAddr, &hiCmd, &result);
}

/*----------------------------------------------------------------------------*/
/* I2C Bridge Functions - end                                                 */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Smart antenna Functions - begin                                            */
/*----------------------------------------------------------------------------*/
/**
* \fn DRXStatus_t SmartAntInit()
* \brief Initialize Smart Antenna.
* \param pointer to DRXDemodInstance_t.
* \return DRXStatus_t.
*
*/
static DRXStatus_t SmartAntInit(pDRXDemodInstance_t demod)
{
	u16_t data = 0;
	pDRXJData_t extAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;
	DRXUIOCfg_t UIOCfg = { DRX_UIO1, DRX_UIO_MODE_FIRMWARE_SMA };

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/*  Write magic word to enable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
	/* init smart antenna */
	RR16(devAddr, SIO_SA_TX_COMMAND__A, &data);
	if (extAttr->smartAntInverted)
		WR16(devAddr, SIO_SA_TX_COMMAND__A,
		     (data | SIO_SA_TX_COMMAND_TX_INVERT__M)
		     | SIO_SA_TX_COMMAND_TX_ENABLE__M);
	else
		WR16(devAddr, SIO_SA_TX_COMMAND__A,
		     (data & (~SIO_SA_TX_COMMAND_TX_INVERT__M))
		     | SIO_SA_TX_COMMAND_TX_ENABLE__M);

	/* config SMA_TX pin to smart antenna mode */
	CHK_ERROR(CtrlSetUIOCfg(demod, &UIOCfg));
	WR16(demod->myI2CDevAddr, SIO_PDR_SMA_TX_CFG__A, 0x13);
	WR16(demod->myI2CDevAddr, SIO_PDR_SMA_TX_GPIO_FNC__A, 0x03);

	/*  Write magic word to disable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, 0x0000);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t CtrlSetCfgSmartAnt()
* \brief Set Smart Antenna.
* \param pointer to DRXJCfgSmartAnt_t.
* \return DRXStatus_t.
*
*/
static DRXStatus_t
CtrlSetCfgSmartAnt(pDRXDemodInstance_t demod, pDRXJCfgSmartAnt_t smartAnt)
{
	pDRXJData_t extAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;
	u16_t data = 0;
	u32_t startTime = 0;
	static Bool_t bitInverted = FALSE;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* check arguments */
	if (smartAnt == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	if (bitInverted != extAttr->smartAntInverted
	    || extAttr->uioSmaTxMode != DRX_UIO_MODE_FIRMWARE_SMA) {
		CHK_ERROR(SmartAntInit(demod));
		bitInverted = extAttr->smartAntInverted;
	}

	/*  Write magic word to enable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);

	switch (smartAnt->io) {
	case DRXJ_SMT_ANT_OUTPUT:
		/* enable Tx if Mode B (input) is supported */
		/*
		   RR16( devAddr, SIO_SA_TX_COMMAND__A, &data );
		   WR16( devAddr, SIO_SA_TX_COMMAND__A, data | SIO_SA_TX_COMMAND_TX_ENABLE__M );
		 */
		startTime = DRXBSP_HST_Clock();
		do {
			RR16(devAddr, SIO_SA_TX_STATUS__A, &data);
		} while ((data & SIO_SA_TX_STATUS_BUSY__M)
			 && ((DRXBSP_HST_Clock() - startTime) <
			     DRXJ_MAX_WAITTIME));

		if (data & SIO_SA_TX_STATUS_BUSY__M) {
			return (DRX_STS_ERROR);
		}

		/* write to smart antenna configuration register */
		WR16(devAddr, SIO_SA_TX_DATA0__A, 0x9200
		     | ((smartAnt->ctrlData & 0x0001) << 8)
		     | ((smartAnt->ctrlData & 0x0002) << 10)
		     | ((smartAnt->ctrlData & 0x0004) << 12)
		    );
		WR16(devAddr, SIO_SA_TX_DATA1__A, 0x4924
		     | ((smartAnt->ctrlData & 0x0008) >> 2)
		     | ((smartAnt->ctrlData & 0x0010))
		     | ((smartAnt->ctrlData & 0x0020) << 2)
		     | ((smartAnt->ctrlData & 0x0040) << 4)
		     | ((smartAnt->ctrlData & 0x0080) << 6)
		    );
		WR16(devAddr, SIO_SA_TX_DATA2__A, 0x2492
		     | ((smartAnt->ctrlData & 0x0100) >> 8)
		     | ((smartAnt->ctrlData & 0x0200) >> 6)
		     | ((smartAnt->ctrlData & 0x0400) >> 4)
		     | ((smartAnt->ctrlData & 0x0800) >> 2)
		     | ((smartAnt->ctrlData & 0x1000))
		     | ((smartAnt->ctrlData & 0x2000) << 2)
		    );
		WR16(devAddr, SIO_SA_TX_DATA3__A, 0xff8d);

		/* trigger the sending */
		WR16(devAddr, SIO_SA_TX_LENGTH__A, 56);

		break;
	case DRXJ_SMT_ANT_INPUT:
		/* disable Tx if Mode B (input) is supported */
		/*
		   RR16( devAddr, SIO_SA_TX_COMMAND__A, &data );
		   WR16( devAddr, SIO_SA_TX_COMMAND__A, data & (~SIO_SA_TX_COMMAND_TX_ENABLE__M) );
		 */
	default:
		return (DRX_STS_INVALID_ARG);
	}
	/*  Write magic word to enable pdr reg write               */
	WR16(demod->myI2CDevAddr, SIO_TOP_COMM_KEY__A, 0x0000);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

static DRXStatus_t SCUCommand(struct i2c_device_addr *devAddr, pDRXJSCUCmd_t cmd)
{
	u16_t curCmd = 0;
	u32_t startTime = 0;

	/* Check param */
	if (cmd == NULL)
		return (DRX_STS_INVALID_ARG);

	/* Wait until SCU command interface is ready to receive command */
	RR16(devAddr, SCU_RAM_COMMAND__A, &curCmd);
	if (curCmd != DRX_SCU_READY) {
		return (DRX_STS_ERROR);
	}

	switch (cmd->parameterLen) {
	case 5:
		WR16(devAddr, SCU_RAM_PARAM_4__A, *(cmd->parameter + 4));	/* fallthrough */
	case 4:
		WR16(devAddr, SCU_RAM_PARAM_3__A, *(cmd->parameter + 3));	/* fallthrough */
	case 3:
		WR16(devAddr, SCU_RAM_PARAM_2__A, *(cmd->parameter + 2));	/* fallthrough */
	case 2:
		WR16(devAddr, SCU_RAM_PARAM_1__A, *(cmd->parameter + 1));	/* fallthrough */
	case 1:
		WR16(devAddr, SCU_RAM_PARAM_0__A, *(cmd->parameter + 0));	/* fallthrough */
	case 0:
		/* do nothing */
		break;
	default:
		/* this number of parameters is not supported */
		return (DRX_STS_ERROR);
	}
	WR16(devAddr, SCU_RAM_COMMAND__A, cmd->command);

	/* Wait until SCU has processed command */
	startTime = DRXBSP_HST_Clock();
	do {
		RR16(devAddr, SCU_RAM_COMMAND__A, &curCmd);
	} while (!(curCmd == DRX_SCU_READY)
		 && ((DRXBSP_HST_Clock() - startTime) < DRXJ_MAX_WAITTIME));

	if (curCmd != DRX_SCU_READY) {
		return (DRX_STS_ERROR);
	}

	/* read results */
	if ((cmd->resultLen > 0) && (cmd->result != NULL)) {
		s16_t err;

		switch (cmd->resultLen) {
		case 4:
			RR16(devAddr, SCU_RAM_PARAM_3__A, cmd->result + 3);	/* fallthrough */
		case 3:
			RR16(devAddr, SCU_RAM_PARAM_2__A, cmd->result + 2);	/* fallthrough */
		case 2:
			RR16(devAddr, SCU_RAM_PARAM_1__A, cmd->result + 1);	/* fallthrough */
		case 1:
			RR16(devAddr, SCU_RAM_PARAM_0__A, cmd->result + 0);	/* fallthrough */
		case 0:
			/* do nothing */
			break;
		default:
			/* this number of parameters is not supported */
			return (DRX_STS_ERROR);
		}

		/* Check if an error was reported by SCU */
		err = cmd->result[0];

		/* check a few fixed error codes */
		if ((err == (s16_t) SCU_RAM_PARAM_0_RESULT_UNKSTD)
		    || (err == (s16_t) SCU_RAM_PARAM_0_RESULT_UNKCMD)
		    || (err == (s16_t) SCU_RAM_PARAM_0_RESULT_INVPAR)
		    || (err == (s16_t) SCU_RAM_PARAM_0_RESULT_SIZE)
		    ) {
			return DRX_STS_INVALID_ARG;
		}
		/* here it is assumed that negative means error, and positive no error */
		else if (err < 0) {
			return DRX_STS_ERROR;
		} else {
			return DRX_STS_OK;
		}
	}

	return (DRX_STS_OK);

rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t DRXJ_DAP_SCUAtomicReadWriteBlock()
* \brief Basic access routine for SCU atomic read or write access
* \param devAddr  pointer to i2c dev address
* \param addr     destination/source address
* \param datasize size of data buffer in bytes
* \param data     pointer to data buffer
* \return DRXStatus_t
* \retval DRX_STS_OK Succes
* \retval DRX_STS_ERROR Timeout, I2C error, illegal bank
*
*/
#define ADDR_AT_SCU_SPACE(x) ((x - 0x82E000) * 2)
static
DRXStatus_t DRXJ_DAP_SCU_AtomicReadWriteBlock(struct i2c_device_addr *devAddr, DRXaddr_t addr, u16_t datasize,	/* max 30 bytes because the limit of SCU parameter */
					      pu8_t data, Bool_t readFlag)
{
	DRXJSCUCmd_t scuCmd;
	u16_t setParamParameters[15];
	u16_t cmdResult[15];

	/* Parameter check */
	if ((data == NULL) ||
	    (devAddr == NULL) || ((datasize % 2) != 0) || ((datasize / 2) > 16)
	    ) {
		return (DRX_STS_INVALID_ARG);
	}

	setParamParameters[1] = (u16_t) ADDR_AT_SCU_SPACE(addr);
	if (readFlag) {		/* read */
		setParamParameters[0] = ((~(0x0080)) & datasize);
		scuCmd.parameterLen = 2;
		scuCmd.resultLen = datasize / 2 + 2;
	} else {
		int i = 0;

		setParamParameters[0] = 0x0080 | datasize;
		for (i = 0; i < (datasize / 2); i++) {
			setParamParameters[i + 2] =
			    (data[2 * i] | (data[(2 * i) + 1] << 8));
		}
		scuCmd.parameterLen = datasize / 2 + 2;
		scuCmd.resultLen = 1;
	}

	scuCmd.command =
	    SCU_RAM_COMMAND_STANDARD_TOP |
	    SCU_RAM_COMMAND_CMD_AUX_SCU_ATOMIC_ACCESS;
	scuCmd.result = cmdResult;
	scuCmd.parameter = setParamParameters;
	CHK_ERROR(SCUCommand(devAddr, &scuCmd));

	if (readFlag == TRUE) {
		int i = 0;
		/* read data from buffer */
		for (i = 0; i < (datasize / 2); i++) {
			data[2 * i] = (u8_t) (scuCmd.result[i + 2] & 0xFF);
			data[(2 * i) + 1] = (u8_t) (scuCmd.result[i + 2] >> 8);
		}
	}

	return DRX_STS_OK;

rw_error:
	return (DRX_STS_ERROR);

}

/*============================================================================*/

/**
* \fn DRXStatus_t DRXJ_DAP_AtomicReadReg16()
* \brief Atomic read of 16 bits words
*/
static
DRXStatus_t DRXJ_DAP_SCU_AtomicReadReg16(struct i2c_device_addr *devAddr,
					 DRXaddr_t addr,
					 pu16_t data, DRXflags_t flags)
{
	u8_t buf[2];
	DRXStatus_t rc = DRX_STS_ERROR;
	u16_t word = 0;

	if (!data) {
		return DRX_STS_INVALID_ARG;
	}

	rc = DRXJ_DAP_SCU_AtomicReadWriteBlock(devAddr, addr, 2, buf, TRUE);

	word = (u16_t) (buf[0] + (buf[1] << 8));

	*data = word;

	return rc;
}

/*============================================================================*/
/**
* \fn DRXStatus_t DRXJ_DAP_SCU_AtomicWriteReg16()
* \brief Atomic read of 16 bits words
*/
static
DRXStatus_t DRXJ_DAP_SCU_AtomicWriteReg16(struct i2c_device_addr *devAddr,
					  DRXaddr_t addr,
					  u16_t data, DRXflags_t flags)
{
	u8_t buf[2];
	DRXStatus_t rc = DRX_STS_ERROR;

	buf[0] = (u8_t) (data & 0xff);
	buf[1] = (u8_t) ((data >> 8) & 0xff);

	rc = DRXJ_DAP_SCU_AtomicReadWriteBlock(devAddr, addr, 2, buf, FALSE);

	return rc;
}

static DRXStatus_t
CtrlI2CWriteRead(pDRXDemodInstance_t demod, pDRXI2CData_t i2cData)
{
	return (DRX_STS_FUNC_NOT_AVAILABLE);
}

DRXStatus_t
TunerI2CWriteRead(pTUNERInstance_t tuner,
		  struct i2c_device_addr *wDevAddr,
		  u16_t wCount,
		  pu8_t wData,
		  struct i2c_device_addr *rDevAddr, u16_t rCount, pu8_t rData)
{
	pDRXDemodInstance_t demod;
	DRXI2CData_t i2cData =
	    { 2, wDevAddr, wCount, wData, rDevAddr, rCount, rData };

	demod = (pDRXDemodInstance_t) (tuner->myCommonAttr->myUserData);

	return (CtrlI2CWriteRead(demod, &i2cData));
}

/* -------------------------------------------------------------------------- */
/**
* \brief Measure result of ADC synchronisation
* \param demod demod instance
* \param count (returned) count
* \return DRXStatus_t.
* \retval DRX_STS_OK    Success
* \retval DRX_STS_ERROR Failure: I2C error
*
*/
static DRXStatus_t ADCSyncMeasurement(pDRXDemodInstance_t demod, pu16_t count)
{
	u16_t data = 0;
	struct i2c_device_addr *devAddr = NULL;

	devAddr = demod->myI2CDevAddr;

	/* Start measurement */
	WR16(devAddr, IQM_AF_COMM_EXEC__A, IQM_AF_COMM_EXEC_ACTIVE);
	WR16(devAddr, IQM_AF_START_LOCK__A, 1);

	/* Wait at least 3*128*(1/sysclk) <<< 1 millisec */
	CHK_ERROR(DRXBSP_HST_Sleep(1));

	*count = 0;
	RR16(devAddr, IQM_AF_PHASE0__A, &data);
	if (data == 127) {
		*count = *count + 1;
	}
	RR16(devAddr, IQM_AF_PHASE1__A, &data);
	if (data == 127) {
		*count = *count + 1;
	}
	RR16(devAddr, IQM_AF_PHASE2__A, &data);
	if (data == 127) {
		*count = *count + 1;
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \brief Synchronize analog and digital clock domains
* \param demod demod instance
* \return DRXStatus_t.
* \retval DRX_STS_OK    Success
* \retval DRX_STS_ERROR Failure: I2C error or failure to synchronize
*
* An IQM reset will also reset the results of this synchronization.
* After an IQM reset this routine needs to be called again.
*
*/

static DRXStatus_t ADCSynchronization(pDRXDemodInstance_t demod)
{
	u16_t count = 0;
	struct i2c_device_addr *devAddr = NULL;

	devAddr = demod->myI2CDevAddr;

	CHK_ERROR(ADCSyncMeasurement(demod, &count));

	if (count == 1) {
		/* Try sampling on a diffrent edge */
		u16_t clkNeg = 0;

		RR16(devAddr, IQM_AF_CLKNEG__A, &clkNeg);

		clkNeg ^= IQM_AF_CLKNEG_CLKNEGDATA__M;
		WR16(devAddr, IQM_AF_CLKNEG__A, clkNeg);

		CHK_ERROR(ADCSyncMeasurement(demod, &count));
	}

	if (count < 2) {
		/* TODO: implement fallback scenarios */
		return (DRX_STS_ERROR);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \brief Configure IQM AF registers
* \param demod instance of demodulator.
* \param active
* \return DRXStatus_t.
*/
static DRXStatus_t IQMSetAf(pDRXDemodInstance_t demod, Bool_t active)
{
	u16_t data = 0;
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = demod->myI2CDevAddr;

	/* Configure IQM */
	RR16(devAddr, IQM_AF_STDBY__A, &data);
	if (!active) {
		data &= ((~IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE)
			 & (~IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE)
			 & (~IQM_AF_STDBY_STDBY_PD_A2_ACTIVE)
			 & (~IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE)
			 & (~IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE)
		    );
	} else {		/* active */

		data |= (IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE
			 | IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE
			 | IQM_AF_STDBY_STDBY_PD_A2_ACTIVE
			 | IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE
			 | IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE);
	}
	WR16(devAddr, IQM_AF_STDBY__A, data);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
static DRXStatus_t
CtrlSetCfgATVOutput(pDRXDemodInstance_t demod, pDRXJCfgAtvOutput_t outputCfg);

/**
* \brief set configuration of pin-safe mode
* \param demod instance of demodulator.
* \param enable boolean; TRUE: activate pin-safe mode, FALSE: de-activate p.s.m.
* \return DRXStatus_t.
*/
static DRXStatus_t
CtrlSetCfgPdrSafeMode(pDRXDemodInstance_t demod, pBool_t enable)
{
	pDRXJData_t extAttr = (pDRXJData_t) NULL;
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) NULL;

	if (enable == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	commonAttr = demod->myCommonAttr;

	/*  Write magic word to enable pdr reg write  */
	WR16(devAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);

	if (*enable == TRUE) {
		Bool_t bridgeEnabled = FALSE;

		/* MPEG pins to input */
		WR16(devAddr, SIO_PDR_MSTRT_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MERR_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MCLK_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MVAL_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MD0_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MD1_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MD2_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MD3_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MD4_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MD5_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MD6_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_MD7_CFG__A, DRXJ_PIN_SAFE_MODE);

		/* PD_I2C_SDA2 Bridge off, Port2 Inactive
		   PD_I2C_SCL2 Bridge off, Port2 Inactive */
		CHK_ERROR(CtrlI2CBridge(demod, &bridgeEnabled));
		WR16(devAddr, SIO_PDR_I2C_SDA2_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_I2C_SCL2_CFG__A, DRXJ_PIN_SAFE_MODE);

		/*  PD_GPIO     Store and set to input
		   PD_VSYNC    Store and set to input
		   PD_SMA_RX   Store and set to input
		   PD_SMA_TX   Store and set to input */
		RR16(devAddr, SIO_PDR_GPIO_CFG__A,
		     &extAttr->pdrSafeRestoreValGpio);
		RR16(devAddr, SIO_PDR_VSYNC_CFG__A,
		     &extAttr->pdrSafeRestoreValVSync);
		RR16(devAddr, SIO_PDR_SMA_RX_CFG__A,
		     &extAttr->pdrSafeRestoreValSmaRx);
		RR16(devAddr, SIO_PDR_SMA_TX_CFG__A,
		     &extAttr->pdrSafeRestoreValSmaTx);
		WR16(devAddr, SIO_PDR_GPIO_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_VSYNC_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_SMA_RX_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_SMA_TX_CFG__A, DRXJ_PIN_SAFE_MODE);

		/*  PD_RF_AGC   Analog DAC outputs, cannot be set to input or tristate!
		   PD_IF_AGC   Analog DAC outputs, cannot be set to input or tristate! */
		CHK_ERROR(IQMSetAf(demod, FALSE));

		/*  PD_CVBS     Analog DAC output, standby mode
		   PD_SIF      Analog DAC output, standby mode */
		WR16(devAddr, ATV_TOP_STDBY__A,
		     (ATV_TOP_STDBY_SIF_STDBY_STANDBY &
		      (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE)));

		/*  PD_I2S_CL   Input
		   PD_I2S_DA   Input
		   PD_I2S_WS   Input */
		WR16(devAddr, SIO_PDR_I2S_CL_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_I2S_DA_CFG__A, DRXJ_PIN_SAFE_MODE);
		WR16(devAddr, SIO_PDR_I2S_WS_CFG__A, DRXJ_PIN_SAFE_MODE);
	} else {
		/* No need to restore MPEG pins;
		   is done in SetStandard/SetChannel */

		/* PD_I2C_SDA2 Port2 active
		   PD_I2C_SCL2 Port2 active */
		WR16(devAddr, SIO_PDR_I2C_SDA2_CFG__A,
		     SIO_PDR_I2C_SDA2_CFG__PRE);
		WR16(devAddr, SIO_PDR_I2C_SCL2_CFG__A,
		     SIO_PDR_I2C_SCL2_CFG__PRE);

		/*  PD_GPIO     Restore
		   PD_VSYNC    Restore
		   PD_SMA_RX   Restore
		   PD_SMA_TX   Restore */
		WR16(devAddr, SIO_PDR_GPIO_CFG__A,
		     extAttr->pdrSafeRestoreValGpio);
		WR16(devAddr, SIO_PDR_VSYNC_CFG__A,
		     extAttr->pdrSafeRestoreValVSync);
		WR16(devAddr, SIO_PDR_SMA_RX_CFG__A,
		     extAttr->pdrSafeRestoreValSmaRx);
		WR16(devAddr, SIO_PDR_SMA_TX_CFG__A,
		     extAttr->pdrSafeRestoreValSmaTx);

		/*  PD_RF_AGC, PD_IF_AGC
		   No need to restore; will be restored in SetStandard/SetChannel */

		/*  PD_CVBS, PD_SIF
		   No need to restore; will be restored in SetStandard/SetChannel */

		/*  PD_I2S_CL, PD_I2S_DA, PD_I2S_WS
		   Should be restored via DRX_CTRL_SET_AUD */
	}

	/*  Write magic word to disable pdr reg write */
	WR16(devAddr, SIO_TOP_COMM_KEY__A, 0x0000);
	extAttr->pdrSafeMode = *enable;

	return (DRX_STS_OK);

rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */

/**
* \brief get configuration of pin-safe mode
* \param demod instance of demodulator.
* \param enable boolean indicating whether pin-safe mode is active
* \return DRXStatus_t.
*/
static DRXStatus_t
CtrlGetCfgPdrSafeMode(pDRXDemodInstance_t demod, pBool_t enabled)
{
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	if (enabled == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	*enabled = extAttr->pdrSafeMode;

	return (DRX_STS_OK);
}

/**
* \brief Verifies whether microcode can be loaded.
* \param demod Demodulator instance.
* \return DRXStatus_t.
*/
static DRXStatus_t CtrlValidateUCode(pDRXDemodInstance_t demod)
{
	u32_t mcDev, mcPatch;
	u16_t verType;

	/* Check device.
	 *  Disallow microcode if:
	 *   - MC has version record AND
	 *   - device ID in version record is not 0 AND
	 *   - product ID in version record's device ID does not
	 *     match DRXJ1 product IDs - 0x393 or 0x394
	 */
	DRX_GET_MCVERTYPE(demod, verType);
	DRX_GET_MCDEV(demod, mcDev);
	DRX_GET_MCPATCH(demod, mcPatch);

	if (DRX_ISMCVERTYPE(verType)) {
		if ((mcDev != 0) &&
		    (((mcDev >> 16) & 0xFFF) != 0x393) &&
		    (((mcDev >> 16) & 0xFFF) != 0x394)) {
			/* Microcode is marked for another device - error */
			return DRX_STS_INVALID_ARG;
		} else if (mcPatch != 0) {
			/* Patch not allowed because there is no ROM */
			return DRX_STS_INVALID_ARG;
		}
	}

	/* Everything else: OK */
	return DRX_STS_OK;
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
* \fn DRXStatus_t InitAGC ()
* \brief Initialize AGC for all standards.
* \param demod instance of demodulator.
* \param channel pointer to channel data.
* \return DRXStatus_t.
*/
static DRXStatus_t InitAGC(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXCommonAttr_t commonAttr = NULL;
	pDRXJData_t extAttr = NULL;
	pDRXJCfgAgc_t pAgcRfSettings = NULL;
	pDRXJCfgAgc_t pAgcIfSettings = NULL;
	u16_t IngainTgtMax = 0;
	u16_t clpDirTo = 0;
	u16_t snsSumMax = 0;
	u16_t clpSumMax = 0;
	u16_t snsDirTo = 0;
	u16_t kiInnergainMin = 0;
	u16_t agcKi = 0;
	u16_t kiMax = 0;
	u16_t ifIaccuHiTgtMin = 0;
	u16_t data = 0;
	u16_t agcKiDgain = 0;
	u16_t kiMin = 0;
	u16_t clpCtrlMode = 0;
	u16_t agcRf = 0;
	u16_t agcIf = 0;
	devAddr = demod->myI2CDevAddr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	switch (extAttr->standard) {
	case DRX_STANDARD_8VSB:
		clpSumMax = 1023;
		clpDirTo = (u16_t) (-9);
		snsSumMax = 1023;
		snsDirTo = (u16_t) (-9);
		kiInnergainMin = (u16_t) (-32768);
		kiMax = 0x032C;
		agcKiDgain = 0xC;
		ifIaccuHiTgtMin = 2047;
		kiMin = 0x0117;
		IngainTgtMax = 16383;
		clpCtrlMode = 0;
		WR16(devAddr, SCU_RAM_AGC_KI_MINGAIN__A, 0x7fff);
		WR16(devAddr, SCU_RAM_AGC_KI_MAXGAIN__A, 0x0);
		WR16(devAddr, SCU_RAM_AGC_CLP_SUM__A, 0);
		WR16(devAddr, SCU_RAM_AGC_CLP_CYCCNT__A, 0);
		WR16(devAddr, SCU_RAM_AGC_CLP_DIR_WD__A, 0);
		WR16(devAddr, SCU_RAM_AGC_CLP_DIR_STP__A, 1);
		WR16(devAddr, SCU_RAM_AGC_SNS_SUM__A, 0);
		WR16(devAddr, SCU_RAM_AGC_SNS_CYCCNT__A, 0);
		WR16(devAddr, SCU_RAM_AGC_SNS_DIR_WD__A, 0);
		WR16(devAddr, SCU_RAM_AGC_SNS_DIR_STP__A, 1);
		WR16(devAddr, SCU_RAM_AGC_INGAIN__A, 1024);
		WR16(devAddr, SCU_RAM_VSB_AGC_POW_TGT__A, 22600);
		WR16(devAddr, SCU_RAM_AGC_INGAIN_TGT__A, 13200);
		pAgcIfSettings = &(extAttr->vsbIfAgcCfg);
		pAgcRfSettings = &(extAttr->vsbRfAgcCfg);
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_C:
	case DRX_STANDARD_ITU_B:
		IngainTgtMax = 5119;
		clpSumMax = 1023;
		clpDirTo = (u16_t) (-5);
		snsSumMax = 127;
		snsDirTo = (u16_t) (-3);
		kiInnergainMin = 0;
		kiMax = 0x0657;
		ifIaccuHiTgtMin = 2047;
		agcKiDgain = 0x7;
		kiMin = 0x0117;
		clpCtrlMode = 0;
		WR16(devAddr, SCU_RAM_AGC_KI_MINGAIN__A, 0x7fff);
		WR16(devAddr, SCU_RAM_AGC_KI_MAXGAIN__A, 0x0);
		WR16(devAddr, SCU_RAM_AGC_CLP_SUM__A, 0);
		WR16(devAddr, SCU_RAM_AGC_CLP_CYCCNT__A, 0);
		WR16(devAddr, SCU_RAM_AGC_CLP_DIR_WD__A, 0);
		WR16(devAddr, SCU_RAM_AGC_CLP_DIR_STP__A, 1);
		WR16(devAddr, SCU_RAM_AGC_SNS_SUM__A, 0);
		WR16(devAddr, SCU_RAM_AGC_SNS_CYCCNT__A, 0);
		WR16(devAddr, SCU_RAM_AGC_SNS_DIR_WD__A, 0);
		WR16(devAddr, SCU_RAM_AGC_SNS_DIR_STP__A, 1);
		pAgcIfSettings = &(extAttr->qamIfAgcCfg);
		pAgcRfSettings = &(extAttr->qamRfAgcCfg);
		WR16(devAddr, SCU_RAM_AGC_INGAIN_TGT__A, pAgcIfSettings->top);

		RR16(devAddr, SCU_RAM_AGC_KI__A, &agcKi);
		agcKi &= 0xf000;
		WR16(devAddr, SCU_RAM_AGC_KI__A, agcKi);
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_FM:
		clpSumMax = 1023;
		snsSumMax = 1023;
		kiInnergainMin = (u16_t) (-32768);
		ifIaccuHiTgtMin = 2047;
		agcKiDgain = 0x7;
		kiMin = 0x0225;
		kiMax = 0x0547;
		clpDirTo = (u16_t) (-9);
		snsDirTo = (u16_t) (-9);
		IngainTgtMax = 9000;
		clpCtrlMode = 1;
		pAgcIfSettings = &(extAttr->atvIfAgcCfg);
		pAgcRfSettings = &(extAttr->atvRfAgcCfg);
		WR16(devAddr, SCU_RAM_AGC_INGAIN_TGT__A, pAgcIfSettings->top);
		break;
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
		clpSumMax = 1023;
		snsSumMax = 1023;
		kiInnergainMin = (u16_t) (-32768);
		ifIaccuHiTgtMin = 2047;
		agcKiDgain = 0x7;
		kiMin = 0x0225;
		kiMax = 0x0547;
		clpDirTo = (u16_t) (-9);
		IngainTgtMax = 9000;
		pAgcIfSettings = &(extAttr->atvIfAgcCfg);
		pAgcRfSettings = &(extAttr->atvRfAgcCfg);
		snsDirTo = (u16_t) (-9);
		clpCtrlMode = 1;
		WR16(devAddr, SCU_RAM_AGC_INGAIN_TGT__A, pAgcIfSettings->top);
		break;
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
		clpSumMax = 1023;
		snsSumMax = 1023;
		kiInnergainMin = (u16_t) (-32768);
		ifIaccuHiTgtMin = 2047;
		agcKiDgain = 0x7;
		kiMin = 0x0225;
		kiMax = 0x0547;
		clpDirTo = (u16_t) (-9);
		snsDirTo = (u16_t) (-9);
		IngainTgtMax = 9000;
		clpCtrlMode = 1;
		pAgcIfSettings = &(extAttr->atvIfAgcCfg);
		pAgcRfSettings = &(extAttr->atvRfAgcCfg);
		WR16(devAddr, SCU_RAM_AGC_INGAIN_TGT__A, pAgcIfSettings->top);
		break;
#endif
	default:
		return (DRX_STS_INVALID_ARG);
	}

	/* for new AGC interface */
	WR16(devAddr, SCU_RAM_AGC_INGAIN_TGT_MIN__A, pAgcIfSettings->top);
	WR16(devAddr, SCU_RAM_AGC_INGAIN__A, pAgcIfSettings->top);	/* Gain fed from inner to outer AGC */
	WR16(devAddr, SCU_RAM_AGC_INGAIN_TGT_MAX__A, IngainTgtMax);
	WR16(devAddr, SCU_RAM_AGC_IF_IACCU_HI_TGT_MIN__A, ifIaccuHiTgtMin);
	WR16(devAddr, SCU_RAM_AGC_IF_IACCU_HI__A, 0);	/* set to pAgcSettings->top before */
	WR16(devAddr, SCU_RAM_AGC_IF_IACCU_LO__A, 0);
	WR16(devAddr, SCU_RAM_AGC_RF_IACCU_HI__A, 0);
	WR16(devAddr, SCU_RAM_AGC_RF_IACCU_LO__A, 0);
	WR16(devAddr, SCU_RAM_AGC_RF_MAX__A, 32767);
	WR16(devAddr, SCU_RAM_AGC_CLP_SUM_MAX__A, clpSumMax);
	WR16(devAddr, SCU_RAM_AGC_SNS_SUM_MAX__A, snsSumMax);
	WR16(devAddr, SCU_RAM_AGC_KI_INNERGAIN_MIN__A, kiInnergainMin);
	WR16(devAddr, SCU_RAM_AGC_FAST_SNS_CTRL_DELAY__A, 50);
	WR16(devAddr, SCU_RAM_AGC_KI_CYCLEN__A, 500);
	WR16(devAddr, SCU_RAM_AGC_SNS_CYCLEN__A, 500);
	WR16(devAddr, SCU_RAM_AGC_KI_MAXMINGAIN_TH__A, 20);
	WR16(devAddr, SCU_RAM_AGC_KI_MIN__A, kiMin);
	WR16(devAddr, SCU_RAM_AGC_KI_MAX__A, kiMax);
	WR16(devAddr, SCU_RAM_AGC_KI_RED__A, 0);
	WR16(devAddr, SCU_RAM_AGC_CLP_SUM_MIN__A, 8);
	WR16(devAddr, SCU_RAM_AGC_CLP_CYCLEN__A, 500);
	WR16(devAddr, SCU_RAM_AGC_CLP_DIR_TO__A, clpDirTo);
	WR16(devAddr, SCU_RAM_AGC_SNS_SUM_MIN__A, 8);
	WR16(devAddr, SCU_RAM_AGC_SNS_DIR_TO__A, snsDirTo);
	WR16(devAddr, SCU_RAM_AGC_FAST_CLP_CTRL_DELAY__A, 50);
	WR16(devAddr, SCU_RAM_AGC_CLP_CTRL_MODE__A, clpCtrlMode);

	agcRf = 0x800 + pAgcRfSettings->cutOffCurrent;
	if (commonAttr->tunerRfAgcPol == TRUE) {
		agcRf = 0x87ff - agcRf;
	}

	agcIf = 0x800;
	if (commonAttr->tunerIfAgcPol == TRUE) {
		agcRf = 0x87ff - agcRf;
	}

	WR16(devAddr, IQM_AF_AGC_RF__A, agcRf);
	WR16(devAddr, IQM_AF_AGC_IF__A, agcIf);

	/* Set/restore Ki DGAIN factor */
	RR16(devAddr, SCU_RAM_AGC_KI__A, &data);
	data &= ~SCU_RAM_AGC_KI_DGAIN__M;
	data |= (agcKiDgain << SCU_RAM_AGC_KI_DGAIN__B);
	WR16(devAddr, SCU_RAM_AGC_KI__A, data);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t SetFrequency ()
* \brief Set frequency shift.
* \param demod instance of demodulator.
* \param channel pointer to channel data.
* \param tunerFreqOffset residual frequency from tuner.
* \return DRXStatus_t.
*/
static DRXStatus_t
SetFrequency(pDRXDemodInstance_t demod,
	     pDRXChannel_t channel, DRXFrequency_t tunerFreqOffset)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXCommonAttr_t commonAttr = NULL;
	DRXFrequency_t samplingFrequency = 0;
	DRXFrequency_t frequencyShift = 0;
	DRXFrequency_t ifFreqActual = 0;
	DRXFrequency_t rfFreqResidual = 0;
	DRXFrequency_t adcFreq = 0;
	DRXFrequency_t intermediateFreq = 0;
	u32_t iqmFsRateOfs = 0;
	pDRXJData_t extAttr = NULL;
	Bool_t adcFlip = TRUE;
	Bool_t selectPosImage = FALSE;
	Bool_t rfMirror = FALSE;
	Bool_t tunerMirror = TRUE;
	Bool_t imageToSelect = TRUE;
	DRXFrequency_t fmFrequencyShift = 0;

	devAddr = demod->myI2CDevAddr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	rfFreqResidual = -1 * tunerFreqOffset;
	rfMirror = (extAttr->mirror == DRX_MIRROR_YES) ? TRUE : FALSE;
	tunerMirror = demod->myCommonAttr->mirrorFreqSpect ? FALSE : TRUE;
	/*
	   Program frequency shifter
	   No need to account for mirroring on RF
	 */
	switch (extAttr->standard) {
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_C:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_8VSB:
		selectPosImage = TRUE;
		break;
	case DRX_STANDARD_FM:
		/* After IQM FS sound carrier must appear at 4 Mhz in spect.
		   Sound carrier is already 3Mhz above centre frequency due
		   to tuner setting so now add an extra shift of 1MHz... */
		fmFrequencyShift = 1000;
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:
		selectPosImage = FALSE;
		break;
	default:
		return (DRX_STS_INVALID_ARG);
	}
	intermediateFreq = demod->myCommonAttr->intermediateFreq;
	samplingFrequency = demod->myCommonAttr->sysClockFreq / 3;
	if (tunerMirror == TRUE) {
		/* tuner doesn't mirror */
		ifFreqActual =
		    intermediateFreq + rfFreqResidual + fmFrequencyShift;
	} else {
		/* tuner mirrors */
		ifFreqActual =
		    intermediateFreq - rfFreqResidual - fmFrequencyShift;
	}
	if (ifFreqActual > samplingFrequency / 2) {
		/* adc mirrors */
		adcFreq = samplingFrequency - ifFreqActual;
		adcFlip = TRUE;
	} else {
		/* adc doesn't mirror */
		adcFreq = ifFreqActual;
		adcFlip = FALSE;
	}

	frequencyShift = adcFreq;
	imageToSelect =
	    (Bool_t) (rfMirror ^ tunerMirror ^ adcFlip ^ selectPosImage);
	iqmFsRateOfs = Frac28(frequencyShift, samplingFrequency);

	if (imageToSelect)
		iqmFsRateOfs = ~iqmFsRateOfs + 1;

	/* Program frequency shifter with tuner offset compensation */
	/* frequencyShift += tunerFreqOffset; TODO */
	WR32(devAddr, IQM_FS_RATE_OFS_LO__A, iqmFsRateOfs);
	extAttr->iqmFsRateOfs = iqmFsRateOfs;
	extAttr->posImage = (Bool_t) (rfMirror ^ tunerMirror ^ selectPosImage);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t GetSigStrength()
* \brief Retrieve signal strength for VSB and QAM.
* \param demod Pointer to demod instance
* \param u16-t Pointer to signal strength data; range 0, .. , 100.
* \return DRXStatus_t.
* \retval DRX_STS_OK sigStrength contains valid data.
* \retval DRX_STS_INVALID_ARG sigStrength is NULL.
* \retval DRX_STS_ERROR Erroneous data, sigStrength contains invalid data.
*/
#define DRXJ_AGC_TOP    0x2800
#define DRXJ_AGC_SNS    0x1600
#define DRXJ_RFAGC_MAX  0x3fff
#define DRXJ_RFAGC_MIN  0x800

static DRXStatus_t GetSigStrength(pDRXDemodInstance_t demod, pu16_t sigStrength)
{
	u16_t rfGain = 0;
	u16_t ifGain = 0;
	u16_t ifAgcSns = 0;
	u16_t ifAgcTop = 0;
	u16_t rfAgcMax = 0;
	u16_t rfAgcMin = 0;
	pDRXJData_t extAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;

	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = demod->myI2CDevAddr;

	RR16(devAddr, IQM_AF_AGC_IF__A, &ifGain);
	ifGain &= IQM_AF_AGC_IF__M;
	RR16(devAddr, IQM_AF_AGC_RF__A, &rfGain);
	rfGain &= IQM_AF_AGC_RF__M;

	ifAgcSns = DRXJ_AGC_SNS;
	ifAgcTop = DRXJ_AGC_TOP;
	rfAgcMax = DRXJ_RFAGC_MAX;
	rfAgcMin = DRXJ_RFAGC_MIN;

	if (ifGain > ifAgcTop) {
		if (rfGain > rfAgcMax)
			*sigStrength = 100;
		else if (rfGain > rfAgcMin) {
			CHK_ZERO(rfAgcMax - rfAgcMin);
			*sigStrength =
			    75 + 25 * (rfGain - rfAgcMin) / (rfAgcMax -
							     rfAgcMin);
		} else
			*sigStrength = 75;
	} else if (ifGain > ifAgcSns) {
		CHK_ZERO(ifAgcTop - ifAgcSns);
		*sigStrength =
		    20 + 55 * (ifGain - ifAgcSns) / (ifAgcTop - ifAgcSns);
	} else {
		CHK_ZERO(ifAgcSns);
		*sigStrength = (20 * ifGain / ifAgcSns);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t GetAccPktErr()
* \brief Retrieve signal strength for VSB and QAM.
* \param demod Pointer to demod instance
* \param packetErr Pointer to packet error
* \return DRXStatus_t.
* \retval DRX_STS_OK sigStrength contains valid data.
* \retval DRX_STS_INVALID_ARG sigStrength is NULL.
* \retval DRX_STS_ERROR Erroneous data, sigStrength contains invalid data.
*/
#ifdef DRXJ_SIGNAL_ACCUM_ERR
static DRXStatus_t GetAccPktErr(pDRXDemodInstance_t demod, pu16_t packetErr)
{
	static u16_t pktErr = 0;
	static u16_t lastPktErr = 0;
	u16_t data = 0;
	pDRXJData_t extAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;

	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = demod->myI2CDevAddr;

	RR16(devAddr, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, &data);
	if (extAttr->resetPktErrAcc == TRUE) {
		lastPktErr = data;
		pktErr = 0;
		extAttr->resetPktErrAcc = FALSE;
	}

	if (data < lastPktErr) {
		pktErr += 0xffff - lastPktErr;
		pktErr += data;
	} else {
		pktErr += (data - lastPktErr);
	}
	*packetErr = pktErr;
	lastPktErr = data;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}
#endif

/**
* \fn DRXStatus_t ResetAccPktErr()
* \brief Reset Accumulating packet error count.
* \param demod Pointer to demod instance
* \return DRXStatus_t.
* \retval DRX_STS_OK.
* \retval DRX_STS_ERROR Erroneous data.
*/
static DRXStatus_t CtrlSetCfgResetPktErr(pDRXDemodInstance_t demod)
{
#ifdef DRXJ_SIGNAL_ACCUM_ERR
	pDRXJData_t extAttr = NULL;
	u16_t packetError = 0;

	extAttr = (pDRXJData_t) demod->myExtAttr;
	extAttr->resetPktErrAcc = TRUE;
	/* call to reset counter */
	CHK_ERROR(GetAccPktErr(demod, &packetError));

	return (DRX_STS_OK);
rw_error:
#endif
	return (DRX_STS_ERROR);
}

/**
* \fn static short GetSTRFreqOffset()
* \brief Get symbol rate offset in QAM & 8VSB mode
* \return Error code
*/
static DRXStatus_t GetSTRFreqOffset(pDRXDemodInstance_t demod, s32_t * STRFreq)
{
	u32_t symbolFrequencyRatio = 0;
	u32_t symbolNomFrequencyRatio = 0;

	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	standard = extAttr->standard;

	ARR32(devAddr, IQM_RC_RATE_LO__A, &symbolFrequencyRatio);
	symbolNomFrequencyRatio = extAttr->iqmRcRateOfs;

	if (symbolFrequencyRatio > symbolNomFrequencyRatio)
		*STRFreq =
		    -1 *
		    FracTimes1e6((symbolFrequencyRatio -
				  symbolNomFrequencyRatio),
				 (symbolFrequencyRatio + (1 << 23)));
	else
		*STRFreq =
		    FracTimes1e6((symbolNomFrequencyRatio -
				  symbolFrequencyRatio),
				 (symbolFrequencyRatio + (1 << 23)));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn static short GetCTLFreqOffset
* \brief Get the value of CTLFreq in QAM & ATSC mode
* \return Error code
*/
static DRXStatus_t GetCTLFreqOffset(pDRXDemodInstance_t demod, s32_t * CTLFreq)
{
	DRXFrequency_t samplingFrequency = 0;
	s32_t currentFrequency = 0;
	s32_t nominalFrequency = 0;
	s32_t carrierFrequencyShift = 0;
	s32_t sign = 1;
	u32_t data64Hi = 0;
	u32_t data64Lo = 0;
	pDRXJData_t extAttr = NULL;
	pDRXCommonAttr_t commonAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;

	samplingFrequency = commonAttr->sysClockFreq / 3;

	/* both registers are sign extended */
	nominalFrequency = extAttr->iqmFsRateOfs;
	ARR32(devAddr, IQM_FS_RATE_LO__A, (pu32_t) & currentFrequency);

	if (extAttr->posImage == TRUE) {
		/* negative image */
		carrierFrequencyShift = nominalFrequency - currentFrequency;
	} else {
		/* positive image */
		carrierFrequencyShift = currentFrequency - nominalFrequency;
	}

	/* carrier Frequency Shift In Hz */
	if (carrierFrequencyShift < 0) {
		sign = -1;
		carrierFrequencyShift *= sign;
	}

	/* *CTLFreq = carrierFrequencyShift * 50.625e6 / (1 << 28); */
	Mult32(carrierFrequencyShift, samplingFrequency, &data64Hi, &data64Lo);
	*CTLFreq =
	    (s32_t) ((((data64Lo >> 28) & 0xf) | (data64Hi << 4)) * sign);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t SetAgcRf ()
* \brief Configure RF AGC
* \param demod instance of demodulator.
* \param agcSettings AGC configuration structure
* \return DRXStatus_t.
*/
static DRXStatus_t
SetAgcRf(pDRXDemodInstance_t demod, pDRXJCfgAgc_t agcSettings, Bool_t atomic)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	pDRXJCfgAgc_t pAgcSettings = NULL;
	pDRXCommonAttr_t commonAttr = NULL;
	DRXWriteReg16Func_t ScuWr16 = NULL;
	DRXReadReg16Func_t ScuRr16 = NULL;

	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	if (atomic) {
		ScuRr16 = DRXJ_DAP_SCU_AtomicReadReg16;
		ScuWr16 = DRXJ_DAP_SCU_AtomicWriteReg16;
	} else {
		ScuRr16 = DRXJ_DAP.readReg16Func;
		ScuWr16 = DRXJ_DAP.writeReg16Func;
	}

	/* Configure AGC only if standard is currently active */
	if ((extAttr->standard == agcSettings->standard) ||
	    (DRXJ_ISQAMSTD(extAttr->standard) &&
	     DRXJ_ISQAMSTD(agcSettings->standard)) ||
	    (DRXJ_ISATVSTD(extAttr->standard) &&
	     DRXJ_ISATVSTD(agcSettings->standard))) {
		u16_t data = 0;

		switch (agcSettings->ctrlMode) {
		case DRX_AGC_CTRL_AUTO:

			/* Enable RF AGC DAC */
			RR16(devAddr, IQM_AF_STDBY__A, &data);
			data |= IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE;
			WR16(devAddr, IQM_AF_STDBY__A, data);

			/* Enable SCU RF AGC loop */
			CHK_ERROR((*ScuRr16)
				  (devAddr, SCU_RAM_AGC_KI__A, &data, 0));
			data &= ~SCU_RAM_AGC_KI_RF__M;
			if (extAttr->standard == DRX_STANDARD_8VSB) {
				data |= (2 << SCU_RAM_AGC_KI_RF__B);
			} else if (DRXJ_ISQAMSTD(extAttr->standard)) {
				data |= (5 << SCU_RAM_AGC_KI_RF__B);
			} else {
				data |= (4 << SCU_RAM_AGC_KI_RF__B);
			}

			if (commonAttr->tunerRfAgcPol) {
				data |= SCU_RAM_AGC_KI_INV_RF_POL__M;
			} else {
				data &= ~SCU_RAM_AGC_KI_INV_RF_POL__M;
			}
			CHK_ERROR((*ScuWr16)
				  (devAddr, SCU_RAM_AGC_KI__A, data, 0));

			/* Set speed ( using complementary reduction value ) */
			CHK_ERROR((*ScuRr16)
				  (devAddr, SCU_RAM_AGC_KI_RED__A, &data, 0));
			data &= ~SCU_RAM_AGC_KI_RED_RAGC_RED__M;
			CHK_ERROR((*ScuWr16) (devAddr, SCU_RAM_AGC_KI_RED__A,
					      (~
					       (agcSettings->
						speed <<
						SCU_RAM_AGC_KI_RED_RAGC_RED__B)
& SCU_RAM_AGC_KI_RED_RAGC_RED__M)
					      | data, 0));

			if (agcSettings->standard == DRX_STANDARD_8VSB)
				pAgcSettings = &(extAttr->vsbIfAgcCfg);
			else if (DRXJ_ISQAMSTD(agcSettings->standard))
				pAgcSettings = &(extAttr->qamIfAgcCfg);
			else if (DRXJ_ISATVSTD(agcSettings->standard))
				pAgcSettings = &(extAttr->atvIfAgcCfg);
			else
				return (DRX_STS_INVALID_ARG);

			/* Set TOP, only if IF-AGC is in AUTO mode */
			if (pAgcSettings->ctrlMode == DRX_AGC_CTRL_AUTO) {
				CHK_ERROR((*ScuWr16)
					  (devAddr,
					   SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A,
					   agcSettings->top, 0));
				CHK_ERROR((*ScuWr16)
					  (devAddr,
					   SCU_RAM_AGC_IF_IACCU_HI_TGT__A,
					   agcSettings->top, 0));
			}

			/* Cut-Off current */
			CHK_ERROR((*ScuWr16)
				  (devAddr, SCU_RAM_AGC_RF_IACCU_HI_CO__A,
				   agcSettings->cutOffCurrent, 0));
			break;
		case DRX_AGC_CTRL_USER:

			/* Enable RF AGC DAC */
			RR16(devAddr, IQM_AF_STDBY__A, &data);
			data |= IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE;
			WR16(devAddr, IQM_AF_STDBY__A, data);

			/* Disable SCU RF AGC loop */
			CHK_ERROR((*ScuRr16)
				  (devAddr, SCU_RAM_AGC_KI__A, &data, 0));
			data &= ~SCU_RAM_AGC_KI_RF__M;
			if (commonAttr->tunerRfAgcPol) {
				data |= SCU_RAM_AGC_KI_INV_RF_POL__M;
			} else {
				data &= ~SCU_RAM_AGC_KI_INV_RF_POL__M;
			}
			CHK_ERROR((*ScuWr16)
				  (devAddr, SCU_RAM_AGC_KI__A, data, 0));

			/* Write value to output pin */
			CHK_ERROR((*ScuWr16)
				  (devAddr, SCU_RAM_AGC_RF_IACCU_HI__A,
				   agcSettings->outputLevel, 0));
			break;
		case DRX_AGC_CTRL_OFF:

			/* Disable RF AGC DAC */
			RR16(devAddr, IQM_AF_STDBY__A, &data);
			data &= (~IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE);
			WR16(devAddr, IQM_AF_STDBY__A, data);

			/* Disable SCU RF AGC loop */
			CHK_ERROR((*ScuRr16)
				  (devAddr, SCU_RAM_AGC_KI__A, &data, 0));
			data &= ~SCU_RAM_AGC_KI_RF__M;
			CHK_ERROR((*ScuWr16)
				  (devAddr, SCU_RAM_AGC_KI__A, data, 0));
			break;
		default:
			return (DRX_STS_INVALID_ARG);
		}		/* switch ( agcsettings->ctrlMode ) */
	}

	/* Store rf agc settings */
	switch (agcSettings->standard) {
	case DRX_STANDARD_8VSB:
		extAttr->vsbRfAgcCfg = *agcSettings;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		extAttr->qamRfAgcCfg = *agcSettings;
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
		extAttr->atvRfAgcCfg = *agcSettings;
		break;
#endif
	default:
		return (DRX_STS_ERROR);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t GetAgcRf ()
* \brief get configuration of RF AGC
* \param demod instance of demodulator.
* \param agcSettings AGC configuration structure
* \return DRXStatus_t.
*/
static DRXStatus_t
GetAgcRf(pDRXDemodInstance_t demod, pDRXJCfgAgc_t agcSettings)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* Return stored AGC settings */
	standard = agcSettings->standard;
	switch (agcSettings->standard) {
	case DRX_STANDARD_8VSB:
		*agcSettings = extAttr->vsbRfAgcCfg;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		*agcSettings = extAttr->qamRfAgcCfg;
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
		*agcSettings = extAttr->atvRfAgcCfg;
		break;
#endif
	default:
		return (DRX_STS_ERROR);
	}
	agcSettings->standard = standard;

	/* Get AGC output only if standard is currently active. */
	if ((extAttr->standard == agcSettings->standard) ||
	    (DRXJ_ISQAMSTD(extAttr->standard) &&
	     DRXJ_ISQAMSTD(agcSettings->standard)) ||
	    (DRXJ_ISATVSTD(extAttr->standard) &&
	     DRXJ_ISATVSTD(agcSettings->standard))) {
		SARR16(devAddr, SCU_RAM_AGC_RF_IACCU_HI__A,
		       &(agcSettings->outputLevel));
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t SetAgcIf ()
* \brief Configure If AGC
* \param demod instance of demodulator.
* \param agcSettings AGC configuration structure
* \return DRXStatus_t.
*/
static DRXStatus_t
SetAgcIf(pDRXDemodInstance_t demod, pDRXJCfgAgc_t agcSettings, Bool_t atomic)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	pDRXJCfgAgc_t pAgcSettings = NULL;
	pDRXCommonAttr_t commonAttr = NULL;
	DRXWriteReg16Func_t ScuWr16 = NULL;
	DRXReadReg16Func_t ScuRr16 = NULL;

	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	if (atomic) {
		ScuRr16 = DRXJ_DAP_SCU_AtomicReadReg16;
		ScuWr16 = DRXJ_DAP_SCU_AtomicWriteReg16;
	} else {
		ScuRr16 = DRXJ_DAP.readReg16Func;
		ScuWr16 = DRXJ_DAP.writeReg16Func;
	}

	/* Configure AGC only if standard is currently active */
	if ((extAttr->standard == agcSettings->standard) ||
	    (DRXJ_ISQAMSTD(extAttr->standard) &&
	     DRXJ_ISQAMSTD(agcSettings->standard)) ||
	    (DRXJ_ISATVSTD(extAttr->standard) &&
	     DRXJ_ISATVSTD(agcSettings->standard))) {
		u16_t data = 0;

		switch (agcSettings->ctrlMode) {
		case DRX_AGC_CTRL_AUTO:
			/* Enable IF AGC DAC */
			RR16(devAddr, IQM_AF_STDBY__A, &data);
			data |= IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE;
			WR16(devAddr, IQM_AF_STDBY__A, data);

			/* Enable SCU IF AGC loop */
			CHK_ERROR((*ScuRr16)
				  (devAddr, SCU_RAM_AGC_KI__A, &data, 0));
			data &= ~SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			data &= ~SCU_RAM_AGC_KI_IF__M;
			if (extAttr->standard == DRX_STANDARD_8VSB) {
				data |= (3 << SCU_RAM_AGC_KI_IF__B);
			} else if (DRXJ_ISQAMSTD(extAttr->standard)) {
				data |= (6 << SCU_RAM_AGC_KI_IF__B);
			} else {
				data |= (5 << SCU_RAM_AGC_KI_IF__B);
			}

			if (commonAttr->tunerIfAgcPol) {
				data |= SCU_RAM_AGC_KI_INV_IF_POL__M;
			} else {
				data &= ~SCU_RAM_AGC_KI_INV_IF_POL__M;
			}
			CHK_ERROR((*ScuWr16)
				  (devAddr, SCU_RAM_AGC_KI__A, data, 0));

			/* Set speed (using complementary reduction value) */
			CHK_ERROR((*ScuRr16)
				  (devAddr, SCU_RAM_AGC_KI_RED__A, &data, 0));
			data &= ~SCU_RAM_AGC_KI_RED_IAGC_RED__M;
			CHK_ERROR((*ScuWr16) (devAddr, SCU_RAM_AGC_KI_RED__A,
					      (~
					       (agcSettings->
						speed <<
						SCU_RAM_AGC_KI_RED_IAGC_RED__B)
& SCU_RAM_AGC_KI_RED_IAGC_RED__M)
					      | data, 0));

			if (agcSettings->standard == DRX_STANDARD_8VSB)
				pAgcSettings = &(extAttr->vsbRfAgcCfg);
			else if (DRXJ_ISQAMSTD(agcSettings->standard))
				pAgcSettings = &(extAttr->qamRfAgcCfg);
			else if (DRXJ_ISATVSTD(agcSettings->standard))
				pAgcSettings = &(extAttr->atvRfAgcCfg);
			else
				return (DRX_STS_INVALID_ARG);

			/* Restore TOP */
			if (pAgcSettings->ctrlMode == DRX_AGC_CTRL_AUTO) {
				CHK_ERROR((*ScuWr16)
					  (devAddr,
					   SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A,
					   pAgcSettings->top, 0));
				CHK_ERROR((*ScuWr16)
					  (devAddr,
					   SCU_RAM_AGC_IF_IACCU_HI_TGT__A,
					   pAgcSettings->top, 0));
			} else {
				CHK_ERROR((*ScuWr16)
					  (devAddr,
					   SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A,
					   0, 0));
				CHK_ERROR((*ScuWr16)
					  (devAddr,
					   SCU_RAM_AGC_IF_IACCU_HI_TGT__A, 0,
					   0));
			}
			break;

		case DRX_AGC_CTRL_USER:

			/* Enable IF AGC DAC */
			RR16(devAddr, IQM_AF_STDBY__A, &data);
			data |= IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE;
			WR16(devAddr, IQM_AF_STDBY__A, data);

			/* Disable SCU IF AGC loop */
			CHK_ERROR((*ScuRr16)
				  (devAddr, SCU_RAM_AGC_KI__A, &data, 0));
			data &= ~SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			data |= SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			if (commonAttr->tunerIfAgcPol) {
				data |= SCU_RAM_AGC_KI_INV_IF_POL__M;
			} else {
				data &= ~SCU_RAM_AGC_KI_INV_IF_POL__M;
			}
			CHK_ERROR((*ScuWr16)
				  (devAddr, SCU_RAM_AGC_KI__A, data, 0));

			/* Write value to output pin */
			CHK_ERROR((*ScuWr16)
				  (devAddr, SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A,
				   agcSettings->outputLevel, 0));
			break;

		case DRX_AGC_CTRL_OFF:

			/* Disable If AGC DAC */
			RR16(devAddr, IQM_AF_STDBY__A, &data);
			data &= (~IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE);
			WR16(devAddr, IQM_AF_STDBY__A, data);

			/* Disable SCU IF AGC loop */
			CHK_ERROR((*ScuRr16)
				  (devAddr, SCU_RAM_AGC_KI__A, &data, 0));
			data &= ~SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			data |= SCU_RAM_AGC_KI_IF_AGC_DISABLE__M;
			CHK_ERROR((*ScuWr16)
				  (devAddr, SCU_RAM_AGC_KI__A, data, 0));
			break;
		default:
			return (DRX_STS_INVALID_ARG);
		}		/* switch ( agcsettings->ctrlMode ) */

		/* always set the top to support configurations without if-loop */
		CHK_ERROR((*ScuWr16) (devAddr,
				      SCU_RAM_AGC_INGAIN_TGT_MIN__A,
				      agcSettings->top, 0));
	}

	/* Store if agc settings */
	switch (agcSettings->standard) {
	case DRX_STANDARD_8VSB:
		extAttr->vsbIfAgcCfg = *agcSettings;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		extAttr->qamIfAgcCfg = *agcSettings;
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
		extAttr->atvIfAgcCfg = *agcSettings;
		break;
#endif
	default:
		return (DRX_STS_ERROR);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t GetAgcIf ()
* \brief get configuration of If AGC
* \param demod instance of demodulator.
* \param agcSettings AGC configuration structure
* \return DRXStatus_t.
*/
static DRXStatus_t
GetAgcIf(pDRXDemodInstance_t demod, pDRXJCfgAgc_t agcSettings)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* Return stored ATV AGC settings */
	standard = agcSettings->standard;
	switch (agcSettings->standard) {
	case DRX_STANDARD_8VSB:
		*agcSettings = extAttr->vsbIfAgcCfg;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		*agcSettings = extAttr->qamIfAgcCfg;
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
		*agcSettings = extAttr->atvIfAgcCfg;
		break;
#endif
	default:
		return (DRX_STS_ERROR);
	}
	agcSettings->standard = standard;

	/* Get AGC output only if standard is currently active */
	if ((extAttr->standard == agcSettings->standard) ||
	    (DRXJ_ISQAMSTD(extAttr->standard) &&
	     DRXJ_ISQAMSTD(agcSettings->standard)) ||
	    (DRXJ_ISATVSTD(extAttr->standard) &&
	     DRXJ_ISATVSTD(agcSettings->standard))) {
		/* read output level */
		SARR16(devAddr, SCU_RAM_AGC_IF_IACCU_HI__A,
		       &(agcSettings->outputLevel));
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t SetIqmAf ()
* \brief Configure IQM AF registers
* \param demod instance of demodulator.
* \param active
* \return DRXStatus_t.
*/
static DRXStatus_t SetIqmAf(pDRXDemodInstance_t demod, Bool_t active)
{
	u16_t data = 0;
	struct i2c_device_addr *devAddr = NULL;

	devAddr = demod->myI2CDevAddr;

	/* Configure IQM */
	RR16(devAddr, IQM_AF_STDBY__A, &data);
	if (!active) {
		data &= ((~IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE)
			 & (~IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE)
			 & (~IQM_AF_STDBY_STDBY_PD_A2_ACTIVE)
			 & (~IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE)
			 & (~IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE)
		    );
	} else {		/* active */

		data |= (IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE
			 | IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE
			 | IQM_AF_STDBY_STDBY_PD_A2_ACTIVE
			 | IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE
			 | IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE);
	}
	WR16(devAddr, IQM_AF_STDBY__A, data);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
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
* \fn DRXStatus_t PowerDownVSB ()
* \brief Powr down QAM related blocks.
* \param demod instance of demodulator.
* \param channel pointer to channel data.
* \return DRXStatus_t.
*/
static DRXStatus_t PowerDownVSB(pDRXDemodInstance_t demod, Bool_t primary)
{
	struct i2c_device_addr *devAddr = NULL;
	DRXJSCUCmd_t cmdSCU = { /* command     */ 0,
		/* parameterLen */ 0,
		/* resultLen    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	u16_t cmdResult = 0;
	pDRXJData_t extAttr = NULL;
	DRXCfgMPEGOutput_t cfgMPEGOutput;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	/*
	   STOP demodulator
	   reset of FEC and VSB HW
	 */
	cmdSCU.command = SCU_RAM_COMMAND_STANDARD_VSB |
	    SCU_RAM_COMMAND_CMD_DEMOD_STOP;
	cmdSCU.parameterLen = 0;
	cmdSCU.resultLen = 1;
	cmdSCU.parameter = NULL;
	cmdSCU.result = &cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

	/* stop all comm_exec */
	WR16(devAddr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP);
	WR16(devAddr, VSB_COMM_EXEC__A, VSB_COMM_EXEC_STOP);
	if (primary == TRUE) {
		WR16(devAddr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_STOP);
		CHK_ERROR(SetIqmAf(demod, FALSE));
	} else {
		WR16(devAddr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP);
		WR16(devAddr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP);
		WR16(devAddr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP);
		WR16(devAddr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP);
		WR16(devAddr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP);
	}

	cfgMPEGOutput.enableMPEGOutput = FALSE;
	CHK_ERROR(CtrlSetCfgMPEGOutput(demod, &cfgMPEGOutput));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t SetVSBLeakNGain ()
* \brief Set ATSC demod.
* \param demod instance of demodulator.
* \return DRXStatus_t.
*/
static DRXStatus_t SetVSBLeakNGain(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = NULL;

	const u8_t vsb_ffe_leak_gain_ram0[] = {
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

	const u8_t vsb_ffe_leak_gain_ram1[] = {
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

	devAddr = demod->myI2CDevAddr;
	WRB(devAddr, VSB_SYSCTRL_RAM0_FFETRAINLKRATIO1__A,
	    sizeof(vsb_ffe_leak_gain_ram0), ((pu8_t) vsb_ffe_leak_gain_ram0));
	WRB(devAddr, VSB_SYSCTRL_RAM1_FIRRCA1GAIN9__A,
	    sizeof(vsb_ffe_leak_gain_ram1), ((pu8_t) vsb_ffe_leak_gain_ram1));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t SetVSB()
* \brief Set 8VSB demod.
* \param demod instance of demodulator.
* \return DRXStatus_t.
*
*/
static DRXStatus_t SetVSB(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = NULL;
	u16_t cmdResult = 0;
	u16_t cmdParam = 0;
	pDRXCommonAttr_t commonAttr = NULL;
	DRXJSCUCmd_t cmdSCU;
	pDRXJData_t extAttr = NULL;
	const u8_t vsb_taps_re[] = {
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

	devAddr = demod->myI2CDevAddr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* stop all comm_exec */
	WR16(devAddr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP);
	WR16(devAddr, VSB_COMM_EXEC__A, VSB_COMM_EXEC_STOP);
	WR16(devAddr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP);
	WR16(devAddr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP);
	WR16(devAddr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP);
	WR16(devAddr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP);
	WR16(devAddr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP);

	/* reset demodulator */
	cmdSCU.command = SCU_RAM_COMMAND_STANDARD_VSB
	    | SCU_RAM_COMMAND_CMD_DEMOD_RESET;
	cmdSCU.parameterLen = 0;
	cmdSCU.resultLen = 1;
	cmdSCU.parameter = NULL;
	cmdSCU.result = &cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

	WR16(devAddr, IQM_AF_DCF_BYPASS__A, 1);
	WR16(devAddr, IQM_FS_ADJ_SEL__A, IQM_FS_ADJ_SEL_B_VSB);
	WR16(devAddr, IQM_RC_ADJ_SEL__A, IQM_RC_ADJ_SEL_B_VSB);
	extAttr->iqmRcRateOfs = 0x00AD0D79;
	WR32(devAddr, IQM_RC_RATE_OFS_LO__A, extAttr->iqmRcRateOfs);
	WR16(devAddr, VSB_TOP_CFAGC_GAINSHIFT__A, 4);
	WR16(devAddr, VSB_TOP_CYGN1TRK__A, 1);

	WR16(devAddr, IQM_RC_CROUT_ENA__A, 1);
	WR16(devAddr, IQM_RC_STRETCH__A, 28);
	WR16(devAddr, IQM_RT_ACTIVE__A, 0);
	WR16(devAddr, IQM_CF_SYMMETRIC__A, 0);
	WR16(devAddr, IQM_CF_MIDTAP__A, 3);
	WR16(devAddr, IQM_CF_OUT_ENA__A, IQM_CF_OUT_ENA_VSB__M);
	WR16(devAddr, IQM_CF_SCALE__A, 1393);
	WR16(devAddr, IQM_CF_SCALE_SH__A, 0);
	WR16(devAddr, IQM_CF_POW_MEAS_LEN__A, 1);

	WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(vsb_taps_re),
	    ((pu8_t) vsb_taps_re));
	WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(vsb_taps_re),
	    ((pu8_t) vsb_taps_re));

	WR16(devAddr, VSB_TOP_BNTHRESH__A, 330);	/* set higher threshold */
	WR16(devAddr, VSB_TOP_CLPLASTNUM__A, 90);	/* burst detection on   */
	WR16(devAddr, VSB_TOP_SNRTH_RCA1__A, 0x0042);	/* drop thresholds by 1 dB */
	WR16(devAddr, VSB_TOP_SNRTH_RCA2__A, 0x0053);	/* drop thresholds by 2 dB */
	WR16(devAddr, VSB_TOP_EQCTRL__A, 0x1);	/* cma on               */
	WR16(devAddr, SCU_RAM_GPIO__A, 0);	/* GPIO               */

	/* Initialize the FEC Subsystem */
	WR16(devAddr, FEC_TOP_ANNEX__A, FEC_TOP_ANNEX_D);
	{
		u16_t fecOcSncMode = 0;
		RR16(devAddr, FEC_OC_SNC_MODE__A, &fecOcSncMode);
		/* output data even when not locked */
		WR16(devAddr, FEC_OC_SNC_MODE__A,
		     fecOcSncMode | FEC_OC_SNC_MODE_UNLOCK_ENABLE__M);
	}

	/* set clip */
	WR16(devAddr, IQM_AF_CLP_LEN__A, 0);
	WR16(devAddr, IQM_AF_CLP_TH__A, 470);
	WR16(devAddr, IQM_AF_SNS_LEN__A, 0);
	WR16(devAddr, VSB_TOP_SNRTH_PT__A, 0xD4);
	/* no transparent, no A&C framing; parity is set in mpegoutput */
	{
		u16_t fecOcRegMode = 0;
		RR16(devAddr, FEC_OC_MODE__A, &fecOcRegMode);
		WR16(devAddr, FEC_OC_MODE__A, fecOcRegMode &
		     (~(FEC_OC_MODE_TRANSPARENT__M
			| FEC_OC_MODE_CLEAR__M | FEC_OC_MODE_RETAIN_FRAMING__M)
		     ));
	}

	WR16(devAddr, FEC_DI_TIMEOUT_LO__A, 0);	/* timeout counter for restarting */
	WR16(devAddr, FEC_DI_TIMEOUT_HI__A, 3);
	WR16(devAddr, FEC_RS_MODE__A, 0);	/* bypass disabled */
	/* initialize RS packet error measurement parameters */
	WR16(devAddr, FEC_RS_MEASUREMENT_PERIOD__A, FEC_RS_MEASUREMENT_PERIOD);
	WR16(devAddr, FEC_RS_MEASUREMENT_PRESCALE__A,
	     FEC_RS_MEASUREMENT_PRESCALE);

	/* init measurement period of MER/SER */
	WR16(devAddr, VSB_TOP_MEASUREMENT_PERIOD__A,
	     VSB_TOP_MEASUREMENT_PERIOD);
	WR32(devAddr, SCU_RAM_FEC_ACCUM_CW_CORRECTED_LO__A, 0);
	WR16(devAddr, SCU_RAM_FEC_MEAS_COUNT__A, 0);
	WR16(devAddr, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, 0);

	WR16(devAddr, VSB_TOP_CKGN1TRK__A, 128);
	/* B-Input to ADC, PGA+filter in standby */
	if (extAttr->hasLNA == FALSE) {
		WR16(devAddr, IQM_AF_AMUX__A, 0x02);
	};

	/* turn on IQMAF. It has to be in front of setAgc**() */
	CHK_ERROR(SetIqmAf(demod, TRUE));
	CHK_ERROR(ADCSynchronization(demod));

	CHK_ERROR(InitAGC(demod));
	CHK_ERROR(SetAgcIf(demod, &(extAttr->vsbIfAgcCfg), FALSE));
	CHK_ERROR(SetAgcRf(demod, &(extAttr->vsbRfAgcCfg), FALSE));
	{
		/* TODO fix this, store a DRXJCfgAfeGain_t structure in DRXJData_t instead
		   of only the gain */
		DRXJCfgAfeGain_t vsbPgaCfg = { DRX_STANDARD_8VSB, 0 };

		vsbPgaCfg.gain = extAttr->vsbPgaCfg;
		CHK_ERROR(CtrlSetCfgAfeGain(demod, &vsbPgaCfg));
	}
	CHK_ERROR(CtrlSetCfgPreSaw(demod, &(extAttr->vsbPreSawCfg)));

	/* Mpeg output has to be in front of FEC active */
	CHK_ERROR(SetMPEGTEIHandling(demod));
	CHK_ERROR(BitReverseMPEGOutput(demod));
	CHK_ERROR(SetMPEGStartWidth(demod));
	{
		/* TODO: move to setStandard after hardware reset value problem is solved */
		/* Configure initial MPEG output */
		DRXCfgMPEGOutput_t cfgMPEGOutput;
		cfgMPEGOutput.enableMPEGOutput = TRUE;
		cfgMPEGOutput.insertRSByte = commonAttr->mpegCfg.insertRSByte;
		cfgMPEGOutput.enableParallel =
		    commonAttr->mpegCfg.enableParallel;
		cfgMPEGOutput.invertDATA = commonAttr->mpegCfg.invertDATA;
		cfgMPEGOutput.invertERR = commonAttr->mpegCfg.invertERR;
		cfgMPEGOutput.invertSTR = commonAttr->mpegCfg.invertSTR;
		cfgMPEGOutput.invertVAL = commonAttr->mpegCfg.invertVAL;
		cfgMPEGOutput.invertCLK = commonAttr->mpegCfg.invertCLK;
		cfgMPEGOutput.staticCLK = commonAttr->mpegCfg.staticCLK;
		cfgMPEGOutput.bitrate = commonAttr->mpegCfg.bitrate;
		CHK_ERROR(CtrlSetCfgMPEGOutput(demod, &cfgMPEGOutput));
	}

	/* TBD: what parameters should be set */
	cmdParam = 0x00;	/* Default mode AGC on, etc */
	cmdSCU.command = SCU_RAM_COMMAND_STANDARD_VSB
	    | SCU_RAM_COMMAND_CMD_DEMOD_SET_PARAM;
	cmdSCU.parameterLen = 1;
	cmdSCU.resultLen = 1;
	cmdSCU.parameter = &cmdParam;
	cmdSCU.result = &cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

	WR16(devAddr, VSB_TOP_BEAGC_GAINSHIFT__A, 0x0004);
	WR16(devAddr, VSB_TOP_SNRTH_PT__A, 0x00D2);
	WR16(devAddr, VSB_TOP_SYSSMTRNCTRL__A, VSB_TOP_SYSSMTRNCTRL__PRE
	     | VSB_TOP_SYSSMTRNCTRL_NCOTIMEOUTCNTEN__M);
	WR16(devAddr, VSB_TOP_BEDETCTRL__A, 0x142);
	WR16(devAddr, VSB_TOP_LBAGCREFLVL__A, 640);
	WR16(devAddr, VSB_TOP_CYGN1ACQ__A, 4);
	WR16(devAddr, VSB_TOP_CYGN1TRK__A, 2);
	WR16(devAddr, VSB_TOP_CYGN2TRK__A, 3);

	/* start demodulator */
	cmdSCU.command = SCU_RAM_COMMAND_STANDARD_VSB
	    | SCU_RAM_COMMAND_CMD_DEMOD_START;
	cmdSCU.parameterLen = 0;
	cmdSCU.resultLen = 1;
	cmdSCU.parameter = NULL;
	cmdSCU.result = &cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

	WR16(devAddr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_ACTIVE);
	WR16(devAddr, VSB_COMM_EXEC__A, VSB_COMM_EXEC_ACTIVE);
	WR16(devAddr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_ACTIVE);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn static short GetVSBPostRSPckErr(struct i2c_device_addr * devAddr, pu16_t PckErrs)
* \brief Get the values of packet error in 8VSB mode
* \return Error code
*/
static DRXStatus_t GetVSBPostRSPckErr(struct i2c_device_addr *devAddr, pu16_t pckErrs)
{
	u16_t data = 0;
	u16_t period = 0;
	u16_t prescale = 0;
	u16_t packetErrorsMant = 0;
	u16_t packetErrorsExp = 0;

	RR16(devAddr, FEC_RS_NR_FAILURES__A, &data);
	packetErrorsMant = data & FEC_RS_NR_FAILURES_FIXED_MANT__M;
	packetErrorsExp = (data & FEC_RS_NR_FAILURES_EXP__M)
	    >> FEC_RS_NR_FAILURES_EXP__B;
	period = FEC_RS_MEASUREMENT_PERIOD;
	prescale = FEC_RS_MEASUREMENT_PRESCALE;
	/* packet error rate = (error packet number) per second */
	/* 77.3 us is time for per packet */
	CHK_ZERO(period * prescale);
	*pckErrs =
	    (u16_t) FracTimes1e6(packetErrorsMant * (1 << packetErrorsExp),
				 (period * prescale * 77));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn static short GetVSBBer(struct i2c_device_addr * devAddr, pu32_t ber)
* \brief Get the values of ber in VSB mode
* \return Error code
*/
static DRXStatus_t GetVSBpostViterbiBer(struct i2c_device_addr *devAddr, pu32_t ber)
{
	u16_t data = 0;
	u16_t period = 0;
	u16_t prescale = 0;
	u16_t bitErrorsMant = 0;
	u16_t bitErrorsExp = 0;

	RR16(devAddr, FEC_RS_NR_BIT_ERRORS__A, &data);
	period = FEC_RS_MEASUREMENT_PERIOD;
	prescale = FEC_RS_MEASUREMENT_PRESCALE;

	bitErrorsMant = data & FEC_RS_NR_BIT_ERRORS_FIXED_MANT__M;
	bitErrorsExp = (data & FEC_RS_NR_BIT_ERRORS_EXP__M)
	    >> FEC_RS_NR_BIT_ERRORS_EXP__B;

	if (((bitErrorsMant << bitErrorsExp) >> 3) > 68700)
		*ber = 26570;
	else {
		CHK_ZERO(period * prescale);
		*ber =
		    FracTimes1e6(bitErrorsMant <<
				 ((bitErrorsExp >
				   2) ? (bitErrorsExp - 3) : bitErrorsExp),
				 period * prescale * 207 *
				 ((bitErrorsExp > 2) ? 1 : 8));
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn static short GetVSBpreViterbiBer(struct i2c_device_addr * devAddr, pu32_t ber)
* \brief Get the values of ber in VSB mode
* \return Error code
*/
static DRXStatus_t GetVSBpreViterbiBer(struct i2c_device_addr *devAddr, pu32_t ber)
{
	u16_t data = 0;

	RR16(devAddr, VSB_TOP_NR_SYM_ERRS__A, &data);
	*ber =
	    FracTimes1e6(data,
			 VSB_TOP_MEASUREMENT_PERIOD * SYMBOLS_PER_SEGMENT);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn static short GetVSBSymbErr(struct i2c_device_addr * devAddr, pu32_t ber)
* \brief Get the values of ber in VSB mode
* \return Error code
*/
static DRXStatus_t GetVSBSymbErr(struct i2c_device_addr *devAddr, pu32_t ser)
{
	u16_t data = 0;
	u16_t period = 0;
	u16_t prescale = 0;
	u16_t symbErrorsMant = 0;
	u16_t symbErrorsExp = 0;

	RR16(devAddr, FEC_RS_NR_SYMBOL_ERRORS__A, &data);
	period = FEC_RS_MEASUREMENT_PERIOD;
	prescale = FEC_RS_MEASUREMENT_PRESCALE;

	symbErrorsMant = data & FEC_RS_NR_SYMBOL_ERRORS_FIXED_MANT__M;
	symbErrorsExp = (data & FEC_RS_NR_SYMBOL_ERRORS_EXP__M)
	    >> FEC_RS_NR_SYMBOL_ERRORS_EXP__B;

	CHK_ZERO(period * prescale);
	*ser = (u32_t) FracTimes1e6((symbErrorsMant << symbErrorsExp) * 1000,
				    (period * prescale * 77318));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn static DRXStatus_t GetVSBMER(struct i2c_device_addr * devAddr, pu16_t mer)
* \brief Get the values of MER
* \return Error code
*/
static DRXStatus_t GetVSBMER(struct i2c_device_addr *devAddr, pu16_t mer)
{
	u16_t dataHi = 0;

	RR16(devAddr, VSB_TOP_ERR_ENERGY_H__A, &dataHi);
	*mer =
	    (u16_t) (Log10Times100(21504) - Log10Times100((dataHi << 6) / 52));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/
/**
* \fn DRXStatus_t CtrlGetVSBConstel()
* \brief Retreive a VSB constellation point via I2C.
* \param demod Pointer to demodulator instance.
* \param complexNr Pointer to the structure in which to store the
		   constellation point.
* \return DRXStatus_t.
*/
static DRXStatus_t
CtrlGetVSBConstel(pDRXDemodInstance_t demod, pDRXComplex_t complexNr)
{
	struct i2c_device_addr *devAddr = NULL;
				       /**< device address                    */
	u16_t vsbTopCommMb = 0;	       /**< VSB SL MB configuration           */
	u16_t vsbTopCommMbInit = 0;    /**< VSB SL MB intial configuration    */
	u16_t re = 0;		       /**< constellation Re part             */
	u32_t data = 0;

	/* read device info */
	devAddr = demod->myI2CDevAddr;

	/* TODO: */
	/* Monitor bus grabbing is an open external interface issue  */
	/* Needs to be checked when external interface PG is updated */

	/* Configure MB (Monitor bus) */
	RR16(devAddr, VSB_TOP_COMM_MB__A, &vsbTopCommMbInit);
	/* set observe flag & MB mux */
	vsbTopCommMb = (vsbTopCommMbInit |
			VSB_TOP_COMM_MB_OBS_OBS_ON |
			VSB_TOP_COMM_MB_MUX_OBS_VSB_TCMEQ_2);
	WR16(devAddr, VSB_TOP_COMM_MB__A, vsbTopCommMb);

	/* Enable MB grabber in the FEC OC */
	WR16(devAddr, FEC_OC_OCR_MODE__A, FEC_OC_OCR_MODE_GRAB_ENABLE__M);

	/* Disable MB grabber in the FEC OC */
	WR16(devAddr, FEC_OC_OCR_MODE__A, 0x0);

	/* read data */
	RR32(devAddr, FEC_OC_OCR_GRAB_RD1__A, &data);
	re = (u16_t) (((data >> 10) & 0x300) | ((data >> 2) & 0xff));
	if (re & 0x0200) {
		re |= 0xfc00;
	}
	complexNr->re = re;
	complexNr->im = 0;

	/* Restore MB (Monitor bus) */
	WR16(devAddr, VSB_TOP_COMM_MB__A, vsbTopCommMbInit);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
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
* \fn DRXStatus_t PowerDownQAM ()
* \brief Powr down QAM related blocks.
* \param demod instance of demodulator.
* \param channel pointer to channel data.
* \return DRXStatus_t.
*/
static DRXStatus_t PowerDownQAM(pDRXDemodInstance_t demod, Bool_t primary)
{
	DRXJSCUCmd_t cmdSCU = { /* command      */ 0,
		/* parameterLen */ 0,
		/* resultLen    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	u16_t cmdResult = 0;
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	DRXCfgMPEGOutput_t cfgMPEGOutput;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/*
	   STOP demodulator
	   resets IQM, QAM and FEC HW blocks
	 */
	/* stop all comm_exec */
	WR16(devAddr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP);
	WR16(devAddr, QAM_COMM_EXEC__A, QAM_COMM_EXEC_STOP);

	cmdSCU.command = SCU_RAM_COMMAND_STANDARD_QAM |
	    SCU_RAM_COMMAND_CMD_DEMOD_STOP;
	cmdSCU.parameterLen = 0;
	cmdSCU.resultLen = 1;
	cmdSCU.parameter = NULL;
	cmdSCU.result = &cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

	if (primary == TRUE) {
		WR16(devAddr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_STOP);
		CHK_ERROR(SetIqmAf(demod, FALSE));
	} else {
		WR16(devAddr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP);
		WR16(devAddr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP);
		WR16(devAddr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP);
		WR16(devAddr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP);
		WR16(devAddr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP);
	}

	cfgMPEGOutput.enableMPEGOutput = FALSE;
	CHK_ERROR(CtrlSetCfgMPEGOutput(demod, &cfgMPEGOutput));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t SetQAMMeasurement ()
* \brief Setup of the QAM Measuremnt intervals for signal quality
* \param demod instance of demod.
* \param constellation current constellation.
* \return DRXStatus_t.
*
*  NOTE:
*  Take into account that for certain settings the errorcounters can overflow.
*  The implementation does not check this.
*
*  TODO: overriding the extAttr->fecBitsDesired by constellation dependent
*  constants to get a measurement period of approx. 1 sec. Remove fecBitsDesired
*  field ?
*
*/
#ifndef DRXJ_VSB_ONLY
static DRXStatus_t
SetQAMMeasurement(pDRXDemodInstance_t demod,
		  DRXConstellation_t constellation, u32_t symbolRate)
{
	struct i2c_device_addr *devAddr = NULL;	/* device address for I2C writes */
	pDRXJData_t extAttr = NULL;	/* Global data container for DRXJ specif data */
	u32_t fecBitsDesired = 0;	/* BER accounting period */
	u16_t fecRsPlen = 0;	/* defines RS BER measurement period */
	u16_t fecRsPrescale = 0;	/* ReedSolomon Measurement Prescale */
	u32_t fecRsPeriod = 0;	/* Value for corresponding I2C register */
	u32_t fecRsBitCnt = 0;	/* Actual precise amount of bits */
	u32_t fecOcSncFailPeriod = 0;	/* Value for corresponding I2C register */
	u32_t qamVdPeriod = 0;	/* Value for corresponding I2C register */
	u32_t qamVdBitCnt = 0;	/* Actual precise amount of bits */
	u16_t fecVdPlen = 0;	/* no of trellis symbols: VD SER measur period */
	u16_t qamVdPrescale = 0;	/* Viterbi Measurement Prescale */

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	fecBitsDesired = extAttr->fecBitsDesired;
	fecRsPrescale = extAttr->fecRsPrescale;

	switch (constellation) {
	case DRX_CONSTELLATION_QAM16:
		fecBitsDesired = 4 * symbolRate;
		break;
	case DRX_CONSTELLATION_QAM32:
		fecBitsDesired = 5 * symbolRate;
		break;
	case DRX_CONSTELLATION_QAM64:
		fecBitsDesired = 6 * symbolRate;
		break;
	case DRX_CONSTELLATION_QAM128:
		fecBitsDesired = 7 * symbolRate;
		break;
	case DRX_CONSTELLATION_QAM256:
		fecBitsDesired = 8 * symbolRate;
		break;
	default:
		return (DRX_STS_INVALID_ARG);
	}

	/* Parameters for Reed-Solomon Decoder */
	/* fecrs_period = (int)ceil(FEC_BITS_DESIRED/(fecrs_prescale*plen)) */
	/* rs_bit_cnt   = fecrs_period*fecrs_prescale*plen                  */
	/*     result is within 32 bit arithmetic ->                        */
	/*     no need for mult or frac functions                           */

	/* TODO: use constant instead of calculation and remove the fecRsPlen in extAttr */
	switch (extAttr->standard) {
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_C:
		fecRsPlen = 204 * 8;
		break;
	case DRX_STANDARD_ITU_B:
		fecRsPlen = 128 * 7;
		break;
	default:
		return (DRX_STS_INVALID_ARG);
	}

	extAttr->fecRsPlen = fecRsPlen;	/* for getSigQual */
	fecRsBitCnt = fecRsPrescale * fecRsPlen;	/* temp storage   */
	CHK_ZERO(fecRsBitCnt);
	fecRsPeriod = fecBitsDesired / fecRsBitCnt + 1;	/* ceil */
	if (extAttr->standard != DRX_STANDARD_ITU_B)
		fecOcSncFailPeriod = fecRsPeriod;

	/* limit to max 16 bit value (I2C register width) if needed */
	if (fecRsPeriod > 0xFFFF)
		fecRsPeriod = 0xFFFF;

	/* write corresponding registers */
	switch (extAttr->standard) {
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_C:
		break;
	case DRX_STANDARD_ITU_B:
		switch (constellation) {
		case DRX_CONSTELLATION_QAM64:
			fecRsPeriod = 31581;
			fecOcSncFailPeriod = 17932;
			break;
		case DRX_CONSTELLATION_QAM256:
			fecRsPeriod = 45446;
			fecOcSncFailPeriod = 25805;
			break;
		default:
			return (DRX_STS_INVALID_ARG);
		}
		break;
	default:
		return (DRX_STS_INVALID_ARG);
	}

	WR16(devAddr, FEC_OC_SNC_FAIL_PERIOD__A, (u16_t) fecOcSncFailPeriod);
	WR16(devAddr, FEC_RS_MEASUREMENT_PERIOD__A, (u16_t) fecRsPeriod);
	WR16(devAddr, FEC_RS_MEASUREMENT_PRESCALE__A, fecRsPrescale);
	extAttr->fecRsPeriod = (u16_t) fecRsPeriod;
	extAttr->fecRsPrescale = fecRsPrescale;
	WR32(devAddr, SCU_RAM_FEC_ACCUM_CW_CORRECTED_LO__A, 0);
	WR16(devAddr, SCU_RAM_FEC_MEAS_COUNT__A, 0);
	WR16(devAddr, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, 0);

	if (extAttr->standard == DRX_STANDARD_ITU_B) {
		/* Parameters for Viterbi Decoder */
		/* qamvd_period = (int)ceil(FEC_BITS_DESIRED/                      */
		/*                    (qamvd_prescale*plen*(qam_constellation+1))) */
		/* vd_bit_cnt   = qamvd_period*qamvd_prescale*plen                 */
		/*     result is within 32 bit arithmetic ->                       */
		/*     no need for mult or frac functions                          */

		/* a(8 bit) * b(8 bit) = 16 bit result => Mult32 not needed */
		fecVdPlen = extAttr->fecVdPlen;
		qamVdPrescale = extAttr->qamVdPrescale;
		qamVdBitCnt = qamVdPrescale * fecVdPlen;	/* temp storage */

		switch (constellation) {
		case DRX_CONSTELLATION_QAM64:
			/* a(16 bit) * b(4 bit) = 20 bit result => Mult32 not needed */
			qamVdPeriod =
			    qamVdBitCnt * (QAM_TOP_CONSTELLATION_QAM64 + 1)
			    * (QAM_TOP_CONSTELLATION_QAM64 + 1);
			break;
		case DRX_CONSTELLATION_QAM256:
			/* a(16 bit) * b(5 bit) = 21 bit result => Mult32 not needed */
			qamVdPeriod =
			    qamVdBitCnt * (QAM_TOP_CONSTELLATION_QAM256 + 1)
			    * (QAM_TOP_CONSTELLATION_QAM256 + 1);
			break;
		default:
			return (DRX_STS_INVALID_ARG);
		}
		CHK_ZERO(qamVdPeriod);
		qamVdPeriod = fecBitsDesired / qamVdPeriod;
		/* limit to max 16 bit value (I2C register width) if needed */
		if (qamVdPeriod > 0xFFFF)
			qamVdPeriod = 0xFFFF;

		/* a(16 bit) * b(16 bit) = 32 bit result => Mult32 not needed */
		qamVdBitCnt *= qamVdPeriod;

		WR16(devAddr, QAM_VD_MEASUREMENT_PERIOD__A,
		     (u16_t) qamVdPeriod);
		WR16(devAddr, QAM_VD_MEASUREMENT_PRESCALE__A, qamVdPrescale);
		extAttr->qamVdPeriod = (u16_t) qamVdPeriod;
		extAttr->qamVdPrescale = qamVdPrescale;
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t SetQAM16 ()
* \brief QAM16 specific setup
* \param demod instance of demod.
* \return DRXStatus_t.
*/
static DRXStatus_t SetQAM16(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = demod->myI2CDevAddr;
	const u8_t qamDqQualFun[] = {
		DRXJ_16TO8(2),	/* fun0  */
		DRXJ_16TO8(2),	/* fun1  */
		DRXJ_16TO8(2),	/* fun2  */
		DRXJ_16TO8(2),	/* fun3  */
		DRXJ_16TO8(3),	/* fun4  */
		DRXJ_16TO8(3),	/* fun5  */
	};
	const u8_t qamEqCmaRad[] = {
		DRXJ_16TO8(13517),	/* RAD0  */
		DRXJ_16TO8(13517),	/* RAD1  */
		DRXJ_16TO8(13517),	/* RAD2  */
		DRXJ_16TO8(13517),	/* RAD3  */
		DRXJ_16TO8(13517),	/* RAD4  */
		DRXJ_16TO8(13517),	/* RAD5  */
	};

	WRB(devAddr, QAM_DQ_QUAL_FUN0__A, sizeof(qamDqQualFun),
	    ((pu8_t) qamDqQualFun));
	WRB(devAddr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qamEqCmaRad),
	    ((pu8_t) qamEqCmaRad));

	WR16(devAddr, SCU_RAM_QAM_FSM_RTH__A, 140);
	WR16(devAddr, SCU_RAM_QAM_FSM_FTH__A, 50);
	WR16(devAddr, SCU_RAM_QAM_FSM_PTH__A, 120);
	WR16(devAddr, SCU_RAM_QAM_FSM_QTH__A, 230);
	WR16(devAddr, SCU_RAM_QAM_FSM_CTH__A, 95);
	WR16(devAddr, SCU_RAM_QAM_FSM_MTH__A, 105);

	WR16(devAddr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	WR16(devAddr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 56);
	WR16(devAddr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3);

	WR16(devAddr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 16);
	WR16(devAddr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 220);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 25);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 6);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16_t) (-24));
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16_t) (-65));
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16_t) (-127));

	WR16(devAddr, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 20);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_COARSE__A, 255);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 10);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_COARSE__A, 50);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 32);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_COARSE__A, 240);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_COARSE__A, 32);

	WR16(devAddr, SCU_RAM_QAM_SL_SIG_POWER__A, 40960);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t SetQAM32 ()
* \brief QAM32 specific setup
* \param demod instance of demod.
* \return DRXStatus_t.
*/
static DRXStatus_t SetQAM32(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = demod->myI2CDevAddr;
	const u8_t qamDqQualFun[] = {
		DRXJ_16TO8(3),	/* fun0  */
		DRXJ_16TO8(3),	/* fun1  */
		DRXJ_16TO8(3),	/* fun2  */
		DRXJ_16TO8(3),	/* fun3  */
		DRXJ_16TO8(4),	/* fun4  */
		DRXJ_16TO8(4),	/* fun5  */
	};
	const u8_t qamEqCmaRad[] = {
		DRXJ_16TO8(6707),	/* RAD0  */
		DRXJ_16TO8(6707),	/* RAD1  */
		DRXJ_16TO8(6707),	/* RAD2  */
		DRXJ_16TO8(6707),	/* RAD3  */
		DRXJ_16TO8(6707),	/* RAD4  */
		DRXJ_16TO8(6707),	/* RAD5  */
	};

	WRB(devAddr, QAM_DQ_QUAL_FUN0__A, sizeof(qamDqQualFun),
	    ((pu8_t) qamDqQualFun));
	WRB(devAddr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qamEqCmaRad),
	    ((pu8_t) qamEqCmaRad));

	WR16(devAddr, SCU_RAM_QAM_FSM_RTH__A, 90);
	WR16(devAddr, SCU_RAM_QAM_FSM_FTH__A, 50);
	WR16(devAddr, SCU_RAM_QAM_FSM_PTH__A, 100);
	WR16(devAddr, SCU_RAM_QAM_FSM_QTH__A, 170);
	WR16(devAddr, SCU_RAM_QAM_FSM_CTH__A, 80);
	WR16(devAddr, SCU_RAM_QAM_FSM_MTH__A, 100);

	WR16(devAddr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	WR16(devAddr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 56);
	WR16(devAddr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3);

	WR16(devAddr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 12);
	WR16(devAddr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 140);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, (u16_t) (-8));
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, (u16_t) (-16));
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16_t) (-26));
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16_t) (-56));
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16_t) (-86));

	WR16(devAddr, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 20);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_COARSE__A, 255);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 10);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_COARSE__A, 50);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 32);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_COARSE__A, 176);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_COARSE__A, 8);

	WR16(devAddr, SCU_RAM_QAM_SL_SIG_POWER__A, 20480);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t SetQAM64 ()
* \brief QAM64 specific setup
* \param demod instance of demod.
* \return DRXStatus_t.
*/
static DRXStatus_t SetQAM64(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = demod->myI2CDevAddr;
	const u8_t qamDqQualFun[] = {	/* this is hw reset value. no necessary to re-write */
		DRXJ_16TO8(4),	/* fun0  */
		DRXJ_16TO8(4),	/* fun1  */
		DRXJ_16TO8(4),	/* fun2  */
		DRXJ_16TO8(4),	/* fun3  */
		DRXJ_16TO8(6),	/* fun4  */
		DRXJ_16TO8(6),	/* fun5  */
	};
	const u8_t qamEqCmaRad[] = {
		DRXJ_16TO8(13336),	/* RAD0  */
		DRXJ_16TO8(12618),	/* RAD1  */
		DRXJ_16TO8(11988),	/* RAD2  */
		DRXJ_16TO8(13809),	/* RAD3  */
		DRXJ_16TO8(13809),	/* RAD4  */
		DRXJ_16TO8(15609),	/* RAD5  */
	};

	WRB(devAddr, QAM_DQ_QUAL_FUN0__A, sizeof(qamDqQualFun),
	    ((pu8_t) qamDqQualFun));
	WRB(devAddr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qamEqCmaRad),
	    ((pu8_t) qamEqCmaRad));

	WR16(devAddr, SCU_RAM_QAM_FSM_RTH__A, 105);
	WR16(devAddr, SCU_RAM_QAM_FSM_FTH__A, 60);
	WR16(devAddr, SCU_RAM_QAM_FSM_PTH__A, 100);
	WR16(devAddr, SCU_RAM_QAM_FSM_QTH__A, 195);
	WR16(devAddr, SCU_RAM_QAM_FSM_CTH__A, 80);
	WR16(devAddr, SCU_RAM_QAM_FSM_MTH__A, 84);

	WR16(devAddr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	WR16(devAddr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 32);
	WR16(devAddr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3);

	WR16(devAddr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 12);
	WR16(devAddr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 141);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 7);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 0);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16_t) (-15));
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16_t) (-45));
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16_t) (-80));

	WR16(devAddr, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 30);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_COARSE__A, 255);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_COARSE__A, 80);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 48);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_COARSE__A, 160);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_COARSE__A, 32);

	WR16(devAddr, SCU_RAM_QAM_SL_SIG_POWER__A, 43008);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t SetQAM128 ()
* \brief QAM128 specific setup
* \param demod: instance of demod.
* \return DRXStatus_t.
*/
static DRXStatus_t SetQAM128(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = demod->myI2CDevAddr;
	const u8_t qamDqQualFun[] = {
		DRXJ_16TO8(6),	/* fun0  */
		DRXJ_16TO8(6),	/* fun1  */
		DRXJ_16TO8(6),	/* fun2  */
		DRXJ_16TO8(6),	/* fun3  */
		DRXJ_16TO8(9),	/* fun4  */
		DRXJ_16TO8(9),	/* fun5  */
	};
	const u8_t qamEqCmaRad[] = {
		DRXJ_16TO8(6164),	/* RAD0  */
		DRXJ_16TO8(6598),	/* RAD1  */
		DRXJ_16TO8(6394),	/* RAD2  */
		DRXJ_16TO8(6409),	/* RAD3  */
		DRXJ_16TO8(6656),	/* RAD4  */
		DRXJ_16TO8(7238),	/* RAD5  */
	};

	WRB(devAddr, QAM_DQ_QUAL_FUN0__A, sizeof(qamDqQualFun),
	    ((pu8_t) qamDqQualFun));
	WRB(devAddr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qamEqCmaRad),
	    ((pu8_t) qamEqCmaRad));

	WR16(devAddr, SCU_RAM_QAM_FSM_RTH__A, 50);
	WR16(devAddr, SCU_RAM_QAM_FSM_FTH__A, 60);
	WR16(devAddr, SCU_RAM_QAM_FSM_PTH__A, 100);
	WR16(devAddr, SCU_RAM_QAM_FSM_QTH__A, 140);
	WR16(devAddr, SCU_RAM_QAM_FSM_CTH__A, 80);
	WR16(devAddr, SCU_RAM_QAM_FSM_MTH__A, 100);

	WR16(devAddr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	WR16(devAddr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 32);
	WR16(devAddr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3);

	WR16(devAddr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 8);
	WR16(devAddr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 65);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 5);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 3);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16_t) (-1));
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, 12);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16_t) (-23));

	WR16(devAddr, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 40);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_COARSE__A, 255);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 20);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_COARSE__A, 80);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 32);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_COARSE__A, 144);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_COARSE__A, 16);

	WR16(devAddr, SCU_RAM_QAM_SL_SIG_POWER__A, 20992);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t SetQAM256 ()
* \brief QAM256 specific setup
* \param demod: instance of demod.
* \return DRXStatus_t.
*/
static DRXStatus_t SetQAM256(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = demod->myI2CDevAddr;
	const u8_t qamDqQualFun[] = {
		DRXJ_16TO8(8),	/* fun0  */
		DRXJ_16TO8(8),	/* fun1  */
		DRXJ_16TO8(8),	/* fun2  */
		DRXJ_16TO8(8),	/* fun3  */
		DRXJ_16TO8(12),	/* fun4  */
		DRXJ_16TO8(12),	/* fun5  */
	};
	const u8_t qamEqCmaRad[] = {
		DRXJ_16TO8(12345),	/* RAD0  */
		DRXJ_16TO8(12345),	/* RAD1  */
		DRXJ_16TO8(13626),	/* RAD2  */
		DRXJ_16TO8(12931),	/* RAD3  */
		DRXJ_16TO8(14719),	/* RAD4  */
		DRXJ_16TO8(15356),	/* RAD5  */
	};

	WRB(devAddr, QAM_DQ_QUAL_FUN0__A, sizeof(qamDqQualFun),
	    ((pu8_t) qamDqQualFun));
	WRB(devAddr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qamEqCmaRad),
	    ((pu8_t) qamEqCmaRad));

	WR16(devAddr, SCU_RAM_QAM_FSM_RTH__A, 50);
	WR16(devAddr, SCU_RAM_QAM_FSM_FTH__A, 60);
	WR16(devAddr, SCU_RAM_QAM_FSM_PTH__A, 100);
	WR16(devAddr, SCU_RAM_QAM_FSM_QTH__A, 150);
	WR16(devAddr, SCU_RAM_QAM_FSM_CTH__A, 80);
	WR16(devAddr, SCU_RAM_QAM_FSM_MTH__A, 110);

	WR16(devAddr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	WR16(devAddr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 16);
	WR16(devAddr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3);

	WR16(devAddr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 8);
	WR16(devAddr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 74);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 18);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 13);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, 7);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, 0);
	WR16(devAddr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16_t) (-8));

	WR16(devAddr, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 50);
	WR16(devAddr, SCU_RAM_QAM_LC_CP_COARSE__A, 255);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_FINE__A, 2);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 25);
	WR16(devAddr, SCU_RAM_QAM_LC_CI_COARSE__A, 80);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 48);
	WR16(devAddr, SCU_RAM_QAM_LC_CF_COARSE__A, 80);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15);
	WR16(devAddr, SCU_RAM_QAM_LC_CF1_COARSE__A, 16);

	WR16(devAddr, SCU_RAM_QAM_SL_SIG_POWER__A, 43520);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/
#define QAM_SET_OP_ALL 0x1
#define QAM_SET_OP_CONSTELLATION 0x2
#define QAM_SET_OP_SPECTRUM 0X4

/**
* \fn DRXStatus_t SetQAM ()
* \brief Set QAM demod.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \return DRXStatus_t.
*/
static DRXStatus_t
SetQAM(pDRXDemodInstance_t demod,
       pDRXChannel_t channel, DRXFrequency_t tunerFreqOffset, u32_t op)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	pDRXCommonAttr_t commonAttr = NULL;
	u16_t cmdResult = 0;
	u32_t adcFrequency = 0;
	u32_t iqmRcRate = 0;
	u16_t lcSymbolFreq = 0;
	u16_t iqmRcStretch = 0;
	u16_t setEnvParameters = 0;
	u16_t setParamParameters[2] = { 0 };
	DRXJSCUCmd_t cmdSCU = { /* command      */ 0,
		/* parameterLen */ 0,
		/* resultLen    */ 0,
		/* parameter    */ NULL,
		/* result       */ NULL
	};
	const u8_t qamA_taps[] = {
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
	const u8_t qamB64_taps[] = {
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
	const u8_t qamB256_taps[] = {
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
	const u8_t qamC_taps[] = {
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

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {
		if (extAttr->standard == DRX_STANDARD_ITU_B) {
			switch (channel->constellation) {
			case DRX_CONSTELLATION_QAM256:
				iqmRcRate = 0x00AE3562;
				lcSymbolFreq =
				    QAM_LC_SYMBOL_FREQ_FREQ_QAM_B_256;
				channel->symbolrate = 5360537;
				iqmRcStretch = IQM_RC_STRETCH_QAM_B_256;
				break;
			case DRX_CONSTELLATION_QAM64:
				iqmRcRate = 0x00C05A0E;
				lcSymbolFreq = 409;
				channel->symbolrate = 5056941;
				iqmRcStretch = IQM_RC_STRETCH_QAM_B_64;
				break;
			default:
				return (DRX_STS_INVALID_ARG);
			}
		} else {
			adcFrequency = (commonAttr->sysClockFreq * 1000) / 3;
			CHK_ZERO(channel->symbolrate);
			iqmRcRate =
			    (adcFrequency / channel->symbolrate) * (1 << 21) +
			    (Frac28
			     ((adcFrequency % channel->symbolrate),
			      channel->symbolrate) >> 7) - (1 << 23);
			lcSymbolFreq =
			    (u16_t) (Frac28
				     (channel->symbolrate +
				      (adcFrequency >> 13),
				      adcFrequency) >> 16);
			if (lcSymbolFreq > 511)
				lcSymbolFreq = 511;

			iqmRcStretch = 21;
		}

		if (extAttr->standard == DRX_STANDARD_ITU_A) {
			setEnvParameters = QAM_TOP_ANNEX_A;	/* annex             */
			setParamParameters[0] = channel->constellation;	/* constellation     */
			setParamParameters[1] = DRX_INTERLEAVEMODE_I12_J17;	/* interleave mode   */
		} else if (extAttr->standard == DRX_STANDARD_ITU_B) {
			setEnvParameters = QAM_TOP_ANNEX_B;	/* annex             */
			setParamParameters[0] = channel->constellation;	/* constellation     */
			setParamParameters[1] = channel->interleavemode;	/* interleave mode   */
		} else if (extAttr->standard == DRX_STANDARD_ITU_C) {
			setEnvParameters = QAM_TOP_ANNEX_C;	/* annex             */
			setParamParameters[0] = channel->constellation;	/* constellation     */
			setParamParameters[1] = DRX_INTERLEAVEMODE_I12_J17;	/* interleave mode   */
		} else {
			return (DRX_STS_INVALID_ARG);
		}
	}

	if (op & QAM_SET_OP_ALL) {
		/*
		   STEP 1: reset demodulator
		   resets IQM, QAM and FEC HW blocks
		   resets SCU variables
		 */
		/* stop all comm_exec */
		WR16(devAddr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP);
		WR16(devAddr, QAM_COMM_EXEC__A, QAM_COMM_EXEC_STOP);
		WR16(devAddr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP);
		WR16(devAddr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP);
		WR16(devAddr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP);
		WR16(devAddr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP);
		WR16(devAddr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP);

		cmdSCU.command = SCU_RAM_COMMAND_STANDARD_QAM |
		    SCU_RAM_COMMAND_CMD_DEMOD_RESET;
		cmdSCU.parameterLen = 0;
		cmdSCU.resultLen = 1;
		cmdSCU.parameter = NULL;
		cmdSCU.result = &cmdResult;
		CHK_ERROR(SCUCommand(devAddr, &cmdSCU));
	}

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {
		/*
		   STEP 2: configure demodulator
		   -set env
		   -set params (resets IQM,QAM,FEC HW; initializes some SCU variables )
		 */
		cmdSCU.command = SCU_RAM_COMMAND_STANDARD_QAM |
		    SCU_RAM_COMMAND_CMD_DEMOD_SET_ENV;
		cmdSCU.parameterLen = 1;
		cmdSCU.resultLen = 1;
		cmdSCU.parameter = &setEnvParameters;
		cmdSCU.result = &cmdResult;
		CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

		cmdSCU.command = SCU_RAM_COMMAND_STANDARD_QAM |
		    SCU_RAM_COMMAND_CMD_DEMOD_SET_PARAM;
		cmdSCU.parameterLen = 2;
		cmdSCU.resultLen = 1;
		cmdSCU.parameter = setParamParameters;
		cmdSCU.result = &cmdResult;
		CHK_ERROR(SCUCommand(devAddr, &cmdSCU));
		/* set symbol rate */
		WR32(devAddr, IQM_RC_RATE_OFS_LO__A, iqmRcRate);
		extAttr->iqmRcRateOfs = iqmRcRate;
		CHK_ERROR(SetQAMMeasurement
			  (demod, channel->constellation, channel->symbolrate));
	}
	/* STEP 3: enable the system in a mode where the ADC provides valid signal
	   setup constellation independent registers */
	/* from qam_cmd.py script (qam_driver_b) */
	/* TODO: remove re-writes of HW reset values */
	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_SPECTRUM)) {
		CHK_ERROR(SetFrequency(demod, channel, tunerFreqOffset));
	}

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {

		WR16(devAddr, QAM_LC_SYMBOL_FREQ__A, lcSymbolFreq);
		WR16(devAddr, IQM_RC_STRETCH__A, iqmRcStretch);
	}

	if (op & QAM_SET_OP_ALL) {
		if (extAttr->hasLNA == FALSE) {
			WR16(devAddr, IQM_AF_AMUX__A, 0x02);
		}
		WR16(devAddr, IQM_CF_SYMMETRIC__A, 0);
		WR16(devAddr, IQM_CF_MIDTAP__A, 3);
		WR16(devAddr, IQM_CF_OUT_ENA__A, IQM_CF_OUT_ENA_QAM__M);

		WR16(devAddr, SCU_RAM_QAM_WR_RSV_0__A, 0x5f);	/* scu temporary shut down agc */

		WR16(devAddr, IQM_AF_SYNC_SEL__A, 3);
		WR16(devAddr, IQM_AF_CLP_LEN__A, 0);
		WR16(devAddr, IQM_AF_CLP_TH__A, 448);
		WR16(devAddr, IQM_AF_SNS_LEN__A, 0);
		WR16(devAddr, IQM_AF_PDREF__A, 4);
		WR16(devAddr, IQM_AF_STDBY__A, 0x10);
		WR16(devAddr, IQM_AF_PGA_GAIN__A, 11);

		WR16(devAddr, IQM_CF_POW_MEAS_LEN__A, 1);
		WR16(devAddr, IQM_CF_SCALE_SH__A, IQM_CF_SCALE_SH__PRE);	/*! reset default val ! */

		WR16(devAddr, QAM_SY_TIMEOUT__A, QAM_SY_TIMEOUT__PRE);	/*! reset default val ! */
		if (extAttr->standard == DRX_STANDARD_ITU_B) {
			WR16(devAddr, QAM_SY_SYNC_LWM__A, QAM_SY_SYNC_LWM__PRE);	/*! reset default val ! */
			WR16(devAddr, QAM_SY_SYNC_AWM__A, QAM_SY_SYNC_AWM__PRE);	/*! reset default val ! */
			WR16(devAddr, QAM_SY_SYNC_HWM__A, QAM_SY_SYNC_HWM__PRE);	/*! reset default val ! */
		} else {
			switch (channel->constellation) {
			case DRX_CONSTELLATION_QAM16:
			case DRX_CONSTELLATION_QAM64:
			case DRX_CONSTELLATION_QAM256:
				WR16(devAddr, QAM_SY_SYNC_LWM__A, 0x03);
				WR16(devAddr, QAM_SY_SYNC_AWM__A, 0x04);
				WR16(devAddr, QAM_SY_SYNC_HWM__A, QAM_SY_SYNC_HWM__PRE);	/*! reset default val ! */
				break;
			case DRX_CONSTELLATION_QAM32:
			case DRX_CONSTELLATION_QAM128:
				WR16(devAddr, QAM_SY_SYNC_LWM__A, 0x03);
				WR16(devAddr, QAM_SY_SYNC_AWM__A, 0x05);
				WR16(devAddr, QAM_SY_SYNC_HWM__A, 0x06);
				break;
			default:
				return (DRX_STS_ERROR);
			}	/* switch */
		}

		WR16(devAddr, QAM_LC_MODE__A, QAM_LC_MODE__PRE);	/*! reset default val ! */
		WR16(devAddr, QAM_LC_RATE_LIMIT__A, 3);
		WR16(devAddr, QAM_LC_LPF_FACTORP__A, 4);
		WR16(devAddr, QAM_LC_LPF_FACTORI__A, 4);
		WR16(devAddr, QAM_LC_MODE__A, 7);
		WR16(devAddr, QAM_LC_QUAL_TAB0__A, 1);
		WR16(devAddr, QAM_LC_QUAL_TAB1__A, 1);
		WR16(devAddr, QAM_LC_QUAL_TAB2__A, 1);
		WR16(devAddr, QAM_LC_QUAL_TAB3__A, 1);
		WR16(devAddr, QAM_LC_QUAL_TAB4__A, 2);
		WR16(devAddr, QAM_LC_QUAL_TAB5__A, 2);
		WR16(devAddr, QAM_LC_QUAL_TAB6__A, 2);
		WR16(devAddr, QAM_LC_QUAL_TAB8__A, 2);
		WR16(devAddr, QAM_LC_QUAL_TAB9__A, 2);
		WR16(devAddr, QAM_LC_QUAL_TAB10__A, 2);
		WR16(devAddr, QAM_LC_QUAL_TAB12__A, 2);
		WR16(devAddr, QAM_LC_QUAL_TAB15__A, 3);
		WR16(devAddr, QAM_LC_QUAL_TAB16__A, 3);
		WR16(devAddr, QAM_LC_QUAL_TAB20__A, 4);
		WR16(devAddr, QAM_LC_QUAL_TAB25__A, 4);

		WR16(devAddr, IQM_FS_ADJ_SEL__A, 1);
		WR16(devAddr, IQM_RC_ADJ_SEL__A, 1);
		WR16(devAddr, IQM_CF_ADJ_SEL__A, 1);
		WR16(devAddr, IQM_CF_POW_MEAS_LEN__A, 0);
		WR16(devAddr, SCU_RAM_GPIO__A, 0);

		/* No more resets of the IQM, current standard correctly set =>
		   now AGCs can be configured. */
		/* turn on IQMAF. It has to be in front of setAgc**() */
		CHK_ERROR(SetIqmAf(demod, TRUE));
		CHK_ERROR(ADCSynchronization(demod));

		CHK_ERROR(InitAGC(demod));
		CHK_ERROR(SetAgcIf(demod, &(extAttr->qamIfAgcCfg), FALSE));
		CHK_ERROR(SetAgcRf(demod, &(extAttr->qamRfAgcCfg), FALSE));
		{
			/* TODO fix this, store a DRXJCfgAfeGain_t structure in DRXJData_t instead
			   of only the gain */
			DRXJCfgAfeGain_t qamPgaCfg = { DRX_STANDARD_ITU_B, 0 };

			qamPgaCfg.gain = extAttr->qamPgaCfg;
			CHK_ERROR(CtrlSetCfgAfeGain(demod, &qamPgaCfg));
		}
		CHK_ERROR(CtrlSetCfgPreSaw(demod, &(extAttr->qamPreSawCfg)));
	}

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {
		if (extAttr->standard == DRX_STANDARD_ITU_A) {
			WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(qamA_taps),
			    ((pu8_t) qamA_taps));
			WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(qamA_taps),
			    ((pu8_t) qamA_taps));
		} else if (extAttr->standard == DRX_STANDARD_ITU_B) {
			switch (channel->constellation) {
			case DRX_CONSTELLATION_QAM64:
				WRB(devAddr, IQM_CF_TAP_RE0__A,
				    sizeof(qamB64_taps), ((pu8_t) qamB64_taps));
				WRB(devAddr, IQM_CF_TAP_IM0__A,
				    sizeof(qamB64_taps), ((pu8_t) qamB64_taps));
				break;
			case DRX_CONSTELLATION_QAM256:
				WRB(devAddr, IQM_CF_TAP_RE0__A,
				    sizeof(qamB256_taps),
				    ((pu8_t) qamB256_taps));
				WRB(devAddr, IQM_CF_TAP_IM0__A,
				    sizeof(qamB256_taps),
				    ((pu8_t) qamB256_taps));
				break;
			default:
				return (DRX_STS_ERROR);
			}
		} else if (extAttr->standard == DRX_STANDARD_ITU_C) {
			WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(qamC_taps),
			    ((pu8_t) qamC_taps));
			WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(qamC_taps),
			    ((pu8_t) qamC_taps));
		}

		/* SETP 4: constellation specific setup */
		switch (channel->constellation) {
		case DRX_CONSTELLATION_QAM16:
			CHK_ERROR(SetQAM16(demod));
			break;
		case DRX_CONSTELLATION_QAM32:
			CHK_ERROR(SetQAM32(demod));
			break;
		case DRX_CONSTELLATION_QAM64:
			CHK_ERROR(SetQAM64(demod));
			break;
		case DRX_CONSTELLATION_QAM128:
			CHK_ERROR(SetQAM128(demod));
			break;
		case DRX_CONSTELLATION_QAM256:
			CHK_ERROR(SetQAM256(demod));
			break;
		default:
			return (DRX_STS_ERROR);
		}		/* switch */
	}

	if ((op & QAM_SET_OP_ALL)) {
		WR16(devAddr, IQM_CF_SCALE_SH__A, 0);

		/* Mpeg output has to be in front of FEC active */
		CHK_ERROR(SetMPEGTEIHandling(demod));
		CHK_ERROR(BitReverseMPEGOutput(demod));
		CHK_ERROR(SetMPEGStartWidth(demod));
		{
			/* TODO: move to setStandard after hardware reset value problem is solved */
			/* Configure initial MPEG output */
			DRXCfgMPEGOutput_t cfgMPEGOutput;

			cfgMPEGOutput.enableMPEGOutput = TRUE;
			cfgMPEGOutput.insertRSByte =
			    commonAttr->mpegCfg.insertRSByte;
			cfgMPEGOutput.enableParallel =
			    commonAttr->mpegCfg.enableParallel;
			cfgMPEGOutput.invertDATA =
			    commonAttr->mpegCfg.invertDATA;
			cfgMPEGOutput.invertERR = commonAttr->mpegCfg.invertERR;
			cfgMPEGOutput.invertSTR = commonAttr->mpegCfg.invertSTR;
			cfgMPEGOutput.invertVAL = commonAttr->mpegCfg.invertVAL;
			cfgMPEGOutput.invertCLK = commonAttr->mpegCfg.invertCLK;
			cfgMPEGOutput.staticCLK = commonAttr->mpegCfg.staticCLK;
			cfgMPEGOutput.bitrate = commonAttr->mpegCfg.bitrate;
			CHK_ERROR(CtrlSetCfgMPEGOutput(demod, &cfgMPEGOutput));
		}
	}

	if ((op & QAM_SET_OP_ALL) || (op & QAM_SET_OP_CONSTELLATION)) {

		/* STEP 5: start QAM demodulator (starts FEC, QAM and IQM HW) */
		cmdSCU.command = SCU_RAM_COMMAND_STANDARD_QAM |
		    SCU_RAM_COMMAND_CMD_DEMOD_START;
		cmdSCU.parameterLen = 0;
		cmdSCU.resultLen = 1;
		cmdSCU.parameter = NULL;
		cmdSCU.result = &cmdResult;
		CHK_ERROR(SCUCommand(devAddr, &cmdSCU));
	}

	WR16(devAddr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_ACTIVE);
	WR16(devAddr, QAM_COMM_EXEC__A, QAM_COMM_EXEC_ACTIVE);
	WR16(devAddr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_ACTIVE);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/
static DRXStatus_t
CtrlGetQAMSigQuality(pDRXDemodInstance_t demod, pDRXSigQuality_t sigQuality);
static DRXStatus_t qamFlipSpec(pDRXDemodInstance_t demod, pDRXChannel_t channel)
{
	u32_t iqmFsRateOfs = 0;
	u32_t iqmFsRateLo = 0;
	u16_t qamCtlEna = 0;
	u16_t data = 0;
	u16_t equMode = 0;
	u16_t fsmState = 0;
	int i = 0;
	int ofsofs = 0;
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* Silence the controlling of lc, equ, and the acquisition state machine */
	RR16(devAddr, SCU_RAM_QAM_CTL_ENA__A, &qamCtlEna);
	WR16(devAddr, SCU_RAM_QAM_CTL_ENA__A, qamCtlEna
	     & ~(SCU_RAM_QAM_CTL_ENA_ACQ__M
		 | SCU_RAM_QAM_CTL_ENA_EQU__M | SCU_RAM_QAM_CTL_ENA_LC__M));

	/* freeze the frequency control loop */
	WR16(devAddr, QAM_LC_CF__A, 0);
	WR16(devAddr, QAM_LC_CF1__A, 0);

	ARR32(devAddr, IQM_FS_RATE_OFS_LO__A, &iqmFsRateOfs);
	ARR32(devAddr, IQM_FS_RATE_LO__A, &iqmFsRateLo);
	ofsofs = iqmFsRateLo - iqmFsRateOfs;
	iqmFsRateOfs = ~iqmFsRateOfs + 1;
	iqmFsRateOfs -= 2 * ofsofs;

	/* freeze dq/fq updating */
	RR16(devAddr, QAM_DQ_MODE__A, &data);
	data = (data & 0xfff9);
	WR16(devAddr, QAM_DQ_MODE__A, data);
	WR16(devAddr, QAM_FQ_MODE__A, data);

	/* lc_cp / _ci / _ca */
	WR16(devAddr, QAM_LC_CI__A, 0);
	WR16(devAddr, QAM_LC_EP__A, 0);
	WR16(devAddr, QAM_FQ_LA_FACTOR__A, 0);

	/* flip the spec */
	WR32(devAddr, IQM_FS_RATE_OFS_LO__A, iqmFsRateOfs);
	extAttr->iqmFsRateOfs = iqmFsRateOfs;
	extAttr->posImage = (extAttr->posImage) ? FALSE : TRUE;

	/* freeze dq/fq updating */
	RR16(devAddr, QAM_DQ_MODE__A, &data);
	equMode = data;
	data = (data & 0xfff9);
	WR16(devAddr, QAM_DQ_MODE__A, data);
	WR16(devAddr, QAM_FQ_MODE__A, data);

	for (i = 0; i < 28; i++) {
		RR16(devAddr, QAM_DQ_TAP_IM_EL0__A + (2 * i), &data);
		WR16(devAddr, QAM_DQ_TAP_IM_EL0__A + (2 * i), -data);
	}

	for (i = 0; i < 24; i++) {
		RR16(devAddr, QAM_FQ_TAP_IM_EL0__A + (2 * i), &data);
		WR16(devAddr, QAM_FQ_TAP_IM_EL0__A + (2 * i), -data);
	}

	data = equMode;
	WR16(devAddr, QAM_DQ_MODE__A, data);
	WR16(devAddr, QAM_FQ_MODE__A, data);

	WR16(devAddr, SCU_RAM_QAM_FSM_STATE_TGT__A, 4);

	i = 0;
	while ((fsmState != 4) && (i++ < 100)) {
		RR16(devAddr, SCU_RAM_QAM_FSM_STATE__A, &fsmState);
	}
	WR16(devAddr, SCU_RAM_QAM_CTL_ENA__A, (qamCtlEna | 0x0016));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);

}

#define  NO_LOCK        0x0
#define  DEMOD_LOCKED   0x1
#define  SYNC_FLIPPED   0x2
#define  SPEC_MIRRORED  0x4
/**
* \fn DRXStatus_t QAM64Auto ()
* \brief auto do sync pattern switching and mirroring.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \param tunerFreqOffset: tuner frequency offset.
* \param lockStatus: pointer to lock status.
* \return DRXStatus_t.
*/
static DRXStatus_t
QAM64Auto(pDRXDemodInstance_t demod,
	  pDRXChannel_t channel,
	  DRXFrequency_t tunerFreqOffset, pDRXLockStatus_t lockStatus)
{
	DRXSigQuality_t sigQuality;
	u16_t data = 0;
	u32_t state = NO_LOCK;
	u32_t startTime = 0;
	u32_t dLockedTime = 0;
	pDRXJData_t extAttr = NULL;
	u32_t timeoutOfs = 0;

	/* external attributes for storing aquired channel constellation */
	extAttr = (pDRXJData_t) demod->myExtAttr;
	*lockStatus = DRX_NOT_LOCKED;
	startTime = DRXBSP_HST_Clock();
	state = NO_LOCK;
	do {
		CHK_ERROR(CtrlLockStatus(demod, lockStatus));

		switch (state) {
		case NO_LOCK:
			if (*lockStatus == DRXJ_DEMOD_LOCK) {
				CHK_ERROR(CtrlGetQAMSigQuality
					  (demod, &sigQuality));
				if (sigQuality.MER > 208) {
					state = DEMOD_LOCKED;
					/* some delay to see if fec_lock possible TODO find the right value */
					timeoutOfs += DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME;	/* see something, waiting longer */
					dLockedTime = DRXBSP_HST_Clock();
				}
			}
			break;
		case DEMOD_LOCKED:
			if ((*lockStatus == DRXJ_DEMOD_LOCK) &&	/* still demod_lock in 150ms */
			    ((DRXBSP_HST_Clock() - dLockedTime) >
			     DRXJ_QAM_FEC_LOCK_WAITTIME)) {
				RR16(demod->myI2CDevAddr, QAM_SY_TIMEOUT__A,
				     &data);
				WR16(demod->myI2CDevAddr, QAM_SY_TIMEOUT__A,
				     data | 0x1);
				state = SYNC_FLIPPED;
				DRXBSP_HST_Sleep(10);
			}
			break;
		case SYNC_FLIPPED:
			if (*lockStatus == DRXJ_DEMOD_LOCK) {
				if (channel->mirror == DRX_MIRROR_AUTO) {
					/* flip sync pattern back */
					RR16(demod->myI2CDevAddr,
					     QAM_SY_TIMEOUT__A, &data);
					WR16(demod->myI2CDevAddr,
					     QAM_SY_TIMEOUT__A, data & 0xFFFE);
					/* flip spectrum */
					extAttr->mirror = DRX_MIRROR_YES;
					CHK_ERROR(qamFlipSpec(demod, channel));
					state = SPEC_MIRRORED;
					/* reset timer TODO: still need 500ms? */
					startTime = dLockedTime =
					    DRXBSP_HST_Clock();
					timeoutOfs = 0;
				} else {	/* no need to wait lock */

					startTime =
					    DRXBSP_HST_Clock() -
					    DRXJ_QAM_MAX_WAITTIME - timeoutOfs;
				}
			}
			break;
		case SPEC_MIRRORED:
			if ((*lockStatus == DRXJ_DEMOD_LOCK) &&	/* still demod_lock in 150ms */
			    ((DRXBSP_HST_Clock() - dLockedTime) >
			     DRXJ_QAM_FEC_LOCK_WAITTIME)) {
				CHK_ERROR(CtrlGetQAMSigQuality
					  (demod, &sigQuality));
				if (sigQuality.MER > 208) {
					RR16(demod->myI2CDevAddr,
					     QAM_SY_TIMEOUT__A, &data);
					WR16(demod->myI2CDevAddr,
					     QAM_SY_TIMEOUT__A, data | 0x1);
					/* no need to wait lock */
					startTime =
					    DRXBSP_HST_Clock() -
					    DRXJ_QAM_MAX_WAITTIME - timeoutOfs;
				}
			}
			break;
		default:
			break;
		}
		DRXBSP_HST_Sleep(10);
	} while
	    ((*lockStatus != DRX_LOCKED) &&
	     (*lockStatus != DRX_NEVER_LOCK) &&
	     ((DRXBSP_HST_Clock() - startTime) <
	      (DRXJ_QAM_MAX_WAITTIME + timeoutOfs))
	    );
	/* Returning control to apllication ... */

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t QAM256Auto ()
* \brief auto do sync pattern switching and mirroring.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \param tunerFreqOffset: tuner frequency offset.
* \param lockStatus: pointer to lock status.
* \return DRXStatus_t.
*/
static DRXStatus_t
QAM256Auto(pDRXDemodInstance_t demod,
	   pDRXChannel_t channel,
	   DRXFrequency_t tunerFreqOffset, pDRXLockStatus_t lockStatus)
{
	DRXSigQuality_t sigQuality;
	u32_t state = NO_LOCK;
	u32_t startTime = 0;
	u32_t dLockedTime = 0;
	pDRXJData_t extAttr = NULL;
	u32_t timeoutOfs = DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME;

	/* external attributes for storing aquired channel constellation */
	extAttr = (pDRXJData_t) demod->myExtAttr;
	*lockStatus = DRX_NOT_LOCKED;
	startTime = DRXBSP_HST_Clock();
	state = NO_LOCK;
	do {
		CHK_ERROR(CtrlLockStatus(demod, lockStatus));
		switch (state) {
		case NO_LOCK:
			if (*lockStatus == DRXJ_DEMOD_LOCK) {
				CHK_ERROR(CtrlGetQAMSigQuality
					  (demod, &sigQuality));
				if (sigQuality.MER > 268) {
					state = DEMOD_LOCKED;
					timeoutOfs += DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME;	/* see something, wait longer */
					dLockedTime = DRXBSP_HST_Clock();
				}
			}
			break;
		case DEMOD_LOCKED:
			if (*lockStatus == DRXJ_DEMOD_LOCK) {
				if ((channel->mirror == DRX_MIRROR_AUTO) &&
				    ((DRXBSP_HST_Clock() - dLockedTime) >
				     DRXJ_QAM_FEC_LOCK_WAITTIME)) {
					extAttr->mirror = DRX_MIRROR_YES;
					CHK_ERROR(qamFlipSpec(demod, channel));
					state = SPEC_MIRRORED;
					/* reset timer TODO: still need 300ms? */
					startTime = DRXBSP_HST_Clock();
					timeoutOfs = -DRXJ_QAM_MAX_WAITTIME / 2;
				}
			}
			break;
		case SPEC_MIRRORED:
			break;
		default:
			break;
		}
		DRXBSP_HST_Sleep(10);
	} while
	    ((*lockStatus < DRX_LOCKED) &&
	     (*lockStatus != DRX_NEVER_LOCK) &&
	     ((DRXBSP_HST_Clock() - startTime) <
	      (DRXJ_QAM_MAX_WAITTIME + timeoutOfs)));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t SetQAMChannel ()
* \brief Set QAM channel according to the requested constellation.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \return DRXStatus_t.
*/
static DRXStatus_t
SetQAMChannel(pDRXDemodInstance_t demod,
	      pDRXChannel_t channel, DRXFrequency_t tunerFreqOffset)
{
	DRXLockStatus_t lockStatus = DRX_NOT_LOCKED;
	pDRXJData_t extAttr = NULL;
	Bool_t autoFlag = FALSE;

	/* external attributes for storing aquired channel constellation */
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* set QAM channel constellation */
	switch (channel->constellation) {
	case DRX_CONSTELLATION_QAM16:
	case DRX_CONSTELLATION_QAM32:
	case DRX_CONSTELLATION_QAM64:
	case DRX_CONSTELLATION_QAM128:
	case DRX_CONSTELLATION_QAM256:
		extAttr->constellation = channel->constellation;
		if (channel->mirror == DRX_MIRROR_AUTO) {
			extAttr->mirror = DRX_MIRROR_NO;
		} else {
			extAttr->mirror = channel->mirror;
		}
		CHK_ERROR(SetQAM
			  (demod, channel, tunerFreqOffset, QAM_SET_OP_ALL));

		if ((extAttr->standard == DRX_STANDARD_ITU_B) &&
		    (channel->constellation == DRX_CONSTELLATION_QAM64)) {
			CHK_ERROR(QAM64Auto
				  (demod, channel, tunerFreqOffset,
				   &lockStatus));
		}

		if ((extAttr->standard == DRX_STANDARD_ITU_B) &&
		    (channel->mirror == DRX_MIRROR_AUTO) &&
		    (channel->constellation == DRX_CONSTELLATION_QAM256)) {
			CHK_ERROR(QAM256Auto
				  (demod, channel, tunerFreqOffset,
				   &lockStatus));
		}
		break;
	case DRX_CONSTELLATION_AUTO:	/* for channel scan */
		if (extAttr->standard == DRX_STANDARD_ITU_B) {
			autoFlag = TRUE;
			/* try to lock default QAM constellation: QAM64 */
			channel->constellation = DRX_CONSTELLATION_QAM256;
			extAttr->constellation = DRX_CONSTELLATION_QAM256;
			if (channel->mirror == DRX_MIRROR_AUTO) {
				extAttr->mirror = DRX_MIRROR_NO;
			} else {
				extAttr->mirror = channel->mirror;
			}
			CHK_ERROR(SetQAM
				  (demod, channel, tunerFreqOffset,
				   QAM_SET_OP_ALL));
			CHK_ERROR(QAM256Auto
				  (demod, channel, tunerFreqOffset,
				   &lockStatus));

			if (lockStatus < DRX_LOCKED) {
				/* QAM254 not locked -> try to lock QAM64 constellation */
				channel->constellation =
				    DRX_CONSTELLATION_QAM64;
				extAttr->constellation =
				    DRX_CONSTELLATION_QAM64;
				if (channel->mirror == DRX_MIRROR_AUTO) {
					extAttr->mirror = DRX_MIRROR_NO;
				} else {
					extAttr->mirror = channel->mirror;
				}
				{
					u16_t qamCtlEna = 0;
					RR16(demod->myI2CDevAddr,
					     SCU_RAM_QAM_CTL_ENA__A,
					     &qamCtlEna);
					WR16(demod->myI2CDevAddr,
					     SCU_RAM_QAM_CTL_ENA__A,
					     qamCtlEna &
					     ~SCU_RAM_QAM_CTL_ENA_ACQ__M);
					WR16(demod->myI2CDevAddr, SCU_RAM_QAM_FSM_STATE_TGT__A, 0x2);	/* force to rate hunting */

					CHK_ERROR(SetQAM
						  (demod, channel,
						   tunerFreqOffset,
						   QAM_SET_OP_CONSTELLATION));
					WR16(demod->myI2CDevAddr,
					     SCU_RAM_QAM_CTL_ENA__A, qamCtlEna);
				}
				CHK_ERROR(QAM64Auto
					  (demod, channel, tunerFreqOffset,
					   &lockStatus));
			}
			channel->constellation = DRX_CONSTELLATION_AUTO;
		} else if (extAttr->standard == DRX_STANDARD_ITU_C) {
			channel->constellation = DRX_CONSTELLATION_QAM64;
			extAttr->constellation = DRX_CONSTELLATION_QAM64;
			autoFlag = TRUE;

			if (channel->mirror == DRX_MIRROR_AUTO) {
				extAttr->mirror = DRX_MIRROR_NO;
			} else {
				extAttr->mirror = channel->mirror;
			}
			{
				u16_t qamCtlEna = 0;
				RR16(demod->myI2CDevAddr,
				     SCU_RAM_QAM_CTL_ENA__A, &qamCtlEna);
				WR16(demod->myI2CDevAddr,
				     SCU_RAM_QAM_CTL_ENA__A,
				     qamCtlEna & ~SCU_RAM_QAM_CTL_ENA_ACQ__M);
				WR16(demod->myI2CDevAddr, SCU_RAM_QAM_FSM_STATE_TGT__A, 0x2);	/* force to rate hunting */

				CHK_ERROR(SetQAM
					  (demod, channel, tunerFreqOffset,
					   QAM_SET_OP_CONSTELLATION));
				WR16(demod->myI2CDevAddr,
				     SCU_RAM_QAM_CTL_ENA__A, qamCtlEna);
			}
			CHK_ERROR(QAM64Auto
				  (demod, channel, tunerFreqOffset,
				   &lockStatus));
			channel->constellation = DRX_CONSTELLATION_AUTO;
		} else {
			channel->constellation = DRX_CONSTELLATION_AUTO;
			return (DRX_STS_INVALID_ARG);
		}
		break;
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
rw_error:
	/* restore starting value */
	if (autoFlag)
		channel->constellation = DRX_CONSTELLATION_AUTO;
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn static short GetQAMRSErrCount(struct i2c_device_addr * devAddr)
* \brief Get RS error count in QAM mode (used for post RS BER calculation)
* \return Error code
*
* precondition: measurement period & measurement prescale must be set
*
*/
static DRXStatus_t
GetQAMRSErrCount(struct i2c_device_addr *devAddr, pDRXJRSErrors_t RSErrors)
{
	u16_t nrBitErrors = 0,
	    nrSymbolErrors = 0,
	    nrPacketErrors = 0, nrFailures = 0, nrSncParFailCount = 0;

	/* check arguments */
	if (devAddr == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	/* all reported errors are received in the  */
	/* most recently finished measurment period */
	/*   no of pre RS bit errors */
	RR16(devAddr, FEC_RS_NR_BIT_ERRORS__A, &nrBitErrors);
	/*   no of symbol errors      */
	RR16(devAddr, FEC_RS_NR_SYMBOL_ERRORS__A, &nrSymbolErrors);
	/*   no of packet errors      */
	RR16(devAddr, FEC_RS_NR_PACKET_ERRORS__A, &nrPacketErrors);
	/*   no of failures to decode */
	RR16(devAddr, FEC_RS_NR_FAILURES__A, &nrFailures);
	/*   no of post RS bit erros  */
	RR16(devAddr, FEC_OC_SNC_FAIL_COUNT__A, &nrSncParFailCount);
	/* TODO: NOTE */
	/* These register values are fetched in non-atomic fashion           */
	/* It is possible that the read values contain unrelated information */

	RSErrors->nrBitErrors = nrBitErrors & FEC_RS_NR_BIT_ERRORS__M;
	RSErrors->nrSymbolErrors = nrSymbolErrors & FEC_RS_NR_SYMBOL_ERRORS__M;
	RSErrors->nrPacketErrors = nrPacketErrors & FEC_RS_NR_PACKET_ERRORS__M;
	RSErrors->nrFailures = nrFailures & FEC_RS_NR_FAILURES__M;
	RSErrors->nrSncParFailCount =
	    nrSncParFailCount & FEC_OC_SNC_FAIL_COUNT__M;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetQAMSigQuality()
* \brief Retreive QAM signal quality from device.
* \param devmod Pointer to demodulator instance.
* \param sigQuality Pointer to signal quality data.
* \return DRXStatus_t.
* \retval DRX_STS_OK sigQuality contains valid data.
* \retval DRX_STS_INVALID_ARG sigQuality is NULL.
* \retval DRX_STS_ERROR Erroneous data, sigQuality contains invalid data.

*  Pre-condition: Device must be started and in lock.
*/
static DRXStatus_t
CtrlGetQAMSigQuality(pDRXDemodInstance_t demod, pDRXSigQuality_t sigQuality)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	DRXConstellation_t constellation = DRX_CONSTELLATION_UNKNOWN;
	DRXJRSErrors_t measuredRSErrors = { 0, 0, 0, 0, 0 };

	u32_t preBitErrRS = 0;	/* pre RedSolomon Bit Error Rate */
	u32_t postBitErrRS = 0;	/* post RedSolomon Bit Error Rate */
	u32_t pktErrs = 0;	/* no of packet errors in RS */
	u16_t qamSlErrPower = 0;	/* accumulated error between raw and sliced symbols */
	u16_t qsymErrVD = 0;	/* quadrature symbol errors in QAM_VD */
	u16_t fecOcPeriod = 0;	/* SNC sync failure measurement period */
	u16_t fecRsPrescale = 0;	/* ReedSolomon Measurement Prescale */
	u16_t fecRsPeriod = 0;	/* Value for corresponding I2C register */
	/* calculation constants */
	u32_t rsBitCnt = 0;	/* RedSolomon Bit Count */
	u32_t qamSlSigPower = 0;	/* used for MER, depends of QAM constellation */
	/* intermediate results */
	u32_t e = 0;		/* exponent value used for QAM BER/SER */
	u32_t m = 0;		/* mantisa value used for QAM BER/SER */
	u32_t berCnt = 0;	/* BER count */
	/* signal quality info */
	u32_t qamSlMer = 0;	/* QAM MER */
	u32_t qamPreRSBer = 0;	/* Pre RedSolomon BER */
	u32_t qamPostRSBer = 0;	/* Post RedSolomon BER */
	u32_t qamVDSer = 0;	/* ViterbiDecoder SER */
	u16_t qamVdPrescale = 0;	/* Viterbi Measurement Prescale */
	u16_t qamVdPeriod = 0;	/* Viterbi Measurement period */
	u32_t vdBitCnt = 0;	/* ViterbiDecoder Bit Count */

	/* get device basic information */
	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	constellation = extAttr->constellation;

	/* read the physical registers */
	/*   Get the RS error data */
	CHK_ERROR(GetQAMRSErrCount(devAddr, &measuredRSErrors));
	/* get the register value needed for MER */
	RR16(devAddr, QAM_SL_ERR_POWER__A, &qamSlErrPower);
	/* get the register value needed for post RS BER */
	RR16(devAddr, FEC_OC_SNC_FAIL_PERIOD__A, &fecOcPeriod);

	/* get constants needed for signal quality calculation */
	fecRsPeriod = extAttr->fecRsPeriod;
	fecRsPrescale = extAttr->fecRsPrescale;
	rsBitCnt = fecRsPeriod * fecRsPrescale * extAttr->fecRsPlen;
	qamVdPeriod = extAttr->qamVdPeriod;
	qamVdPrescale = extAttr->qamVdPrescale;
	vdBitCnt = qamVdPeriod * qamVdPrescale * extAttr->fecVdPlen;

	/* DRXJ_QAM_SL_SIG_POWER_QAMxxx  * 4     */
	switch (constellation) {
	case DRX_CONSTELLATION_QAM16:
		qamSlSigPower = DRXJ_QAM_SL_SIG_POWER_QAM16 << 2;
		break;
	case DRX_CONSTELLATION_QAM32:
		qamSlSigPower = DRXJ_QAM_SL_SIG_POWER_QAM32 << 2;
		break;
	case DRX_CONSTELLATION_QAM64:
		qamSlSigPower = DRXJ_QAM_SL_SIG_POWER_QAM64 << 2;
		break;
	case DRX_CONSTELLATION_QAM128:
		qamSlSigPower = DRXJ_QAM_SL_SIG_POWER_QAM128 << 2;
		break;
	case DRX_CONSTELLATION_QAM256:
		qamSlSigPower = DRXJ_QAM_SL_SIG_POWER_QAM256 << 2;
		break;
	default:
		return (DRX_STS_ERROR);
	}

	/* ------------------------------ */
	/* MER Calculation                */
	/* ------------------------------ */
	/* MER is good if it is above 27.5 for QAM256 or 21.5 for QAM64 */

	/* 10.0*log10(qam_sl_sig_power * 4.0 / qam_sl_err_power); */
	if (qamSlErrPower == 0)
		qamSlMer = 0;
	else
		qamSlMer =
		    Log10Times100(qamSlSigPower) -
		    Log10Times100((u32_t) qamSlErrPower);

	/* ----------------------------------------- */
	/* Pre Viterbi Symbol Error Rate Calculation */
	/* ----------------------------------------- */
	/* pre viterbi SER is good if it is bellow 0.025 */

	/* get the register value */
	/*   no of quadrature symbol errors */
	RR16(devAddr, QAM_VD_NR_QSYM_ERRORS__A, &qsymErrVD);
	/* Extract the Exponent and the Mantisa  */
	/* of number of quadrature symbol errors */
	e = (qsymErrVD & QAM_VD_NR_QSYM_ERRORS_EXP__M) >>
	    QAM_VD_NR_QSYM_ERRORS_EXP__B;
	m = (qsymErrVD & QAM_VD_NR_SYMBOL_ERRORS_FIXED_MANT__M) >>
	    QAM_VD_NR_SYMBOL_ERRORS_FIXED_MANT__B;

	if ((m << e) >> 3 > 549752) {	/* the max of FracTimes1e6 */
		qamVDSer = 500000;	/* clip BER 0.5 */
	} else {
		qamVDSer =
		    FracTimes1e6(m << ((e > 2) ? (e - 3) : e),
				 vdBitCnt * ((e > 2) ? 1 : 8) / 8);
	}

	/* --------------------------------------- */
	/* pre and post RedSolomon BER Calculation */
	/* --------------------------------------- */
	/* pre RS BER is good if it is below 3.5e-4 */

	/* get the register values */
	preBitErrRS = (u32_t) measuredRSErrors.nrBitErrors;
	pktErrs = postBitErrRS = (u32_t) measuredRSErrors.nrSncParFailCount;

	/* Extract the Exponent and the Mantisa of the */
	/* pre Reed-Solomon bit error count            */
	e = (preBitErrRS & FEC_RS_NR_BIT_ERRORS_EXP__M) >>
	    FEC_RS_NR_BIT_ERRORS_EXP__B;
	m = (preBitErrRS & FEC_RS_NR_BIT_ERRORS_FIXED_MANT__M) >>
	    FEC_RS_NR_BIT_ERRORS_FIXED_MANT__B;

	berCnt = m << e;

	/*qamPreRSBer = FracTimes1e6( berCnt, rsBitCnt ); */
	if (m > (rsBitCnt >> (e + 1)) || (rsBitCnt >> e) == 0) {
		qamPreRSBer = 500000;	/* clip BER 0.5 */
	} else {
		qamPreRSBer = FracTimes1e6(m, rsBitCnt >> e);
	}

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
	e = postBitErrRS * 742686;
	m = fecOcPeriod * 100;
	if (fecOcPeriod == 0)
		qamPostRSBer = 0xFFFFFFFF;
	else
		qamPostRSBer = e / m;

	/* fill signal quality data structure */
	sigQuality->MER = ((u16_t) qamSlMer);
	if (extAttr->standard == DRX_STANDARD_ITU_B) {
		sigQuality->preViterbiBER = qamVDSer;
	} else {
		sigQuality->preViterbiBER = qamPreRSBer;
	}
	sigQuality->postViterbiBER = qamPreRSBer;
	sigQuality->postReedSolomonBER = qamPostRSBer;
	sigQuality->scaleFactorBER = ((u32_t) 1000000);
#ifdef DRXJ_SIGNAL_ACCUM_ERR
	CHK_ERROR(GetAccPktErr(demod, &sigQuality->packetError));
#else
	sigQuality->packetError = ((u16_t) pktErrs);
#endif

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t CtrlGetQAMConstel()
* \brief Retreive a QAM constellation point via I2C.
* \param demod Pointer to demodulator instance.
* \param complexNr Pointer to the structure in which to store the
		   constellation point.
* \return DRXStatus_t.
*/
static DRXStatus_t
CtrlGetQAMConstel(pDRXDemodInstance_t demod, pDRXComplex_t complexNr)
{
	u16_t fecOcOcrMode = 0;
			      /**< FEC OCR grabber configuration        */
	u16_t qamSlCommMb = 0;/**< QAM SL MB configuration              */
	u16_t qamSlCommMbInit = 0;
			      /**< QAM SL MB intial configuration       */
	u16_t im = 0;	      /**< constellation Im part                */
	u16_t re = 0;	      /**< constellation Re part                */
	u32_t data = 0;
	struct i2c_device_addr *devAddr = NULL;
				     /**< device address */

	/* read device info */
	devAddr = demod->myI2CDevAddr;

	/* TODO: */
	/* Monitor bus grabbing is an open external interface issue  */
	/* Needs to be checked when external interface PG is updated */

	/* Configure MB (Monitor bus) */
	RR16(devAddr, QAM_SL_COMM_MB__A, &qamSlCommMbInit);
	/* set observe flag & MB mux */
	qamSlCommMb = qamSlCommMbInit & (~(QAM_SL_COMM_MB_OBS__M +
					   QAM_SL_COMM_MB_MUX_OBS__M));
	qamSlCommMb |= (QAM_SL_COMM_MB_OBS_ON +
			QAM_SL_COMM_MB_MUX_OBS_CONST_CORR);
	WR16(devAddr, QAM_SL_COMM_MB__A, qamSlCommMb);

	/* Enable MB grabber in the FEC OC */
	fecOcOcrMode = (	/* output select:  observe bus */
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
	WR16(devAddr, FEC_OC_OCR_MODE__A, fecOcOcrMode);

	/* Disable MB grabber in the FEC OC */
	WR16(devAddr, FEC_OC_OCR_MODE__A, 0x00);

	/* read data */
	RR32(devAddr, FEC_OC_OCR_GRAB_RD0__A, &data);
	re = (u16_t) (data & FEC_OC_OCR_GRAB_RD0__M);
	im = (u16_t) ((data >> 16) & FEC_OC_OCR_GRAB_RD1__M);

	/* TODO: */
	/* interpret data (re & im) according to the Monitor bus mapping ?? */

	/* sign extension, 10th bit is sign bit */
	if ((re & 0x0200) == 0x0200) {
		re |= 0xFC00;
	}
	if ((im & 0x0200) == 0x0200) {
		im |= 0xFC00;
	}
	complexNr->re = ((s16_t) re);
	complexNr->im = ((s16_t) im);

	/* Restore MB (Monitor bus) */
	WR16(devAddr, QAM_SL_COMM_MB__A, qamSlCommMbInit);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
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
      to perform the settings only once after a DRX_Open(). The driver must
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
* \brief Get array index for atv coef (extAttr->atvTopCoefX[index])
* \param standard
* \param pointer to index
* \return DRXStatus_t.
*
*/
static DRXStatus_t AtvEquCoefIndex(DRXStandard_t standard, int *index)
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
		return DRX_STS_ERROR;
		break;
	}

	return DRX_STS_OK;
}

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t AtvUpdateConfig ()
* \brief Flush changes in ATV shadow registers to physical registers.
* \param demod instance of demodulator
* \param forceUpdate don't look at standard or change flags, flush all.
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AtvUpdateConfig(pDRXDemodInstance_t demod, Bool_t forceUpdate)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* equalizer coefficients */
	if (forceUpdate ||
	    ((extAttr->atvCfgChangedFlags & DRXJ_ATV_CHANGED_COEF) != 0)) {
		int index = 0;

		CHK_ERROR(AtvEquCoefIndex(extAttr->standard, &index));
		WR16(devAddr, ATV_TOP_EQU0__A, extAttr->atvTopEqu0[index]);
		WR16(devAddr, ATV_TOP_EQU1__A, extAttr->atvTopEqu1[index]);
		WR16(devAddr, ATV_TOP_EQU2__A, extAttr->atvTopEqu2[index]);
		WR16(devAddr, ATV_TOP_EQU3__A, extAttr->atvTopEqu3[index]);
	}

	/* bypass fast carrier recovery */
	if (forceUpdate) {
		u16_t data = 0;

		RR16(devAddr, IQM_RT_ROT_BP__A, &data);
		data &= (~((u16_t) IQM_RT_ROT_BP_ROT_OFF__M));
		if (extAttr->phaseCorrectionBypass) {
			data |= IQM_RT_ROT_BP_ROT_OFF_OFF;
		} else {
			data |= IQM_RT_ROT_BP_ROT_OFF_ACTIVE;
		}
		WR16(devAddr, IQM_RT_ROT_BP__A, data);
	}

	/* peak filter setting */
	if (forceUpdate ||
	    ((extAttr->atvCfgChangedFlags & DRXJ_ATV_CHANGED_PEAK_FLT) != 0)) {
		WR16(devAddr, ATV_TOP_VID_PEAK__A, extAttr->atvTopVidPeak);
	}

	/* noise filter setting */
	if (forceUpdate ||
	    ((extAttr->atvCfgChangedFlags & DRXJ_ATV_CHANGED_NOISE_FLT) != 0)) {
		WR16(devAddr, ATV_TOP_NOISE_TH__A, extAttr->atvTopNoiseTh);
	}

	/* SIF attenuation */
	if (forceUpdate ||
	    ((extAttr->atvCfgChangedFlags & DRXJ_ATV_CHANGED_SIF_ATT) != 0)) {
		u16_t attenuation = 0;

		switch (extAttr->sifAttenuation) {
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
			return DRX_STS_ERROR;
			break;
		}
		WR16(devAddr, ATV_TOP_AF_SIF_ATT__A, attenuation);
	}

	/* SIF & CVBS enable */
	if (forceUpdate ||
	    ((extAttr->atvCfgChangedFlags & DRXJ_ATV_CHANGED_OUTPUT) != 0)) {
		u16_t data = 0;

		RR16(devAddr, ATV_TOP_STDBY__A, &data);
		if (extAttr->enableCVBSOutput) {
			data |= ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE;
		} else {
			data &= (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE);
		}

		if (extAttr->enableSIFOutput) {
			data &= (~ATV_TOP_STDBY_SIF_STDBY_STANDBY);
		} else {
			data |= ATV_TOP_STDBY_SIF_STDBY_STANDBY;
		}
		WR16(devAddr, ATV_TOP_STDBY__A, data);
	}

	extAttr->atvCfgChangedFlags = 0;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t CtrlSetCfgATVOutput()
* \brief Configure ATV ouputs
* \param demod instance of demodulator
* \param outputCfg output configuaration
* \return DRXStatus_t.
*
*/
static DRXStatus_t
CtrlSetCfgATVOutput(pDRXDemodInstance_t demod, pDRXJCfgAtvOutput_t outputCfg)
{
	pDRXJData_t extAttr = NULL;

	/* Check arguments */
	if (outputCfg == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	if (outputCfg->enableSIFOutput) {
		switch (outputCfg->sifAttenuation) {
		case DRXJ_SIF_ATTENUATION_0DB:	/* fallthrough */
		case DRXJ_SIF_ATTENUATION_3DB:	/* fallthrough */
		case DRXJ_SIF_ATTENUATION_6DB:	/* fallthrough */
		case DRXJ_SIF_ATTENUATION_9DB:
			/* Do nothing */
			break;
		default:
			return DRX_STS_INVALID_ARG;
			break;
		}

		if (extAttr->sifAttenuation != outputCfg->sifAttenuation) {
			extAttr->sifAttenuation = outputCfg->sifAttenuation;
			extAttr->atvCfgChangedFlags |= DRXJ_ATV_CHANGED_SIF_ATT;
		}
	}

	if (extAttr->enableCVBSOutput != outputCfg->enableCVBSOutput) {
		extAttr->enableCVBSOutput = outputCfg->enableCVBSOutput;
		extAttr->atvCfgChangedFlags |= DRXJ_ATV_CHANGED_OUTPUT;
	}

	if (extAttr->enableSIFOutput != outputCfg->enableSIFOutput) {
		extAttr->enableSIFOutput = outputCfg->enableSIFOutput;
		extAttr->atvCfgChangedFlags |= DRXJ_ATV_CHANGED_OUTPUT;
	}

	CHK_ERROR(AtvUpdateConfig(demod, FALSE));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
#ifndef DRXJ_DIGITAL_ONLY
/**
* \fn DRXStatus_t CtrlSetCfgAtvEquCoef()
* \brief Set ATV equalizer coefficients
* \param demod instance of demodulator
* \param coef  the equalizer coefficients
* \return DRXStatus_t.
*
*/
static DRXStatus_t
CtrlSetCfgAtvEquCoef(pDRXDemodInstance_t demod, pDRXJCfgAtvEquCoef_t coef)
{
	pDRXJData_t extAttr = NULL;
	int index;

	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* current standard needs to be an ATV standard */
	if (!DRXJ_ISATVSTD(extAttr->standard)) {
		return DRX_STS_ERROR;
	}

	/* Check arguments */
	if ((coef == NULL) ||
	    (coef->coef0 > (ATV_TOP_EQU0_EQU_C0__M / 2)) ||
	    (coef->coef1 > (ATV_TOP_EQU1_EQU_C1__M / 2)) ||
	    (coef->coef2 > (ATV_TOP_EQU2_EQU_C2__M / 2)) ||
	    (coef->coef3 > (ATV_TOP_EQU3_EQU_C3__M / 2)) ||
	    (coef->coef0 < ((s16_t) ~ (ATV_TOP_EQU0_EQU_C0__M >> 1))) ||
	    (coef->coef1 < ((s16_t) ~ (ATV_TOP_EQU1_EQU_C1__M >> 1))) ||
	    (coef->coef2 < ((s16_t) ~ (ATV_TOP_EQU2_EQU_C2__M >> 1))) ||
	    (coef->coef3 < ((s16_t) ~ (ATV_TOP_EQU3_EQU_C3__M >> 1)))) {
		return (DRX_STS_INVALID_ARG);
	}

	CHK_ERROR(AtvEquCoefIndex(extAttr->standard, &index));
	extAttr->atvTopEqu0[index] = coef->coef0;
	extAttr->atvTopEqu1[index] = coef->coef1;
	extAttr->atvTopEqu2[index] = coef->coef2;
	extAttr->atvTopEqu3[index] = coef->coef3;
	extAttr->atvCfgChangedFlags |= DRXJ_ATV_CHANGED_COEF;

	CHK_ERROR(AtvUpdateConfig(demod, FALSE));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t CtrlGetCfgAtvEquCoef()
* \brief Get ATV equ coef settings
* \param demod instance of demodulator
* \param coef The ATV equ coefficients
* \return DRXStatus_t.
*
* The values are read from the shadow registers maintained by the drxdriver
* If registers are manipulated outside of the drxdriver scope the reported
* settings will not reflect these changes because of the use of shadow
* regitsers.
*
*/
static DRXStatus_t
CtrlGetCfgAtvEquCoef(pDRXDemodInstance_t demod, pDRXJCfgAtvEquCoef_t coef)
{
	pDRXJData_t extAttr = NULL;
	int index = 0;

	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* current standard needs to be an ATV standard */
	if (!DRXJ_ISATVSTD(extAttr->standard)) {
		return DRX_STS_ERROR;
	}

	/* Check arguments */
	if (coef == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	CHK_ERROR(AtvEquCoefIndex(extAttr->standard, &index));
	coef->coef0 = extAttr->atvTopEqu0[index];
	coef->coef1 = extAttr->atvTopEqu1[index];
	coef->coef2 = extAttr->atvTopEqu2[index];
	coef->coef3 = extAttr->atvTopEqu3[index];

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t CtrlSetCfgAtvMisc()
* \brief Set misc. settings for ATV.
* \param demod instance of demodulator
* \param
* \return DRXStatus_t.
*
*/
static DRXStatus_t
CtrlSetCfgAtvMisc(pDRXDemodInstance_t demod, pDRXJCfgAtvMisc_t settings)
{
	pDRXJData_t extAttr = NULL;

	/* Check arguments */
	if ((settings == NULL) ||
	    ((settings->peakFilter) < (s16_t) (-8)) ||
	    ((settings->peakFilter) > (s16_t) (15)) ||
	    ((settings->noiseFilter) > 15)) {
		return (DRX_STS_INVALID_ARG);
	}
	/* if */
	extAttr = (pDRXJData_t) demod->myExtAttr;

	if (settings->peakFilter != extAttr->atvTopVidPeak) {
		extAttr->atvTopVidPeak = settings->peakFilter;
		extAttr->atvCfgChangedFlags |= DRXJ_ATV_CHANGED_PEAK_FLT;
	}

	if (settings->noiseFilter != extAttr->atvTopNoiseTh) {
		extAttr->atvTopNoiseTh = settings->noiseFilter;
		extAttr->atvCfgChangedFlags |= DRXJ_ATV_CHANGED_NOISE_FLT;
	}

	CHK_ERROR(AtvUpdateConfig(demod, FALSE));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t  CtrlGetCfgAtvMisc()
* \brief Get misc settings of ATV.
* \param demod instance of demodulator
* \param settings misc. ATV settings
* \return DRXStatus_t.
*
* The values are read from the shadow registers maintained by the drxdriver
* If registers are manipulated outside of the drxdriver scope the reported
* settings will not reflect these changes because of the use of shadow
* regitsers.
*/
static DRXStatus_t
CtrlGetCfgAtvMisc(pDRXDemodInstance_t demod, pDRXJCfgAtvMisc_t settings)
{
	pDRXJData_t extAttr = NULL;

	/* Check arguments */
	if (settings == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;

	settings->peakFilter = extAttr->atvTopVidPeak;
	settings->noiseFilter = extAttr->atvTopNoiseTh;

	return (DRX_STS_OK);
}

/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t  CtrlGetCfgAtvOutput()
* \brief
* \param demod instance of demodulator
* \param outputCfg output configuaration
* \return DRXStatus_t.
*
*/
static DRXStatus_t
CtrlGetCfgAtvOutput(pDRXDemodInstance_t demod, pDRXJCfgAtvOutput_t outputCfg)
{
	u16_t data = 0;

	/* Check arguments */
	if (outputCfg == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	RR16(demod->myI2CDevAddr, ATV_TOP_STDBY__A, &data);
	if (data & ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE) {
		outputCfg->enableCVBSOutput = TRUE;
	} else {
		outputCfg->enableCVBSOutput = FALSE;
	}

	if (data & ATV_TOP_STDBY_SIF_STDBY_STANDBY) {
		outputCfg->enableSIFOutput = FALSE;
	} else {
		outputCfg->enableSIFOutput = TRUE;
		RR16(demod->myI2CDevAddr, ATV_TOP_AF_SIF_ATT__A, &data);
		outputCfg->sifAttenuation = (DRXJSIFAttenuation_t) data;
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t  CtrlGetCfgAtvAgcStatus()
* \brief
* \param demod instance of demodulator
* \param agcStatus agc status
* \return DRXStatus_t.
*
*/
static DRXStatus_t
CtrlGetCfgAtvAgcStatus(pDRXDemodInstance_t demod,
		       pDRXJCfgAtvAgcStatus_t agcStatus)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	u16_t data = 0;
	u32_t tmp = 0;

	/* Check arguments */
	if (agcStatus == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/*
	   RFgain = (IQM_AF_AGC_RF__A * 26.75)/1000 (uA)
	   = ((IQM_AF_AGC_RF__A * 27) - (0.25*IQM_AF_AGC_RF__A))/1000

	   IQM_AF_AGC_RF__A * 27 is 20 bits worst case.
	 */
	RR16(devAddr, IQM_AF_AGC_RF__A, &data);
	tmp = ((u32_t) data) * 27 - ((u32_t) (data >> 2));	/* nA */
	agcStatus->rfAgcGain = (u16_t) (tmp / 1000);	/* uA */
	/* rounding */
	if (tmp % 1000 >= 500) {
		(agcStatus->rfAgcGain)++;
	}

	/*
	   IFgain = (IQM_AF_AGC_IF__A * 26.75)/1000 (uA)
	   = ((IQM_AF_AGC_IF__A * 27) - (0.25*IQM_AF_AGC_IF__A))/1000

	   IQM_AF_AGC_IF__A * 27 is 20 bits worst case.
	 */
	RR16(devAddr, IQM_AF_AGC_IF__A, &data);
	tmp = ((u32_t) data) * 27 - ((u32_t) (data >> 2));	/* nA */
	agcStatus->ifAgcGain = (u16_t) (tmp / 1000);	/* uA */
	/* rounding */
	if (tmp % 1000 >= 500) {
		(agcStatus->ifAgcGain)++;
	}

	/*
	   videoGain = (ATV_TOP_SFR_VID_GAIN__A/16 -150)* 0.05 (dB)
	   = (ATV_TOP_SFR_VID_GAIN__A/16 -150)/20 (dB)
	   = 10*(ATV_TOP_SFR_VID_GAIN__A/16 -150)/20 (in 0.1 dB)
	   = (ATV_TOP_SFR_VID_GAIN__A/16 -150)/2 (in 0.1 dB)
	   = (ATV_TOP_SFR_VID_GAIN__A/32) - 75 (in 0.1 dB)
	 */

	SARR16(devAddr, SCU_RAM_ATV_VID_GAIN_HI__A, &data);
	/* dividing by 32 inclusive rounding */
	data >>= 4;
	if ((data & 1) != 0) {
		data++;
	}
	data >>= 1;
	agcStatus->videoAgcGain = ((s16_t) data) - 75;	/* 0.1 dB */

	/*
	   audioGain = (SCU_RAM_ATV_SIF_GAIN__A -8)* 0.05 (dB)
	   = (SCU_RAM_ATV_SIF_GAIN__A -8)/20 (dB)
	   = 10*(SCU_RAM_ATV_SIF_GAIN__A -8)/20 (in 0.1 dB)
	   = (SCU_RAM_ATV_SIF_GAIN__A -8)/2 (in 0.1 dB)
	   = (SCU_RAM_ATV_SIF_GAIN__A/2) - 4 (in 0.1 dB)
	 */

	SARR16(devAddr, SCU_RAM_ATV_SIF_GAIN__A, &data);
	data &= SCU_RAM_ATV_SIF_GAIN__M;
	/* dividing by 2 inclusive rounding */
	if ((data & 1) != 0) {
		data++;
	}
	data >>= 1;
	agcStatus->audioAgcGain = ((s16_t) data) - 4;	/* 0.1 dB */

	/* Loop gain's */
	SARR16(devAddr, SCU_RAM_AGC_KI__A, &data);
	agcStatus->videoAgcLoopGain =
	    ((data & SCU_RAM_AGC_KI_DGAIN__M) >> SCU_RAM_AGC_KI_DGAIN__B);
	agcStatus->rfAgcLoopGain =
	    ((data & SCU_RAM_AGC_KI_RF__M) >> SCU_RAM_AGC_KI_RF__B);
	agcStatus->ifAgcLoopGain =
	    ((data & SCU_RAM_AGC_KI_IF__M) >> SCU_RAM_AGC_KI_IF__B);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */

/**
* \fn DRXStatus_t PowerUpATV ()
* \brief Power up ATV.
* \param demod instance of demodulator
* \param standard either NTSC or FM (sub strandard for ATV )
* \return DRXStatus_t.
*
* * Starts ATV and IQM
* * AUdio already started during standard init for ATV.
*/
static DRXStatus_t PowerUpATV(pDRXDemodInstance_t demod, DRXStandard_t standard)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* ATV NTSC */
	WR16(devAddr, ATV_COMM_EXEC__A, ATV_COMM_EXEC_ACTIVE);
	/* turn on IQM_AF */
	CHK_ERROR(SetIqmAf(demod, TRUE));
	CHK_ERROR(ADCSynchronization(demod));

	WR16(devAddr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_ACTIVE);

	/* Audio, already done during set standard */

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}
#endif /* #ifndef DRXJ_DIGITAL_ONLY */

/* -------------------------------------------------------------------------- */

/**
* \fn DRXStatus_t PowerDownATV ()
* \brief Power down ATV.
* \param demod instance of demodulator
* \param standard either NTSC or FM (sub strandard for ATV )
* \return DRXStatus_t.
*
*  Stops and thus resets ATV and IQM block
*  SIF and CVBS ADC are powered down
*  Calls audio power down
*/
static DRXStatus_t
PowerDownATV(pDRXDemodInstance_t demod, DRXStandard_t standard, Bool_t primary)
{
	struct i2c_device_addr *devAddr = NULL;
	DRXJSCUCmd_t cmdSCU = { /* command      */ 0,
		/* parameterLen */ 0,
		/* resultLen    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	u16_t cmdResult = 0;
	pDRXJData_t extAttr = NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	/* ATV NTSC */

	/* Stop ATV SCU (will reset ATV and IQM hardware */
	cmdSCU.command = SCU_RAM_COMMAND_STANDARD_ATV |
	    SCU_RAM_COMMAND_CMD_DEMOD_STOP;
	cmdSCU.parameterLen = 0;
	cmdSCU.resultLen = 1;
	cmdSCU.parameter = NULL;
	cmdSCU.result = &cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));
	/* Disable ATV outputs (ATV reset enables CVBS, undo this) */
	WR16(devAddr, ATV_TOP_STDBY__A, (ATV_TOP_STDBY_SIF_STDBY_STANDBY &
					 (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE)));

	WR16(devAddr, ATV_COMM_EXEC__A, ATV_COMM_EXEC_STOP);
	if (primary == TRUE) {
		WR16(devAddr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_STOP);
		CHK_ERROR(SetIqmAf(demod, FALSE));
	} else {
		WR16(devAddr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP);
		WR16(devAddr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP);
		WR16(devAddr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP);
		WR16(devAddr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP);
		WR16(devAddr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP);
	}
	CHK_ERROR(PowerDownAud(demod));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t SetATVStandard ()
* \brief Set up ATV demodulator.
* \param demod instance of demodulator
* \param standard either NTSC or FM (sub strandard for ATV )
* \return DRXStatus_t.
*
* Init all channel independent registers.
* Assuming that IQM, ATV and AUD blocks have been reset and are in STOP mode
*
*/
#ifndef DRXJ_DIGITAL_ONLY
#define SCU_RAM_ATV_ENABLE_IIR_WA__A 0x831F6D	/* TODO remove after done with reg import */
static DRXStatus_t
SetATVStandard(pDRXDemodInstance_t demod, pDRXStandard_t standard)
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
	const u8_t ntsc_taps_re[] = {
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
	const u8_t ntsc_taps_im[] = {
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
	const u8_t bg_taps_re[] = {
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
	const u8_t bg_taps_im[] = {
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
	const u8_t dk_i_l_lp_taps_re[] = {
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
	const u8_t dk_i_l_lp_taps_im[] = {
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
	const u8_t fm_taps_re[] = {
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
	const u8_t fm_taps_im[] = {
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

	struct i2c_device_addr *devAddr = NULL;
	DRXJSCUCmd_t cmdSCU = { /* command      */ 0,
		/* parameterLen */ 0,
		/* resultLen    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	u16_t cmdResult = 0;
	u16_t cmdParam = 0;
#ifdef DRXJ_SPLIT_UCODE_UPLOAD
	DRXUCodeInfo_t ucodeInfo;
	pDRXCommonAttr_t commonAttr = NULL;
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */
	pDRXJData_t extAttr = NULL;

	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = demod->myI2CDevAddr;

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
	commonAttr = demod->myCommonAttr;

	/* Check if audio microcode is already uploaded */
	if (!(extAttr->flagAudMcUploaded)) {
		ucodeInfo.mcData = commonAttr->microcode;
		ucodeInfo.mcSize = commonAttr->microcodeSize;

		/* Upload only audio microcode */
		CHK_ERROR(CtrlUCodeUpload
			  (demod, &ucodeInfo, UCODE_UPLOAD, TRUE));

		if (commonAttr->verifyMicrocode == TRUE) {
			CHK_ERROR(CtrlUCodeUpload
				  (demod, &ucodeInfo, UCODE_VERIFY, TRUE));
		}

		/* Prevent uploading audio microcode again */
		extAttr->flagAudMcUploaded = TRUE;
	}
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

	WR16(devAddr, ATV_COMM_EXEC__A, ATV_COMM_EXEC_STOP);
	WR16(devAddr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP);
	WR16(devAddr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP);
	WR16(devAddr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP);
	WR16(devAddr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP);
	WR16(devAddr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP);
	/* Reset ATV SCU */
	cmdSCU.command = SCU_RAM_COMMAND_STANDARD_ATV |
	    SCU_RAM_COMMAND_CMD_DEMOD_RESET;
	cmdSCU.parameterLen = 0;
	cmdSCU.resultLen = 1;
	cmdSCU.parameter = NULL;
	cmdSCU.result = &cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

	WR16(devAddr, ATV_TOP_MOD_CONTROL__A, ATV_TOP_MOD_CONTROL__PRE);

	/* TODO remove AUTO/OFF patches after ucode fix. */
	switch (*standard) {
	case DRX_STANDARD_NTSC:
		/* NTSC */
		cmdParam = SCU_RAM_ATV_STANDARD_STANDARD_MN;

		WR16(devAddr, IQM_RT_LO_INCR__A, IQM_RT_LO_INCR_MN);
		WR16(devAddr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M);
		WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(ntsc_taps_re),
		    ((pu8_t) ntsc_taps_re));
		WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(ntsc_taps_im),
		    ((pu8_t) ntsc_taps_im));

		WR16(devAddr, ATV_TOP_CR_AMP_TH__A, ATV_TOP_CR_AMP_TH_MN);
		WR16(devAddr, ATV_TOP_CR_CONT__A,
		     (ATV_TOP_CR_CONT_CR_P_MN |
		      ATV_TOP_CR_CONT_CR_D_MN | ATV_TOP_CR_CONT_CR_I_MN));
		WR16(devAddr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_MN);
		WR16(devAddr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_MN |
					       ATV_TOP_STD_VID_POL_MN));
		WR16(devAddr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_MN);

		WR16(devAddr, SCU_RAM_ATV_AGC_MODE__A,
		     (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM |
		      SCU_RAM_ATV_AGC_MODE_FAST_VAGC_EN_FAGC_ENABLE));
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000);
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000);
		WR16(devAddr, SCU_RAM_ATV_AMS_MAX_REF__A,
		     SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_BG_MN);
		extAttr->phaseCorrectionBypass = FALSE;
		extAttr->enableCVBSOutput = TRUE;
		break;
	case DRX_STANDARD_FM:
		/* FM */
		cmdParam = SCU_RAM_ATV_STANDARD_STANDARD_FM;

		WR16(devAddr, IQM_RT_LO_INCR__A, 2994);
		WR16(devAddr, IQM_CF_MIDTAP__A, 0);
		WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(fm_taps_re),
		    ((pu8_t) fm_taps_re));
		WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(fm_taps_im),
		    ((pu8_t) fm_taps_im));
		WR16(devAddr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_FM |
					       ATV_TOP_STD_VID_POL_FM));
		WR16(devAddr, ATV_TOP_MOD_CONTROL__A, 0);
		WR16(devAddr, ATV_TOP_CR_CONT__A, 0);

		WR16(devAddr, SCU_RAM_ATV_AGC_MODE__A,
		     (SCU_RAM_ATV_AGC_MODE_VAGC_VEL_AGC_SLOW |
		      SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM));
		WR16(devAddr, IQM_RT_ROT_BP__A, IQM_RT_ROT_BP_ROT_OFF_OFF);
		extAttr->phaseCorrectionBypass = TRUE;
		extAttr->enableCVBSOutput = FALSE;
		break;
	case DRX_STANDARD_PAL_SECAM_BG:
		/* PAL/SECAM B/G */
		cmdParam = SCU_RAM_ATV_STANDARD_STANDARD_B;

		WR16(devAddr, IQM_RT_LO_INCR__A, 1820);	/* TODO check with IS */
		WR16(devAddr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M);
		WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(bg_taps_re),
		    ((pu8_t) bg_taps_re));
		WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(bg_taps_im),
		    ((pu8_t) bg_taps_im));
		WR16(devAddr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_BG);
		WR16(devAddr, ATV_TOP_CR_AMP_TH__A, ATV_TOP_CR_AMP_TH_BG);
		WR16(devAddr, ATV_TOP_CR_CONT__A,
		     (ATV_TOP_CR_CONT_CR_P_BG |
		      ATV_TOP_CR_CONT_CR_D_BG | ATV_TOP_CR_CONT_CR_I_BG));
		WR16(devAddr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_BG);
		WR16(devAddr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_BG |
					       ATV_TOP_STD_VID_POL_BG));
		WR16(devAddr, SCU_RAM_ATV_AGC_MODE__A,
		     (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM |
		      SCU_RAM_ATV_AGC_MODE_FAST_VAGC_EN_FAGC_ENABLE));
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000);
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000);
		WR16(devAddr, SCU_RAM_ATV_AMS_MAX_REF__A,
		     SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_BG_MN);
		extAttr->phaseCorrectionBypass = FALSE;
		extAttr->atvIfAgcCfg.ctrlMode = DRX_AGC_CTRL_AUTO;
		extAttr->enableCVBSOutput = TRUE;
		break;
	case DRX_STANDARD_PAL_SECAM_DK:
		/* PAL/SECAM D/K */
		cmdParam = SCU_RAM_ATV_STANDARD_STANDARD_DK;

		WR16(devAddr, IQM_RT_LO_INCR__A, 2225);	/* TODO check with IS */
		WR16(devAddr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M);
		WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(dk_i_l_lp_taps_re),
		    ((pu8_t) dk_i_l_lp_taps_re));
		WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(dk_i_l_lp_taps_im),
		    ((pu8_t) dk_i_l_lp_taps_im));
		WR16(devAddr, ATV_TOP_CR_AMP_TH__A, ATV_TOP_CR_AMP_TH_DK);
		WR16(devAddr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_DK);
		WR16(devAddr, ATV_TOP_CR_CONT__A,
		     (ATV_TOP_CR_CONT_CR_P_DK |
		      ATV_TOP_CR_CONT_CR_D_DK | ATV_TOP_CR_CONT_CR_I_DK));
		WR16(devAddr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_DK);
		WR16(devAddr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_DK |
					       ATV_TOP_STD_VID_POL_DK));
		WR16(devAddr, SCU_RAM_ATV_AGC_MODE__A,
		     (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM |
		      SCU_RAM_ATV_AGC_MODE_FAST_VAGC_EN_FAGC_ENABLE));
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000);
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000);
		WR16(devAddr, SCU_RAM_ATV_AMS_MAX_REF__A,
		     SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_DK);
		extAttr->phaseCorrectionBypass = FALSE;
		extAttr->atvIfAgcCfg.ctrlMode = DRX_AGC_CTRL_AUTO;
		extAttr->enableCVBSOutput = TRUE;
		break;
	case DRX_STANDARD_PAL_SECAM_I:
		/* PAL/SECAM I   */
		cmdParam = SCU_RAM_ATV_STANDARD_STANDARD_I;

		WR16(devAddr, IQM_RT_LO_INCR__A, 2225);	/* TODO check with IS */
		WR16(devAddr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M);
		WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(dk_i_l_lp_taps_re),
		    ((pu8_t) dk_i_l_lp_taps_re));
		WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(dk_i_l_lp_taps_im),
		    ((pu8_t) dk_i_l_lp_taps_im));
		WR16(devAddr, ATV_TOP_CR_AMP_TH__A, ATV_TOP_CR_AMP_TH_I);
		WR16(devAddr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_I);
		WR16(devAddr, ATV_TOP_CR_CONT__A,
		     (ATV_TOP_CR_CONT_CR_P_I |
		      ATV_TOP_CR_CONT_CR_D_I | ATV_TOP_CR_CONT_CR_I_I));
		WR16(devAddr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_I);
		WR16(devAddr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_I |
					       ATV_TOP_STD_VID_POL_I));
		WR16(devAddr, SCU_RAM_ATV_AGC_MODE__A,
		     (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_FM |
		      SCU_RAM_ATV_AGC_MODE_FAST_VAGC_EN_FAGC_ENABLE));
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000);
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000);
		WR16(devAddr, SCU_RAM_ATV_AMS_MAX_REF__A,
		     SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_I);
		extAttr->phaseCorrectionBypass = FALSE;
		extAttr->atvIfAgcCfg.ctrlMode = DRX_AGC_CTRL_AUTO;
		extAttr->enableCVBSOutput = TRUE;
		break;
	case DRX_STANDARD_PAL_SECAM_L:
		/* PAL/SECAM L with negative modulation */
		cmdParam = SCU_RAM_ATV_STANDARD_STANDARD_L;

		WR16(devAddr, IQM_RT_LO_INCR__A, 2225);	/* TODO check with IS */
		WR16(devAddr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_L);
		WR16(devAddr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M);
		WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(dk_i_l_lp_taps_re),
		    ((pu8_t) dk_i_l_lp_taps_re));
		WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(dk_i_l_lp_taps_im),
		    ((pu8_t) dk_i_l_lp_taps_im));
		WR16(devAddr, ATV_TOP_CR_AMP_TH__A, 0x2);	/* TODO check with IS */
		WR16(devAddr, ATV_TOP_CR_CONT__A,
		     (ATV_TOP_CR_CONT_CR_P_L |
		      ATV_TOP_CR_CONT_CR_D_L | ATV_TOP_CR_CONT_CR_I_L));
		WR16(devAddr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_L);
		WR16(devAddr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_L |
					       ATV_TOP_STD_VID_POL_L));
		WR16(devAddr, SCU_RAM_ATV_AGC_MODE__A,
		     (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_AM |
		      SCU_RAM_ATV_AGC_MODE_BP_EN_BPC_ENABLE |
		      SCU_RAM_ATV_AGC_MODE_VAGC_VEL_AGC_SLOW));
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000);
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000);
		WR16(devAddr, SCU_RAM_ATV_AMS_MAX_REF__A,
		     SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_LLP);
		extAttr->phaseCorrectionBypass = FALSE;
		extAttr->atvIfAgcCfg.ctrlMode = DRX_AGC_CTRL_USER;
		extAttr->atvIfAgcCfg.outputLevel = extAttr->atvRfAgcCfg.top;
		extAttr->enableCVBSOutput = TRUE;
		break;
	case DRX_STANDARD_PAL_SECAM_LP:
		/* PAL/SECAM L with positive modulation */
		cmdParam = SCU_RAM_ATV_STANDARD_STANDARD_LP;

		WR16(devAddr, ATV_TOP_VID_AMP__A, ATV_TOP_VID_AMP_LP);
		WR16(devAddr, IQM_RT_LO_INCR__A, 2225);	/* TODO check with IS */
		WR16(devAddr, IQM_CF_MIDTAP__A, IQM_CF_MIDTAP_RE__M);
		WRB(devAddr, IQM_CF_TAP_RE0__A, sizeof(dk_i_l_lp_taps_re),
		    ((pu8_t) dk_i_l_lp_taps_re));
		WRB(devAddr, IQM_CF_TAP_IM0__A, sizeof(dk_i_l_lp_taps_im),
		    ((pu8_t) dk_i_l_lp_taps_im));
		WR16(devAddr, ATV_TOP_CR_AMP_TH__A, 0x2);	/* TODO check with IS */
		WR16(devAddr, ATV_TOP_CR_CONT__A,
		     (ATV_TOP_CR_CONT_CR_P_LP |
		      ATV_TOP_CR_CONT_CR_D_LP | ATV_TOP_CR_CONT_CR_I_LP));
		WR16(devAddr, ATV_TOP_CR_OVM_TH__A, ATV_TOP_CR_OVM_TH_LP);
		WR16(devAddr, ATV_TOP_STD__A, (ATV_TOP_STD_MODE_LP |
					       ATV_TOP_STD_VID_POL_LP));
		WR16(devAddr, SCU_RAM_ATV_AGC_MODE__A,
		     (SCU_RAM_ATV_AGC_MODE_SIF_STD_SIF_AGC_AM |
		      SCU_RAM_ATV_AGC_MODE_BP_EN_BPC_ENABLE |
		      SCU_RAM_ATV_AGC_MODE_VAGC_VEL_AGC_SLOW));
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_HI__A, 0x1000);
		WR16(devAddr, SCU_RAM_ATV_VID_GAIN_LO__A, 0x0000);
		WR16(devAddr, SCU_RAM_ATV_AMS_MAX_REF__A,
		     SCU_RAM_ATV_AMS_MAX_REF_AMS_MAX_REF_LLP);
		extAttr->phaseCorrectionBypass = FALSE;
		extAttr->atvIfAgcCfg.ctrlMode = DRX_AGC_CTRL_USER;
		extAttr->atvIfAgcCfg.outputLevel = extAttr->atvRfAgcCfg.top;
		extAttr->enableCVBSOutput = TRUE;
		break;
	default:
		return (DRX_STS_ERROR);
	}

	/* Common initializations FM & NTSC & B/G & D/K & I & L & LP */
	if (extAttr->hasLNA == FALSE) {
		WR16(devAddr, IQM_AF_AMUX__A, 0x01);
	}

	WR16(devAddr, SCU_RAM_ATV_STANDARD__A, 0x002);
	WR16(devAddr, IQM_AF_CLP_LEN__A, IQM_AF_CLP_LEN_ATV);
	WR16(devAddr, IQM_AF_CLP_TH__A, IQM_AF_CLP_TH_ATV);
	WR16(devAddr, IQM_AF_SNS_LEN__A, IQM_AF_SNS_LEN_ATV);
	CHK_ERROR(CtrlSetCfgPreSaw(demod, &(extAttr->atvPreSawCfg)));
	WR16(devAddr, IQM_AF_AGC_IF__A, 10248);

	extAttr->iqmRcRateOfs = 0x00200000L;
	WR32(devAddr, IQM_RC_RATE_OFS_LO__A, extAttr->iqmRcRateOfs);
	WR16(devAddr, IQM_RC_ADJ_SEL__A, IQM_RC_ADJ_SEL_B_OFF);
	WR16(devAddr, IQM_RC_STRETCH__A, IQM_RC_STRETCH_ATV);

	WR16(devAddr, IQM_RT_ACTIVE__A, IQM_RT_ACTIVE_ACTIVE_RT_ATV_FCR_ON |
	     IQM_RT_ACTIVE_ACTIVE_CR_ATV_CR_ON);

	WR16(devAddr, IQM_CF_OUT_ENA__A, IQM_CF_OUT_ENA_ATV__M);
	WR16(devAddr, IQM_CF_SYMMETRIC__A, IQM_CF_SYMMETRIC_IM__M);
	/* default: SIF in standby */
	WR16(devAddr, ATV_TOP_SYNC_SLICE__A, ATV_TOP_SYNC_SLICE_MN);
	WR16(devAddr, ATV_TOP_MOD_ACCU__A, ATV_TOP_MOD_ACCU__PRE);

	WR16(devAddr, SCU_RAM_ATV_SIF_GAIN__A, 0x080);
	WR16(devAddr, SCU_RAM_ATV_FAGC_TH_RED__A, 10);
	WR16(devAddr, SCU_RAM_ATV_AAGC_CNT__A, 7);
	WR16(devAddr, SCU_RAM_ATV_NAGC_KI_MIN__A, 0x0225);
	WR16(devAddr, SCU_RAM_ATV_NAGC_KI_MAX__A, 0x0547);
	WR16(devAddr, SCU_RAM_ATV_KI_CHANGE_TH__A, 20);
	WR16(devAddr, SCU_RAM_ATV_LOCK__A, 0);

	WR16(devAddr, IQM_RT_DELAY__A, IQM_RT_DELAY__PRE);
	WR16(devAddr, SCU_RAM_ATV_BPC_KI_MIN__A, 531);
	WR16(devAddr, SCU_RAM_ATV_PAGC_KI_MIN__A, 1061);
	WR16(devAddr, SCU_RAM_ATV_BP_REF_MIN__A, 100);
	WR16(devAddr, SCU_RAM_ATV_BP_REF_MAX__A, 260);
	WR16(devAddr, SCU_RAM_ATV_BP_LVL__A, 0);
	WR16(devAddr, SCU_RAM_ATV_AMS_MAX__A, 0);
	WR16(devAddr, SCU_RAM_ATV_AMS_MIN__A, 2047);
	WR16(devAddr, SCU_RAM_GPIO__A, 0);

	/* Override reset values with current shadow settings */
	CHK_ERROR(AtvUpdateConfig(demod, TRUE));

	/* Configure/restore AGC settings */
	CHK_ERROR(InitAGC(demod));
	CHK_ERROR(SetAgcIf(demod, &(extAttr->atvIfAgcCfg), FALSE));
	CHK_ERROR(SetAgcRf(demod, &(extAttr->atvRfAgcCfg), FALSE));
	CHK_ERROR(CtrlSetCfgPreSaw(demod, &(extAttr->atvPreSawCfg)));

	/* Set SCU ATV substandard,assuming this doesn't require running ATV block */
	cmdSCU.command = SCU_RAM_COMMAND_STANDARD_ATV |
	    SCU_RAM_COMMAND_CMD_DEMOD_SET_ENV;
	cmdSCU.parameterLen = 1;
	cmdSCU.resultLen = 1;
	cmdSCU.parameter = &cmdParam;
	cmdSCU.result = &cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

	/* turn the analog work around on/off (must after set_env b/c it is set in mc) */
	if (extAttr->mfx == 0x03) {
		WR16(devAddr, SCU_RAM_ATV_ENABLE_IIR_WA__A, 0);
	} else {
		WR16(devAddr, SCU_RAM_ATV_ENABLE_IIR_WA__A, 1);
		WR16(devAddr, SCU_RAM_ATV_IIR_CRIT__A, 225);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}
#endif

/* -------------------------------------------------------------------------- */

#ifndef DRXJ_DIGITAL_ONLY
/**
* \fn DRXStatus_t SetATVChannel ()
* \brief Set ATV channel.
* \param demod:   instance of demod.
* \return DRXStatus_t.
*
* Not much needs to be done here, only start the SCU for NTSC/FM.
* Mirrored channels are not expected in the RF domain, so IQM FS setting
* doesn't need to be remembered.
* The channel->mirror parameter is therefor ignored.
*
*/
static DRXStatus_t
SetATVChannel(pDRXDemodInstance_t demod,
	      DRXFrequency_t tunerFreqOffset,
	      pDRXChannel_t channel, DRXStandard_t standard)
{
	DRXJSCUCmd_t cmdSCU = { /* command      */ 0,
		/* parameterLen */ 0,
		/* resultLen    */ 0,
		/* parameter    */ NULL,
		/* result       */ NULL
	};
	u16_t cmdResult = 0;
	pDRXJData_t extAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/*
	   Program frequency shifter
	   No need to account for mirroring on RF
	 */
	if (channel->mirror == DRX_MIRROR_AUTO) {
		extAttr->mirror = DRX_MIRROR_NO;
	} else {
		extAttr->mirror = channel->mirror;
	}

	CHK_ERROR(SetFrequency(demod, channel, tunerFreqOffset));
	WR16(devAddr, ATV_TOP_CR_FREQ__A, ATV_TOP_CR_FREQ__PRE);

	/* Start ATV SCU */
	cmdSCU.command = SCU_RAM_COMMAND_STANDARD_ATV |
	    SCU_RAM_COMMAND_CMD_DEMOD_START;
	cmdSCU.parameterLen = 0;
	cmdSCU.resultLen = 1;
	cmdSCU.parameter = NULL;
	cmdSCU.result = &cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

/*   if ( (extAttr->standard == DRX_STANDARD_FM) && (extAttr->flagSetAUDdone == TRUE) )
   {
      extAttr->detectedRDS = (Bool_t)FALSE;
   }*/

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}
#endif

/* -------------------------------------------------------------------------- */

/**
* \fn DRXStatus_t GetATVChannel ()
* \brief Set ATV channel.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \param standard: NTSC or FM.
* \return DRXStatus_t.
*
* Covers NTSC, PAL/SECAM - B/G, D/K, I, L, LP and FM.
* Computes the frequency offset in te RF domain and adds it to
* channel->frequency. Determines the value for channel->bandwidth.
*
*/
#ifndef DRXJ_DIGITAL_ONLY
static DRXStatus_t
GetATVChannel(pDRXDemodInstance_t demod,
	      pDRXChannel_t channel, DRXStandard_t standard)
{
	DRXFrequency_t offset = 0;
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* Bandwidth */
	channel->bandwidth = ((pDRXJData_t) demod->myExtAttr)->currBandwidth;

	switch (standard) {
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
		{
			u16_t measuredOffset = 0;

			/* get measured frequency offset */
			RR16(devAddr, ATV_TOP_CR_FREQ__A, &measuredOffset);
			/* Signed 8 bit register => sign extension needed */
			if ((measuredOffset & 0x0080) != 0) {
				/* sign extension */
				measuredOffset |= 0xFF80;
			}
			offset +=
			    (DRXFrequency_t) (((s16_t) measuredOffset) * 10);
			break;
		}
	case DRX_STANDARD_PAL_SECAM_LP:
		{
			u16_t measuredOffset = 0;

			/* get measured frequency offset */
			RR16(devAddr, ATV_TOP_CR_FREQ__A, &measuredOffset);
			/* Signed 8 bit register => sign extension needed */
			if ((measuredOffset & 0x0080) != 0) {
				/* sign extension */
				measuredOffset |= 0xFF80;
			}
			offset -=
			    (DRXFrequency_t) (((s16_t) measuredOffset) * 10);
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
		return (DRX_STS_ERROR);
	}

	channel->frequency -= offset;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t GetAtvSigStrength()
* \brief Retrieve signal strength for ATV & FM.
* \param devmod Pointer to demodulator instance.
* \param sigQuality Pointer to signal strength data; range 0, .. , 100.
* \return DRXStatus_t.
* \retval DRX_STS_OK sigStrength contains valid data.
* \retval DRX_STS_ERROR Erroneous data, sigStrength equals 0.
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
static DRXStatus_t
GetAtvSigStrength(pDRXDemodInstance_t demod, pu16_t sigStrength)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	/* All weights must add up to 100 (%)
	   TODO: change weights when IF ctrl is available */
	u32_t digitalWeight = 50;	/* 0 .. 100 */
	u32_t rfWeight = 50;	/* 0 .. 100 */
	u32_t ifWeight = 0;	/* 0 .. 100 */

	u16_t digitalCurrGain = 0;
	u32_t digitalMaxGain = 0;
	u32_t digitalMinGain = 0;
	u16_t rfCurrGain = 0;
	u32_t rfMaxGain = 0x800;	/* taken from ucode */
	u32_t rfMinGain = 0x7fff;
	u16_t ifCurrGain = 0;
	u32_t ifMaxGain = 0x800;	/* taken from ucode */
	u32_t ifMinGain = 0x7fff;

	u32_t digitalStrength = 0;	/* 0.. 100 */
	u32_t rfStrength = 0;	/* 0.. 100 */
	u32_t ifStrength = 0;	/* 0.. 100 */

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	*sigStrength = 0;

	switch (extAttr->standard) {
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:
		SARR16(devAddr, SCU_RAM_ATV_VID_GAIN_HI__A, &digitalCurrGain);
		digitalMaxGain = 22512;	/* taken from ucode */
		digitalMinGain = 2400;	/* taken from ucode */
		break;
	case DRX_STANDARD_FM:
		SARR16(devAddr, SCU_RAM_ATV_SIF_GAIN__A, &digitalCurrGain);
		digitalMaxGain = 0x4ff;	/* taken from ucode */
		digitalMinGain = 0;	/* taken from ucode */
		break;
	default:
		return (DRX_STS_ERROR);
		break;
	}
	RR16(devAddr, IQM_AF_AGC_RF__A, &rfCurrGain);
	RR16(devAddr, IQM_AF_AGC_IF__A, &ifCurrGain);

	/* clipping */
	if (digitalCurrGain >= digitalMaxGain)
		digitalCurrGain = (u16_t) digitalMaxGain;
	if (digitalCurrGain <= digitalMinGain)
		digitalCurrGain = (u16_t) digitalMinGain;
	if (ifCurrGain <= ifMaxGain)
		ifCurrGain = (u16_t) ifMaxGain;
	if (ifCurrGain >= ifMinGain)
		ifCurrGain = (u16_t) ifMinGain;
	if (rfCurrGain <= rfMaxGain)
		rfCurrGain = (u16_t) rfMaxGain;
	if (rfCurrGain >= rfMinGain)
		rfCurrGain = (u16_t) rfMinGain;

	/* TODO: use SCU_RAM_ATV_RAGC_HR__A to shift max and min in case
	   of clipping at ADC */

	/* Compute signal strength (in %) per "gain domain" */

	/* Digital gain  */
	/* TODO: ADC clipping not handled */
	digitalStrength = (100 * (digitalMaxGain - (u32_t) digitalCurrGain)) /
	    (digitalMaxGain - digitalMinGain);

	/* TODO: IF gain not implemented yet in microcode, check after impl. */
	ifStrength = (100 * ((u32_t) ifCurrGain - ifMaxGain)) /
	    (ifMinGain - ifMaxGain);

	/* Rf gain */
	/* TODO: ADC clipping not handled */
	rfStrength = (100 * ((u32_t) rfCurrGain - rfMaxGain)) /
	    (rfMinGain - rfMaxGain);

	/* Compute a weighted signal strength (in %) */
	*sigStrength = (u16_t) (digitalWeight * digitalStrength +
				rfWeight * rfStrength + ifWeight * ifStrength);
	*sigStrength /= 100;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/* -------------------------------------------------------------------------- */
/**
* \fn DRXStatus_t AtvSigQuality()
* \brief Retrieve signal quality indication for ATV.
* \param devmod Pointer to demodulator instance.
* \param sigQuality Pointer to signal quality structure.
* \return DRXStatus_t.
* \retval DRX_STS_OK sigQuality contains valid data.
* \retval DRX_STS_ERROR Erroneous data, sigQuality indicator equals 0.
*
*
*/
static DRXStatus_t
AtvSigQuality(pDRXDemodInstance_t demod, pDRXSigQuality_t sigQuality)
{
	struct i2c_device_addr *devAddr = NULL;
	u16_t qualityIndicator = 0;

	devAddr = demod->myI2CDevAddr;

	/* defined values for fields not used */
	sigQuality->MER = 0;
	sigQuality->preViterbiBER = 0;
	sigQuality->postViterbiBER = 0;
	sigQuality->scaleFactorBER = 1;
	sigQuality->packetError = 0;
	sigQuality->postReedSolomonBER = 0;

	/*
	   Mapping:
	   0x000..0x080: strong signal  => 80% .. 100%
	   0x080..0x700: weak signal    => 30% .. 80%
	   0x700..0x7ff: no signal      => 0%  .. 30%
	 */

	SARR16(devAddr, SCU_RAM_ATV_CR_LOCK__A, &qualityIndicator);
	qualityIndicator &= SCU_RAM_ATV_CR_LOCK_CR_LOCK__M;
	if (qualityIndicator <= 0x80) {
		sigQuality->indicator =
		    80 + ((20 * (0x80 - qualityIndicator)) / 0x80);
	} else if (qualityIndicator <= 0x700) {
		sigQuality->indicator = 30 +
		    ((50 * (0x700 - qualityIndicator)) / (0x700 - 0x81));
	} else {
		sigQuality->indicator =
		    (30 * (0x7FF - qualityIndicator)) / (0x7FF - 0x701);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
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
* \return DRXStatus_t.
*
*/
static DRXStatus_t PowerUpAud(pDRXDemodInstance_t demod, Bool_t setStandard)
{
	DRXAudStandard_t audStandard = DRX_AUD_STANDARD_AUTO;
	struct i2c_device_addr *devAddr = NULL;

	devAddr = demod->myI2CDevAddr;

	WR16(devAddr, AUD_TOP_COMM_EXEC__A, AUD_TOP_COMM_EXEC_ACTIVE);
	/* setup TR interface: R/W mode, fifosize=8 */
	WR16(devAddr, AUD_TOP_TR_MDE__A, 8);
	WR16(devAddr, AUD_COMM_EXEC__A, AUD_COMM_EXEC_ACTIVE);

	if (setStandard == TRUE) {
		CHK_ERROR(AUDCtrlSetStandard(demod, &audStandard));
	}

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/

/**
* \brief Power up AUD.
* \param demod instance of demodulator
* \return DRXStatus_t.
*
*/
static DRXStatus_t PowerDownAud(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	WR16(devAddr, AUD_COMM_EXEC__A, AUD_COMM_EXEC_STOP);

	extAttr->audData.audioIsActive = FALSE;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get Modus data from audio RAM
* \param demod instance of demodulator
* \param pointer to modus
* \return DRXStatus_t.
*
*/
static DRXStatus_t AUDGetModus(pDRXDemodInstance_t demod, pu16_t modus)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	u16_t rModus = 0;
	u16_t rModusHi = 0;
	u16_t rModusLo = 0;

	if (modus == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* Modus register is combined in to RAM location */
	RR16(devAddr, AUD_DEM_RAM_MODUS_HI__A, &rModusHi);
	RR16(devAddr, AUD_DEM_RAM_MODUS_LO__A, &rModusLo);

	rModus = ((rModusHi << 12) & AUD_DEM_RAM_MODUS_HI__M)
	    | (((rModusLo & AUD_DEM_RAM_MODUS_LO__M)));

	*modus = rModus;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;

}

/*============================================================================*/
/**
* \brief Get audio RDS dat
* \param demod instance of demodulator
* \param pointer to DRXCfgAudRDS_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgRDS(pDRXDemodInstance_t demod, pDRXCfgAudRDS_t status)
{
	struct i2c_device_addr *addr = NULL;
	pDRXJData_t extAttr = NULL;

	u16_t rRDSArrayCntInit = 0;
	u16_t rRDSArrayCntCheck = 0;
	u16_t rRDSData = 0;
	u16_t RDSDataCnt = 0;

	addr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	if (status == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	status->valid = FALSE;

	RR16(addr, AUD_DEM_RD_RDS_ARRAY_CNT__A, &rRDSArrayCntInit);

	if (rRDSArrayCntInit ==
	    AUD_DEM_RD_RDS_ARRAY_CNT_RDS_ARRAY_CT_RDS_DATA_NOT_VALID) {
		/* invalid data */
		return DRX_STS_OK;
	}

	if (extAttr->audData.rdsDataCounter == rRDSArrayCntInit) {
		/* no new data */
		return DRX_STS_OK;
	}

	/* RDS is detected, as long as FM radio is selected assume
	   RDS will be available                                    */
	extAttr->audData.rdsDataPresent = TRUE;

	/* new data */
	/* read the data */
	for (RDSDataCnt = 0; RDSDataCnt < AUD_RDS_ARRAY_SIZE; RDSDataCnt++) {
		RR16(addr, AUD_DEM_RD_RDS_DATA__A, &rRDSData);
		status->data[RDSDataCnt] = rRDSData;
	}

	RR16(addr, AUD_DEM_RD_RDS_ARRAY_CNT__A, &rRDSArrayCntCheck);

	if (rRDSArrayCntCheck == rRDSArrayCntInit) {
		status->valid = TRUE;
		extAttr->audData.rdsDataCounter = rRDSArrayCntCheck;
	}

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get the current audio carrier detection status
* \param demod instance of demodulator
* \param pointer to AUDCtrlGetStatus
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCarrierDetectStatus(pDRXDemodInstance_t demod, pDRXAudStatus_t status)
{
	pDRXJData_t extAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;

	u16_t rData = 0;

	if (status == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* initialize the variables */
	status->carrierA = FALSE;
	status->carrierB = FALSE;
	status->nicamStatus = DRX_AUD_NICAM_NOT_DETECTED;
	status->sap = FALSE;
	status->stereo = FALSE;

	/* read stereo sound mode indication */
	RR16(devAddr, AUD_DEM_RD_STATUS__A, &rData);

	/* carrier a detected */
	if ((rData & AUD_DEM_RD_STATUS_STAT_CARR_A__M) ==
	    AUD_DEM_RD_STATUS_STAT_CARR_A_DETECTED) {
		status->carrierA = TRUE;
	}

	/* carrier b detected */
	if ((rData & AUD_DEM_RD_STATUS_STAT_CARR_B__M) ==
	    AUD_DEM_RD_STATUS_STAT_CARR_B_DETECTED) {
		status->carrierB = TRUE;
	}
	/* nicam detected */
	if ((rData & AUD_DEM_RD_STATUS_STAT_NICAM__M) ==
	    AUD_DEM_RD_STATUS_STAT_NICAM_NICAM_DETECTED) {
		if ((rData & AUD_DEM_RD_STATUS_BAD_NICAM__M) ==
		    AUD_DEM_RD_STATUS_BAD_NICAM_OK) {
			status->nicamStatus = DRX_AUD_NICAM_DETECTED;
		} else {
			status->nicamStatus = DRX_AUD_NICAM_BAD;
		}
	}

	/* audio mode bilingual or SAP detected */
	if ((rData & AUD_DEM_RD_STATUS_STAT_BIL_OR_SAP__M) ==
	    AUD_DEM_RD_STATUS_STAT_BIL_OR_SAP_SAP) {
		status->sap = TRUE;
	}

	/* stereo detected */
	if ((rData & AUD_DEM_RD_STATUS_STAT_STEREO__M) ==
	    AUD_DEM_RD_STATUS_STAT_STEREO_STEREO) {
		status->stereo = TRUE;
	}

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get the current audio status parameters
* \param demod instance of demodulator
* \param pointer to AUDCtrlGetStatus
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetStatus(pDRXDemodInstance_t demod, pDRXAudStatus_t status)
{
	pDRXJData_t extAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;
	DRXCfgAudRDS_t rds = { FALSE, {0} };
	u16_t rData = 0;

	if (status == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* carrier detection */
	CHK_ERROR(AUDCtrlGetCarrierDetectStatus(demod, status));

	/* rds data */
	status->rds = FALSE;
	CHK_ERROR(AUDCtrlGetCfgRDS(demod, &rds));
	status->rds = extAttr->audData.rdsDataPresent;

	/* fmIdent */
	RR16(devAddr, AUD_DSP_RD_FM_IDENT_VALUE__A, &rData);
	rData >>= AUD_DSP_RD_FM_IDENT_VALUE_FM_IDENT__B;
	status->fmIdent = (s8_t) rData;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get the current volume settings
* \param demod instance of demodulator
* \param pointer to DRXCfgAudVolume_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgVolume(pDRXDemodInstance_t demod, pDRXCfgAudVolume_t volume)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	u16_t rVolume = 0;
	u16_t rAVC = 0;
	u16_t rStrengthLeft = 0;
	u16_t rStrengthRight = 0;

	if (volume == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* volume */
	volume->mute = extAttr->audData.volume.mute;
	RR16(devAddr, AUD_DSP_WR_VOLUME__A, &rVolume);
	if (rVolume == 0) {
		volume->mute = TRUE;
		volume->volume = extAttr->audData.volume.volume;
	} else {
		volume->mute = FALSE;
		volume->volume = ((rVolume & AUD_DSP_WR_VOLUME_VOL_MAIN__M) >>
				  AUD_DSP_WR_VOLUME_VOL_MAIN__B) -
		    AUD_VOLUME_ZERO_DB;
		if (volume->volume < AUD_VOLUME_DB_MIN) {
			volume->volume = AUD_VOLUME_DB_MIN;
		}
		if (volume->volume > AUD_VOLUME_DB_MAX) {
			volume->volume = AUD_VOLUME_DB_MAX;
		}
	}

	/* automatic volume control */
	RR16(devAddr, AUD_DSP_WR_AVC__A, &rAVC);

	if ((rAVC & AUD_DSP_WR_AVC_AVC_ON__M) == AUD_DSP_WR_AVC_AVC_ON_OFF)
	{
		volume->avcMode = DRX_AUD_AVC_OFF;
	} else {
		switch (rAVC & AUD_DSP_WR_AVC_AVC_DECAY__M) {
		case AUD_DSP_WR_AVC_AVC_DECAY_20_MSEC:
			volume->avcMode = DRX_AUD_AVC_DECAYTIME_20MS;
			break;
		case AUD_DSP_WR_AVC_AVC_DECAY_8_SEC:
			volume->avcMode = DRX_AUD_AVC_DECAYTIME_8S;
			break;
		case AUD_DSP_WR_AVC_AVC_DECAY_4_SEC:
			volume->avcMode = DRX_AUD_AVC_DECAYTIME_4S;
			break;
		case AUD_DSP_WR_AVC_AVC_DECAY_2_SEC:
			volume->avcMode = DRX_AUD_AVC_DECAYTIME_2S;
			break;
		default:
			return DRX_STS_ERROR;
			break;
		}
	}

	/* max attenuation */
	switch (rAVC & AUD_DSP_WR_AVC_AVC_MAX_ATT__M) {
	case AUD_DSP_WR_AVC_AVC_MAX_ATT_12DB:
		volume->avcMaxAtten = DRX_AUD_AVC_MAX_ATTEN_12DB;
		break;
	case AUD_DSP_WR_AVC_AVC_MAX_ATT_18DB:
		volume->avcMaxAtten = DRX_AUD_AVC_MAX_ATTEN_18DB;
		break;
	case AUD_DSP_WR_AVC_AVC_MAX_ATT_24DB:
		volume->avcMaxAtten = DRX_AUD_AVC_MAX_ATTEN_24DB;
		break;
	default:
		return DRX_STS_ERROR;
		break;
	}

	/* max gain */
	switch (rAVC & AUD_DSP_WR_AVC_AVC_MAX_GAIN__M) {
	case AUD_DSP_WR_AVC_AVC_MAX_GAIN_0DB:
		volume->avcMaxGain = DRX_AUD_AVC_MAX_GAIN_0DB;
		break;
	case AUD_DSP_WR_AVC_AVC_MAX_GAIN_6DB:
		volume->avcMaxGain = DRX_AUD_AVC_MAX_GAIN_6DB;
		break;
	case AUD_DSP_WR_AVC_AVC_MAX_GAIN_12DB:
		volume->avcMaxGain = DRX_AUD_AVC_MAX_GAIN_12DB;
		break;
	default:
		return DRX_STS_ERROR;
		break;
	}

	/* reference level */
	volume->avcRefLevel = (u16_t) ((rAVC & AUD_DSP_WR_AVC_AVC_REF_LEV__M) >>
				       AUD_DSP_WR_AVC_AVC_REF_LEV__B);

	/* read qpeak registers and calculate strength of left and right carrier */
	/* quasi peaks formula: QP(dB) = 20 * log( AUD_DSP_RD_QPEAKx / Q(0dB) */
	/* Q(0dB) represents QP value of 0dB (hex value 0x4000) */
	/* left carrier */

	/* QP vaues */
	/* left carrier */
	RR16(devAddr, AUD_DSP_RD_QPEAK_L__A, &rStrengthLeft);
	volume->strengthLeft = (((s16_t) Log10Times100(rStrengthLeft)) -
				AUD_CARRIER_STRENGTH_QP_0DB_LOG10T100) / 5;

	/* right carrier */
	RR16(devAddr, AUD_DSP_RD_QPEAK_R__A, &rStrengthRight);
	volume->strengthRight = (((s16_t) Log10Times100(rStrengthRight)) -
				 AUD_CARRIER_STRENGTH_QP_0DB_LOG10T100) / 5;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Set the current volume settings
* \param demod instance of demodulator
* \param pointer to DRXCfgAudVolume_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlSetCfgVolume(pDRXDemodInstance_t demod, pDRXCfgAudVolume_t volume)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	u16_t wVolume = 0;
	u16_t wAVC = 0;

	if (volume == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* volume */
	/* volume range from -60 to 12 (expressed in dB) */
	if ((volume->volume < AUD_VOLUME_DB_MIN) ||
	    (volume->volume > AUD_VOLUME_DB_MAX)) {
		return DRX_STS_INVALID_ARG;
	}

	RR16(devAddr, AUD_DSP_WR_VOLUME__A, &wVolume);

	/* clear the volume mask */
	wVolume &= (u16_t) ~ AUD_DSP_WR_VOLUME_VOL_MAIN__M;
	if (volume->mute == TRUE) {
		/* mute */
		/* mute overrules volume */
		wVolume |= (u16_t) (0);

	} else {
		wVolume |= (u16_t) ((volume->volume + AUD_VOLUME_ZERO_DB) <<
				    AUD_DSP_WR_VOLUME_VOL_MAIN__B);
	}

	WR16(devAddr, AUD_DSP_WR_VOLUME__A, wVolume);

	/* automatic volume control */
	RR16(devAddr, AUD_DSP_WR_AVC__A, &wAVC);

	/* clear masks that require writing */
	wAVC &= (u16_t) ~ AUD_DSP_WR_AVC_AVC_ON__M;
	wAVC &= (u16_t) ~ AUD_DSP_WR_AVC_AVC_DECAY__M;

	if (volume->avcMode == DRX_AUD_AVC_OFF) {
		wAVC |= (AUD_DSP_WR_AVC_AVC_ON_OFF);
	} else {

		wAVC |= (AUD_DSP_WR_AVC_AVC_ON_ON);

		/* avc decay */
		switch (volume->avcMode) {
		case DRX_AUD_AVC_DECAYTIME_20MS:
			wAVC |= AUD_DSP_WR_AVC_AVC_DECAY_20_MSEC;
			break;
		case DRX_AUD_AVC_DECAYTIME_8S:
			wAVC |= AUD_DSP_WR_AVC_AVC_DECAY_8_SEC;
			break;
		case DRX_AUD_AVC_DECAYTIME_4S:
			wAVC |= AUD_DSP_WR_AVC_AVC_DECAY_4_SEC;
			break;
		case DRX_AUD_AVC_DECAYTIME_2S:
			wAVC |= AUD_DSP_WR_AVC_AVC_DECAY_2_SEC;
			break;
		default:
			return DRX_STS_INVALID_ARG;
		}
	}

	/* max attenuation */
	wAVC &= (u16_t) ~ AUD_DSP_WR_AVC_AVC_MAX_ATT__M;
	switch (volume->avcMaxAtten) {
	case DRX_AUD_AVC_MAX_ATTEN_12DB:
		wAVC |= AUD_DSP_WR_AVC_AVC_MAX_ATT_12DB;
		break;
	case DRX_AUD_AVC_MAX_ATTEN_18DB:
		wAVC |= AUD_DSP_WR_AVC_AVC_MAX_ATT_18DB;
		break;
	case DRX_AUD_AVC_MAX_ATTEN_24DB:
		wAVC |= AUD_DSP_WR_AVC_AVC_MAX_ATT_24DB;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	/* max gain */
	wAVC &= (u16_t) ~ AUD_DSP_WR_AVC_AVC_MAX_GAIN__M;
	switch (volume->avcMaxGain) {
	case DRX_AUD_AVC_MAX_GAIN_0DB:
		wAVC |= AUD_DSP_WR_AVC_AVC_MAX_GAIN_0DB;
		break;
	case DRX_AUD_AVC_MAX_GAIN_6DB:
		wAVC |= AUD_DSP_WR_AVC_AVC_MAX_GAIN_6DB;
		break;
	case DRX_AUD_AVC_MAX_GAIN_12DB:
		wAVC |= AUD_DSP_WR_AVC_AVC_MAX_GAIN_12DB;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	/* avc reference level */
	if (volume->avcRefLevel > AUD_MAX_AVC_REF_LEVEL) {
		return DRX_STS_INVALID_ARG;
	}

	wAVC &= (u16_t) ~ AUD_DSP_WR_AVC_AVC_REF_LEV__M;
	wAVC |= (u16_t) (volume->avcRefLevel << AUD_DSP_WR_AVC_AVC_REF_LEV__B);

	WR16(devAddr, AUD_DSP_WR_AVC__A, wAVC);

	/* all done, store config in data structure */
	extAttr->audData.volume = *volume;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get the I2S settings
* \param demod instance of demodulator
* \param pointer to DRXCfgI2SOutput_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgOutputI2S(pDRXDemodInstance_t demod, pDRXCfgI2SOutput_t output)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	u16_t wI2SConfig = 0;
	u16_t rI2SFreq = 0;

	if (output == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	RR16(devAddr, AUD_DEM_RAM_I2S_CONFIG2__A, &wI2SConfig);
	RR16(devAddr, AUD_DSP_WR_I2S_OUT_FS__A, &rI2SFreq);

	/* I2S mode */
	switch (wI2SConfig & AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST__M) {
	case AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST_MASTER:
		output->mode = DRX_I2S_MODE_MASTER;
		break;
	case AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST_SLAVE:
		output->mode = DRX_I2S_MODE_SLAVE;
		break;
	default:
		return DRX_STS_ERROR;
	}

	/* I2S format */
	switch (wI2SConfig & AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE__M) {
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE_DELAY:
		output->format = DRX_I2S_FORMAT_WS_ADVANCED;
		break;
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE_NO_DELAY:
		output->format = DRX_I2S_FORMAT_WS_WITH_DATA;
		break;
	default:
		return DRX_STS_ERROR;
	}

	/* I2S word length */
	switch (wI2SConfig & AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN__M) {
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN_BIT_16:
		output->wordLength = DRX_I2S_WORDLENGTH_16;
		break;
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN_BIT_32:
		output->wordLength = DRX_I2S_WORDLENGTH_32;
		break;
	default:
		return DRX_STS_ERROR;
	}

	/* I2S polarity */
	switch (wI2SConfig & AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL__M) {
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL_LEFT_HIGH:
		output->polarity = DRX_I2S_POLARITY_LEFT;
		break;
	case AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL_LEFT_LOW:
		output->polarity = DRX_I2S_POLARITY_RIGHT;
		break;
	default:
		return DRX_STS_ERROR;
	}

	/* I2S output enabled */
	if ((wI2SConfig & AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE__M)
	    == AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE_ENABLE) {
		output->outputEnable = TRUE;
	} else {
		output->outputEnable = FALSE;
	}

	if (rI2SFreq > 0) {
		output->frequency = 6144UL * 48000 / rI2SFreq;
		if (output->wordLength == DRX_I2S_WORDLENGTH_16) {
			output->frequency *= 2;
		}
	} else {
		output->frequency = AUD_I2S_FREQUENCY_MAX;
	}

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Set the I2S settings
* \param demod instance of demodulator
* \param pointer to DRXCfgI2SOutput_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlSetCfgOutputI2S(pDRXDemodInstance_t demod, pDRXCfgI2SOutput_t output)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	u16_t wI2SConfig = 0;
	u16_t wI2SPadsDataDa = 0;
	u16_t wI2SPadsDataCl = 0;
	u16_t wI2SPadsDataWs = 0;
	u32_t wI2SFreq = 0;

	if (output == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	RR16(devAddr, AUD_DEM_RAM_I2S_CONFIG2__A, &wI2SConfig);

	/* I2S mode */
	wI2SConfig &= (u16_t) ~ AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST__M;

	switch (output->mode) {
	case DRX_I2S_MODE_MASTER:
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST_MASTER;
		break;
	case DRX_I2S_MODE_SLAVE:
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_SLV_MST_SLAVE;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	/* I2S format */
	wI2SConfig &= (u16_t) ~ AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE__M;

	switch (output->format) {
	case DRX_I2S_FORMAT_WS_ADVANCED:
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE_DELAY;
		break;
	case DRX_I2S_FORMAT_WS_WITH_DATA:
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_WS_MODE_NO_DELAY;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	/* I2S word length */
	wI2SConfig &= (u16_t) ~ AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN__M;

	switch (output->wordLength) {
	case DRX_I2S_WORDLENGTH_16:
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN_BIT_16;
		break;
	case DRX_I2S_WORDLENGTH_32:
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_WORD_LEN_BIT_32;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	/* I2S polarity */
	wI2SConfig &= (u16_t) ~ AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL__M;
	switch (output->polarity) {
	case DRX_I2S_POLARITY_LEFT:
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL_LEFT_HIGH;
		break;
	case DRX_I2S_POLARITY_RIGHT:
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_WS_POL_LEFT_LOW;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	/* I2S output enabled */
	wI2SConfig &= (u16_t) ~ AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE__M;
	if (output->outputEnable == TRUE) {
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE_ENABLE;
	} else {
		wI2SConfig |= AUD_DEM_WR_I2S_CONFIG2_I2S_ENABLE_DISABLE;
	}

	/*
	   I2S frequency

	   wI2SFreq = 6144 * 48000 * nrbits / ( 32 * frequency )

	   16bit: 6144 * 48000 / ( 2 * freq ) = ( 6144 * 48000 / freq ) / 2
	   32bit: 6144 * 48000 / freq         = ( 6144 * 48000 / freq )
	 */
	if ((output->frequency > AUD_I2S_FREQUENCY_MAX) ||
	    output->frequency < AUD_I2S_FREQUENCY_MIN) {
		return DRX_STS_INVALID_ARG;
	}

	wI2SFreq = (6144UL * 48000UL) + (output->frequency >> 1);
	wI2SFreq /= output->frequency;

	if (output->wordLength == DRX_I2S_WORDLENGTH_16) {
		wI2SFreq *= 2;
	}

	WR16(devAddr, AUD_DEM_WR_I2S_CONFIG2__A, wI2SConfig);
	WR16(devAddr, AUD_DSP_WR_I2S_OUT_FS__A, (u16_t) wI2SFreq);

	/* configure I2S output pads for master or slave mode */
	WR16(devAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);

	if (output->mode == DRX_I2S_MODE_MASTER) {
		wI2SPadsDataDa = SIO_PDR_I2S_DA_CFG_MODE__MASTER |
		    SIO_PDR_I2S_DA_CFG_DRIVE__MASTER;
		wI2SPadsDataCl = SIO_PDR_I2S_CL_CFG_MODE__MASTER |
		    SIO_PDR_I2S_CL_CFG_DRIVE__MASTER;
		wI2SPadsDataWs = SIO_PDR_I2S_WS_CFG_MODE__MASTER |
		    SIO_PDR_I2S_WS_CFG_DRIVE__MASTER;
	} else {
		wI2SPadsDataDa = SIO_PDR_I2S_DA_CFG_MODE__SLAVE |
		    SIO_PDR_I2S_DA_CFG_DRIVE__SLAVE;
		wI2SPadsDataCl = SIO_PDR_I2S_CL_CFG_MODE__SLAVE |
		    SIO_PDR_I2S_CL_CFG_DRIVE__SLAVE;
		wI2SPadsDataWs = SIO_PDR_I2S_WS_CFG_MODE__SLAVE |
		    SIO_PDR_I2S_WS_CFG_DRIVE__SLAVE;
	}

	WR16(devAddr, SIO_PDR_I2S_DA_CFG__A, wI2SPadsDataDa);
	WR16(devAddr, SIO_PDR_I2S_CL_CFG__A, wI2SPadsDataCl);
	WR16(devAddr, SIO_PDR_I2S_WS_CFG__A, wI2SPadsDataWs);

	WR16(devAddr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY__PRE);

	/* all done, store config in data structure */
	extAttr->audData.i2sdata = *output;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get the Automatic Standard Select (ASS)
*        and Automatic Sound Change (ASC)
* \param demod instance of demodulator
* \param pointer to pDRXAudAutoSound_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgAutoSound(pDRXDemodInstance_t demod,
		       pDRXCfgAudAutoSound_t autoSound)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t rModus = 0;

	if (autoSound == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	CHK_ERROR(AUDGetModus(demod, &rModus));

	switch (rModus & (AUD_DEM_WR_MODUS_MOD_ASS__M |
			  AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG__M)) {
	case AUD_DEM_WR_MODUS_MOD_ASS_OFF | AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_DISABLED:
	case AUD_DEM_WR_MODUS_MOD_ASS_OFF | AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_ENABLED:
		*autoSound =
		    DRX_AUD_AUTO_SOUND_OFF;
		break;
	case AUD_DEM_WR_MODUS_MOD_ASS_ON | AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_ENABLED:
		*autoSound =
		    DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_ON;
		break;
	case AUD_DEM_WR_MODUS_MOD_ASS_ON | AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_DISABLED:
		*autoSound =
		    DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_OFF;
		break;
	default:
		return DRX_STS_ERROR;
	}

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Set the Automatic Standard Select (ASS)
*        and Automatic Sound Change (ASC)
* \param demod instance of demodulator
* \param pointer to pDRXAudAutoSound_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrSetlCfgAutoSound(pDRXDemodInstance_t demod,
		       pDRXCfgAudAutoSound_t autoSound)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t rModus = 0;
	u16_t wModus = 0;

	if (autoSound == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	CHK_ERROR(AUDGetModus(demod, &rModus));

	wModus = rModus;
	/* clear ASS & ASC bits */
	wModus &= (u16_t) ~ AUD_DEM_WR_MODUS_MOD_ASS__M;
	wModus &= (u16_t) ~ AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG__M;

	switch (*autoSound) {
	case DRX_AUD_AUTO_SOUND_OFF:
		wModus |= AUD_DEM_WR_MODUS_MOD_ASS_OFF;
		wModus |= AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_DISABLED;
		break;
	case DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_ON:
		wModus |= AUD_DEM_WR_MODUS_MOD_ASS_ON;
		wModus |= AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_ENABLED;
		break;
	case DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_OFF:
		wModus |= AUD_DEM_WR_MODUS_MOD_ASS_ON;
		wModus |= AUD_DEM_WR_MODUS_MOD_DIS_STD_CHG_DISABLED;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	if (wModus != rModus) {
		WR16(devAddr, AUD_DEM_WR_MODUS__A, wModus);
	}
	/* copy to data structure */
	extAttr->audData.autoSound = *autoSound;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get the Automatic Standard Select thresholds
* \param demod instance of demodulator
* \param pointer to pDRXAudASSThres_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgASSThres(pDRXDemodInstance_t demod, pDRXCfgAudASSThres_t thres)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t thresA2 = 0;
	u16_t thresBtsc = 0;
	u16_t thresNicam = 0;

	if (thres == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	RR16(devAddr, AUD_DEM_RAM_A2_THRSHLD__A, &thresA2);
	RR16(devAddr, AUD_DEM_RAM_BTSC_THRSHLD__A, &thresBtsc);
	RR16(devAddr, AUD_DEM_RAM_NICAM_THRSHLD__A, &thresNicam);

	thres->a2 = thresA2;
	thres->btsc = thresBtsc;
	thres->nicam = thresNicam;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get the Automatic Standard Select thresholds
* \param demod instance of demodulator
* \param pointer to pDRXAudASSThres_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlSetCfgASSThres(pDRXDemodInstance_t demod, pDRXCfgAudASSThres_t thres)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	if (thres == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	WR16(devAddr, AUD_DEM_WR_A2_THRSHLD__A, thres->a2);
	WR16(devAddr, AUD_DEM_WR_BTSC_THRSHLD__A, thres->btsc);
	WR16(devAddr, AUD_DEM_WR_NICAM_THRSHLD__A, thres->nicam);

	/* update DRXK data structure with hardware values */
	extAttr->audData.assThresholds = *thres;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get Audio Carrier settings
* \param demod instance of demodulator
* \param pointer to pDRXAudCarrier_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgCarrier(pDRXDemodInstance_t demod, pDRXCfgAudCarriers_t carriers)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t wModus = 0;

	u16_t dcoAHi = 0;
	u16_t dcoALo = 0;
	u16_t dcoBHi = 0;
	u16_t dcoBLo = 0;

	u32_t valA = 0;
	u32_t valB = 0;

	u16_t dcLvlA = 0;
	u16_t dcLvlB = 0;

	u16_t cmThesA = 0;
	u16_t cmThesB = 0;

	if (carriers == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	CHK_ERROR(AUDGetModus(demod, &wModus));

	/* Behaviour of primary audio channel */
	switch (wModus & (AUD_DEM_WR_MODUS_MOD_CM_A__M)) {
	case AUD_DEM_WR_MODUS_MOD_CM_A_MUTE:
		carriers->a.opt = DRX_NO_CARRIER_MUTE;
		break;
	case AUD_DEM_WR_MODUS_MOD_CM_A_NOISE:
		carriers->a.opt = DRX_NO_CARRIER_NOISE;
		break;
	default:
		return DRX_STS_ERROR;
		break;
	}

	/* Behaviour of secondary audio channel */
	switch (wModus & (AUD_DEM_WR_MODUS_MOD_CM_B__M)) {
	case AUD_DEM_WR_MODUS_MOD_CM_B_MUTE:
		carriers->b.opt = DRX_NO_CARRIER_MUTE;
		break;
	case AUD_DEM_WR_MODUS_MOD_CM_B_NOISE:
		carriers->b.opt = DRX_NO_CARRIER_NOISE;
		break;
	default:
		return DRX_STS_ERROR;
		break;
	}

	/* frequency adjustment for primary & secondary audio channel */
	RR16(devAddr, AUD_DEM_RAM_DCO_A_HI__A, &dcoAHi);
	RR16(devAddr, AUD_DEM_RAM_DCO_A_LO__A, &dcoALo);
	RR16(devAddr, AUD_DEM_RAM_DCO_B_HI__A, &dcoBHi);
	RR16(devAddr, AUD_DEM_RAM_DCO_B_LO__A, &dcoBLo);

	valA = (((u32_t) dcoAHi) << 12) | ((u32_t) dcoALo & 0xFFF);
	valB = (((u32_t) dcoBHi) << 12) | ((u32_t) dcoBLo & 0xFFF);

	/* Multiply by 20250 * 1>>24  ~= 2 / 1657 */
	carriers->a.dco = DRX_S24TODRXFREQ(valA) * 2L / 1657L;
	carriers->b.dco = DRX_S24TODRXFREQ(valB) * 2L / 1657L;

	/* DC level of the incoming FM signal on the primary
	   & seconday sound channel */
	RR16(devAddr, AUD_DSP_RD_FM_DC_LEVEL_A__A, &dcLvlA);
	RR16(devAddr, AUD_DSP_RD_FM_DC_LEVEL_B__A, &dcLvlB);

	/* offset (kHz) = (dcLvl / 322) */
	carriers->a.shift = (DRX_U16TODRXFREQ(dcLvlA) / 322L);
	carriers->b.shift = (DRX_U16TODRXFREQ(dcLvlB) / 322L);

	/* Carrier detetcion threshold for primary & secondary channel */
	RR16(devAddr, AUD_DEM_RAM_CM_A_THRSHLD__A, &cmThesA);
	RR16(devAddr, AUD_DEM_RAM_CM_B_THRSHLD__A, &cmThesB);

	carriers->a.thres = cmThesA;
	carriers->b.thres = cmThesB;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Set Audio Carrier settings
* \param demod instance of demodulator
* \param pointer to pDRXAudCarrier_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlSetCfgCarrier(pDRXDemodInstance_t demod, pDRXCfgAudCarriers_t carriers)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t wModus = 0;
	u16_t rModus = 0;

	u16_t dcoAHi = 0;
	u16_t dcoALo = 0;
	u16_t dcoBHi = 0;
	u16_t dcoBLo = 0;

	s32_t valA = 0;
	s32_t valB = 0;

	if (carriers == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	CHK_ERROR(AUDGetModus(demod, &rModus));

	wModus = rModus;
	wModus &= (u16_t) ~ AUD_DEM_WR_MODUS_MOD_CM_A__M;
	/* Behaviour of primary audio channel */
	switch (carriers->a.opt) {
	case DRX_NO_CARRIER_MUTE:
		wModus |= AUD_DEM_WR_MODUS_MOD_CM_A_MUTE;
		break;
	case DRX_NO_CARRIER_NOISE:
		wModus |= AUD_DEM_WR_MODUS_MOD_CM_A_NOISE;
		break;
	default:
		return DRX_STS_INVALID_ARG;
		break;
	}

	/* Behaviour of secondary audio channel */
	wModus &= (u16_t) ~ AUD_DEM_WR_MODUS_MOD_CM_B__M;
	switch (carriers->b.opt) {
	case DRX_NO_CARRIER_MUTE:
		wModus |= AUD_DEM_WR_MODUS_MOD_CM_B_MUTE;
		break;
	case DRX_NO_CARRIER_NOISE:
		wModus |= AUD_DEM_WR_MODUS_MOD_CM_B_NOISE;
		break;
	default:
		return DRX_STS_INVALID_ARG;
		break;
	}

	/* now update the modus register */
	if (wModus != rModus) {
		WR16(devAddr, AUD_DEM_WR_MODUS__A, wModus);
	}

	/* frequency adjustment for primary & secondary audio channel */
	valA = (s32_t) ((carriers->a.dco) * 1657L / 2);
	valB = (s32_t) ((carriers->b.dco) * 1657L / 2);

	dcoAHi = (u16_t) ((valA >> 12) & 0xFFF);
	dcoALo = (u16_t) (valA & 0xFFF);
	dcoBHi = (u16_t) ((valB >> 12) & 0xFFF);
	dcoBLo = (u16_t) (valB & 0xFFF);

	WR16(devAddr, AUD_DEM_WR_DCO_A_HI__A, dcoAHi);
	WR16(devAddr, AUD_DEM_WR_DCO_A_LO__A, dcoALo);
	WR16(devAddr, AUD_DEM_WR_DCO_B_HI__A, dcoBHi);
	WR16(devAddr, AUD_DEM_WR_DCO_B_LO__A, dcoBLo);

	/* Carrier detetcion threshold for primary & secondary channel */
	WR16(devAddr, AUD_DEM_WR_CM_A_THRSHLD__A, carriers->a.thres);
	WR16(devAddr, AUD_DEM_WR_CM_B_THRSHLD__A, carriers->b.thres);

	/* update DRXK data structure */
	extAttr->audData.carriers = *carriers;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get I2S Source, I2S matrix and FM matrix
* \param demod instance of demodulator
* \param pointer to pDRXAudmixer_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgMixer(pDRXDemodInstance_t demod, pDRXCfgAudMixer_t mixer)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t srcI2SMatr = 0;
	u16_t fmMatr = 0;

	if (mixer == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* Source Selctor */
	RR16(devAddr, AUD_DSP_WR_SRC_I2S_MATR__A, &srcI2SMatr);

	switch (srcI2SMatr & AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S__M) {
	case AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_MONO:
		mixer->sourceI2S = DRX_AUD_SRC_MONO;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_AB:
		mixer->sourceI2S = DRX_AUD_SRC_STEREO_OR_AB;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_A:
		mixer->sourceI2S = DRX_AUD_SRC_STEREO_OR_A;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_B:
		mixer->sourceI2S = DRX_AUD_SRC_STEREO_OR_B;
		break;
	default:
		return DRX_STS_ERROR;
	}

	/* Matrix */
	switch (srcI2SMatr & AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S__M) {
	case AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_MONO:
		mixer->matrixI2S = DRX_AUD_I2S_MATRIX_MONO;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_STEREO:
		mixer->matrixI2S = DRX_AUD_I2S_MATRIX_STEREO;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_SOUND_A:
		mixer->matrixI2S = DRX_AUD_I2S_MATRIX_A_MONO;
		break;
	case AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_SOUND_B:
		mixer->matrixI2S = DRX_AUD_I2S_MATRIX_B_MONO;
		break;
	default:
		return DRX_STS_ERROR;
	}

	/* FM Matrix */
	RR16(devAddr, AUD_DEM_WR_FM_MATRIX__A, &fmMatr);
	switch (fmMatr & AUD_DEM_WR_FM_MATRIX__M) {
	case AUD_DEM_WR_FM_MATRIX_NO_MATRIX:
		mixer->matrixFm = DRX_AUD_FM_MATRIX_NO_MATRIX;
		break;
	case AUD_DEM_WR_FM_MATRIX_GERMAN_MATRIX:
		mixer->matrixFm = DRX_AUD_FM_MATRIX_GERMAN;
		break;
	case AUD_DEM_WR_FM_MATRIX_KOREAN_MATRIX:
		mixer->matrixFm = DRX_AUD_FM_MATRIX_KOREAN;
		break;
	case AUD_DEM_WR_FM_MATRIX_SOUND_A:
		mixer->matrixFm = DRX_AUD_FM_MATRIX_SOUND_A;
		break;
	case AUD_DEM_WR_FM_MATRIX_SOUND_B:
		mixer->matrixFm = DRX_AUD_FM_MATRIX_SOUND_B;
		break;
	default:
		return DRX_STS_ERROR;
	}

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Set I2S Source, I2S matrix and FM matrix
* \param demod instance of demodulator
* \param pointer to DRXAudmixer_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlSetCfgMixer(pDRXDemodInstance_t demod, pDRXCfgAudMixer_t mixer)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t srcI2SMatr = 0;
	u16_t fmMatr = 0;

	if (mixer == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* Source Selctor */
	RR16(devAddr, AUD_DSP_WR_SRC_I2S_MATR__A, &srcI2SMatr);
	srcI2SMatr &= (u16_t) ~ AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S__M;

	switch (mixer->sourceI2S) {
	case DRX_AUD_SRC_MONO:
		srcI2SMatr |= AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_MONO;
		break;
	case DRX_AUD_SRC_STEREO_OR_AB:
		srcI2SMatr |= AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_AB;
		break;
	case DRX_AUD_SRC_STEREO_OR_A:
		srcI2SMatr |= AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_A;
		break;
	case DRX_AUD_SRC_STEREO_OR_B:
		srcI2SMatr |= AUD_DSP_WR_SRC_I2S_MATR_SRC_I2S_STEREO_B;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	/* Matrix */
	srcI2SMatr &= (u16_t) ~ AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S__M;
	switch (mixer->matrixI2S) {
	case DRX_AUD_I2S_MATRIX_MONO:
		srcI2SMatr |= AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_MONO;
		break;
	case DRX_AUD_I2S_MATRIX_STEREO:
		srcI2SMatr |= AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_STEREO;
		break;
	case DRX_AUD_I2S_MATRIX_A_MONO:
		srcI2SMatr |= AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_SOUND_A;
		break;
	case DRX_AUD_I2S_MATRIX_B_MONO:
		srcI2SMatr |= AUD_DSP_WR_SRC_I2S_MATR_MAT_I2S_SOUND_B;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}
	/* write the result */
	WR16(devAddr, AUD_DSP_WR_SRC_I2S_MATR__A, srcI2SMatr);

	/* FM Matrix */
	RR16(devAddr, AUD_DEM_WR_FM_MATRIX__A, &fmMatr);
	fmMatr &= (u16_t) ~ AUD_DEM_WR_FM_MATRIX__M;
	switch (mixer->matrixFm) {
	case DRX_AUD_FM_MATRIX_NO_MATRIX:
		fmMatr |= AUD_DEM_WR_FM_MATRIX_NO_MATRIX;
		break;
	case DRX_AUD_FM_MATRIX_GERMAN:
		fmMatr |= AUD_DEM_WR_FM_MATRIX_GERMAN_MATRIX;
		break;
	case DRX_AUD_FM_MATRIX_KOREAN:
		fmMatr |= AUD_DEM_WR_FM_MATRIX_KOREAN_MATRIX;
		break;
	case DRX_AUD_FM_MATRIX_SOUND_A:
		fmMatr |= AUD_DEM_WR_FM_MATRIX_SOUND_A;
		break;
	case DRX_AUD_FM_MATRIX_SOUND_B:
		fmMatr |= AUD_DEM_WR_FM_MATRIX_SOUND_B;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	/* Only write if ASS is off */
	if (extAttr->audData.autoSound == DRX_AUD_AUTO_SOUND_OFF) {
		WR16(devAddr, AUD_DEM_WR_FM_MATRIX__A, fmMatr);
	}

	/* update the data structure with hardware state */
	extAttr->audData.mixer = *mixer;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Set AV Sync settings
* \param demod instance of demodulator
* \param pointer to DRXICfgAVSync_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlSetCfgAVSync(pDRXDemodInstance_t demod, pDRXCfgAudAVSync_t avSync)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t wAudVidSync = 0;

	if (avSync == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* audio/video synchronisation */
	RR16(devAddr, AUD_DSP_WR_AV_SYNC__A, &wAudVidSync);

	wAudVidSync &= (u16_t) ~ AUD_DSP_WR_AV_SYNC_AV_ON__M;

	if (*avSync == DRX_AUD_AVSYNC_OFF) {
		wAudVidSync |= AUD_DSP_WR_AV_SYNC_AV_ON_DISABLE;
	} else {
		wAudVidSync |= AUD_DSP_WR_AV_SYNC_AV_ON_ENABLE;
	}

	wAudVidSync &= (u16_t) ~ AUD_DSP_WR_AV_SYNC_AV_STD_SEL__M;

	switch (*avSync) {
	case DRX_AUD_AVSYNC_NTSC:
		wAudVidSync |= AUD_DSP_WR_AV_SYNC_AV_STD_SEL_NTSC;
		break;
	case DRX_AUD_AVSYNC_MONOCHROME:
		wAudVidSync |= AUD_DSP_WR_AV_SYNC_AV_STD_SEL_MONOCHROME;
		break;
	case DRX_AUD_AVSYNC_PAL_SECAM:
		wAudVidSync |= AUD_DSP_WR_AV_SYNC_AV_STD_SEL_PAL_SECAM;
		break;
	case DRX_AUD_AVSYNC_OFF:
		/* OK */
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	WR16(devAddr, AUD_DSP_WR_AV_SYNC__A, wAudVidSync);
	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get AV Sync settings
* \param demod instance of demodulator
* \param pointer to DRXICfgAVSync_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgAVSync(pDRXDemodInstance_t demod, pDRXCfgAudAVSync_t avSync)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t wAudVidSync = 0;

	if (avSync == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* audio/video synchronisation */
	RR16(devAddr, AUD_DSP_WR_AV_SYNC__A, &wAudVidSync);

	if ((wAudVidSync & AUD_DSP_WR_AV_SYNC_AV_ON__M) ==
	    AUD_DSP_WR_AV_SYNC_AV_ON_DISABLE) {
		*avSync = DRX_AUD_AVSYNC_OFF;
		return DRX_STS_OK;
	}

	switch (wAudVidSync & AUD_DSP_WR_AV_SYNC_AV_STD_SEL__M) {
	case AUD_DSP_WR_AV_SYNC_AV_STD_SEL_NTSC:
		*avSync = DRX_AUD_AVSYNC_NTSC;
		break;
	case AUD_DSP_WR_AV_SYNC_AV_STD_SEL_MONOCHROME:
		*avSync = DRX_AUD_AVSYNC_MONOCHROME;
		break;
	case AUD_DSP_WR_AV_SYNC_AV_STD_SEL_PAL_SECAM:
		*avSync = DRX_AUD_AVSYNC_PAL_SECAM;
		break;
	default:
		return DRX_STS_ERROR;
	}

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get deviation mode
* \param demod instance of demodulator
* \param pointer to DRXCfgAudDeviation_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgDev(pDRXDemodInstance_t demod, pDRXCfgAudDeviation_t dev)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t rModus = 0;

	if (dev == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = demod->myI2CDevAddr;

	CHK_ERROR(AUDGetModus(demod, &rModus));

	switch (rModus & AUD_DEM_WR_MODUS_MOD_HDEV_A__M) {
	case AUD_DEM_WR_MODUS_MOD_HDEV_A_NORMAL:
		*dev = DRX_AUD_DEVIATION_NORMAL;
		break;
	case AUD_DEM_WR_MODUS_MOD_HDEV_A_HIGH_DEVIATION:
		*dev = DRX_AUD_DEVIATION_HIGH;
		break;
	default:
		return DRX_STS_ERROR;
	}

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get deviation mode
* \param demod instance of demodulator
* \param pointer to DRXCfgAudDeviation_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlSetCfgDev(pDRXDemodInstance_t demod, pDRXCfgAudDeviation_t dev)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t wModus = 0;
	u16_t rModus = 0;

	if (dev == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = demod->myI2CDevAddr;

	CHK_ERROR(AUDGetModus(demod, &rModus));

	wModus = rModus;

	wModus &= (u16_t) ~ AUD_DEM_WR_MODUS_MOD_HDEV_A__M;

	switch (*dev) {
	case DRX_AUD_DEVIATION_NORMAL:
		wModus |= AUD_DEM_WR_MODUS_MOD_HDEV_A_NORMAL;
		break;
	case DRX_AUD_DEVIATION_HIGH:
		wModus |= AUD_DEM_WR_MODUS_MOD_HDEV_A_HIGH_DEVIATION;
		break;
	default:
		return DRX_STS_INVALID_ARG;
	}

	/* now update the modus register */
	if (wModus != rModus) {
		WR16(devAddr, AUD_DEM_WR_MODUS__A, wModus);
	}
	/* store in drxk data struct */
	extAttr->audData.deviation = *dev;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get Prescaler settings
* \param demod instance of demodulator
* \param pointer to DRXCfgAudPrescale_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetCfgPrescale(pDRXDemodInstance_t demod, pDRXCfgAudPrescale_t presc)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t rMaxFMDeviation = 0;
	u16_t rNicamPrescaler = 0;

	if (presc == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* read register data */
	RR16(devAddr, AUD_DSP_WR_NICAM_PRESC__A, &rNicamPrescaler);
	RR16(devAddr, AUD_DSP_WR_FM_PRESC__A, &rMaxFMDeviation);

	/* calculate max FM deviation */
	rMaxFMDeviation >>= AUD_DSP_WR_FM_PRESC_FM_AM_PRESC__B;
	if (rMaxFMDeviation > 0) {
		presc->fmDeviation = 3600UL + (rMaxFMDeviation >> 1);
		presc->fmDeviation /= rMaxFMDeviation;
	} else {
		presc->fmDeviation = 380;	/* kHz */
	}

	/* calculate NICAM gain from pre-scaler */
	/*
	   nicamGain   = 20 * ( log10( preScaler / 16) )
	   = ( 100log10( preScaler ) - 100log10( 16 ) ) / 5

	   because Log10Times100() cannot return negative numbers
	   = ( 100log10( 10 * preScaler ) - 100log10( 10 * 16) ) / 5

	   for 0.1dB resolution:

	   nicamGain   = 200 * ( log10( preScaler / 16) )
	   = 2 * ( 100log10( 10 * preScaler ) - 100log10( 10 * 16) )
	   = ( 100log10( 10 * preScaler^2 ) - 100log10( 10 * 16^2 ) )

	 */
	rNicamPrescaler >>= 8;
	if (rNicamPrescaler <= 1) {
		presc->nicamGain = -241;
	} else {

		presc->nicamGain = (s16_t) (((s32_t)
					     (Log10Times100
					      (10 * rNicamPrescaler *
					       rNicamPrescaler)) - (s32_t)
					     (Log10Times100(10 * 16 * 16))));
	}

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Set Prescaler settings
* \param demod instance of demodulator
* \param pointer to DRXCfgAudPrescale_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlSetCfgPrescale(pDRXDemodInstance_t demod, pDRXCfgAudPrescale_t presc)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t wMaxFMDeviation = 0;
	u16_t nicamPrescaler;

	if (presc == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* setting of max FM deviation */
	wMaxFMDeviation = (u16_t) (Frac(3600UL, presc->fmDeviation, 0));
	wMaxFMDeviation <<= AUD_DSP_WR_FM_PRESC_FM_AM_PRESC__B;
	if (wMaxFMDeviation >=
	    AUD_DSP_WR_FM_PRESC_FM_AM_PRESC_28_KHZ_FM_DEVIATION) {
		wMaxFMDeviation =
		    AUD_DSP_WR_FM_PRESC_FM_AM_PRESC_28_KHZ_FM_DEVIATION;
	}

	/* NICAM Prescaler */
	if ((presc->nicamGain >= -241) && (presc->nicamGain <= 180)) {
		/* calculation

		   prescaler = 16 * 10^( GdB / 20 )

		   minval of GdB = -20*log( 16 ) = -24.1dB

		   negative numbers not allowed for dB2LinTimes100, so

		   prescaler = 16 * 10^( GdB / 20 )
		   = 10^( (GdB / 20) + log10(16) )
		   = 10^( (GdB + 20log10(16)) / 20 )

		   in 0.1dB

		   = 10^( G0.1dB + 200log10(16)) / 200 )

		 */
		nicamPrescaler = (u16_t)
		    ((dB2LinTimes100(presc->nicamGain + 241UL) + 50UL) / 100UL);

		/* clip result */
		if (nicamPrescaler > 127) {
			nicamPrescaler = 127;
		}

		/* shift before writing to register */
		nicamPrescaler <<= 8;
	} else {
		return (DRX_STS_INVALID_ARG);
	}
	/* end of setting NICAM Prescaler */

	WR16(devAddr, AUD_DSP_WR_NICAM_PRESC__A, nicamPrescaler);
	WR16(devAddr, AUD_DSP_WR_FM_PRESC__A, wMaxFMDeviation);

	extAttr->audData.prescale = *presc;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Beep
* \param demod instance of demodulator
* \param pointer to DRXAudBeep_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t AUDCtrlBeep(pDRXDemodInstance_t demod, pDRXAudBeep_t beep)
{
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;

	u16_t theBeep = 0;
	u16_t volume = 0;
	u32_t frequency = 0;

	if (beep == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	if ((beep->volume > 0) || (beep->volume < -127)) {
		return DRX_STS_INVALID_ARG;
	}

	if (beep->frequency > 3000) {
		return DRX_STS_INVALID_ARG;
	}

	volume = (u16_t) beep->volume + 127;
	theBeep |= volume << AUD_DSP_WR_BEEPER_BEEP_VOLUME__B;

	frequency = ((u32_t) beep->frequency) * 23 / 500;
	if (frequency > AUD_DSP_WR_BEEPER_BEEP_FREQUENCY__M) {
		frequency = AUD_DSP_WR_BEEPER_BEEP_FREQUENCY__M;
	}
	theBeep |= (u16_t) frequency;

	if (beep->mute == TRUE) {
		theBeep = 0;
	}

	WR16(devAddr, AUD_DSP_WR_BEEPER__A, theBeep);

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Set an audio standard
* \param demod instance of demodulator
* \param pointer to DRXAudStandard_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlSetStandard(pDRXDemodInstance_t demod, pDRXAudStandard_t standard)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	DRXStandard_t currentStandard = DRX_STANDARD_UNKNOWN;

	u16_t wStandard = 0;
	u16_t wModus = 0;
	u16_t rModus = 0;

	Bool_t muteBuffer = FALSE;
	s16_t volumeBuffer = 0;
	u16_t wVolume = 0;

	if (standard == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, FALSE));
		extAttr->audData.audioIsActive = TRUE;
	}

	/* reset RDS data availability flag */
	extAttr->audData.rdsDataPresent = FALSE;

	/* we need to mute from here to avoid noise during standard switching */
	muteBuffer = extAttr->audData.volume.mute;
	volumeBuffer = extAttr->audData.volume.volume;

	extAttr->audData.volume.mute = TRUE;
	/* restore data structure from DRX ExtAttr, call volume first to mute */
	CHK_ERROR(AUDCtrlSetCfgVolume(demod, &extAttr->audData.volume));
	CHK_ERROR(AUDCtrlSetCfgCarrier(demod, &extAttr->audData.carriers));
	CHK_ERROR(AUDCtrlSetCfgASSThres
		  (demod, &extAttr->audData.assThresholds));
	CHK_ERROR(AUDCtrSetlCfgAutoSound(demod, &extAttr->audData.autoSound));
	CHK_ERROR(AUDCtrlSetCfgMixer(demod, &extAttr->audData.mixer));
	CHK_ERROR(AUDCtrlSetCfgAVSync(demod, &extAttr->audData.avSync));
	CHK_ERROR(AUDCtrlSetCfgOutputI2S(demod, &extAttr->audData.i2sdata));

	/* get prescaler from presets */
	CHK_ERROR(AUDCtrlSetCfgPrescale(demod, &extAttr->audData.prescale));

	CHK_ERROR(AUDGetModus(demod, &rModus));

	wModus = rModus;

	switch (*standard) {
	case DRX_AUD_STANDARD_AUTO:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_AUTO;
		break;
	case DRX_AUD_STANDARD_BTSC:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_BTSC_STEREO;
		if (extAttr->audData.btscDetect == DRX_BTSC_MONO_AND_SAP) {
			wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_BTSC_SAP;
		}
		break;
	case DRX_AUD_STANDARD_A2:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_M_KOREA;
		break;
	case DRX_AUD_STANDARD_EIAJ:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_EIA_J;
		break;
	case DRX_AUD_STANDARD_FM_STEREO:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_FM_RADIO;
		break;
	case DRX_AUD_STANDARD_BG_FM:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_BG_FM;
		break;
	case DRX_AUD_STANDARD_D_K1:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_D_K1;
		break;
	case DRX_AUD_STANDARD_D_K2:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_D_K2;
		break;
	case DRX_AUD_STANDARD_D_K3:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_D_K3;
		break;
	case DRX_AUD_STANDARD_BG_NICAM_FM:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_BG_NICAM_FM;
		break;
	case DRX_AUD_STANDARD_L_NICAM_AM:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_L_NICAM_AM;
		break;
	case DRX_AUD_STANDARD_I_NICAM_FM:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_I_NICAM_FM;
		break;
	case DRX_AUD_STANDARD_D_K_NICAM_FM:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_D_K_NICAM_FM;
		break;
	case DRX_AUD_STANDARD_UNKNOWN:
		wStandard = AUD_DEM_WR_STANDARD_SEL_STD_SEL_AUTO;
		break;
	default:
		return DRX_STS_ERROR;
	}

	if (*standard == DRX_AUD_STANDARD_AUTO) {
		/* we need the current standard here */
		currentStandard = extAttr->standard;

		wModus &= (u16_t) ~ AUD_DEM_WR_MODUS_MOD_6_5MHZ__M;

		if ((currentStandard == DRX_STANDARD_PAL_SECAM_L) ||
		    (currentStandard == DRX_STANDARD_PAL_SECAM_LP)) {
			wModus |= (AUD_DEM_WR_MODUS_MOD_6_5MHZ_SECAM);
		} else {
			wModus |= (AUD_DEM_WR_MODUS_MOD_6_5MHZ_D_K);
		}

		wModus &= (u16_t) ~ AUD_DEM_WR_MODUS_MOD_4_5MHZ__M;
		if (currentStandard == DRX_STANDARD_NTSC) {
			wModus |= (AUD_DEM_WR_MODUS_MOD_4_5MHZ_M_BTSC);

		} else {	/* non USA, ignore standard M to save time */

			wModus |= (AUD_DEM_WR_MODUS_MOD_4_5MHZ_CHROMA);
		}

	}

	wModus &= (u16_t) ~ AUD_DEM_WR_MODUS_MOD_FMRADIO__M;

	/* just get hardcoded deemphasis and activate here */
	if (extAttr->audData.deemph == DRX_AUD_FM_DEEMPH_50US) {
		wModus |= (AUD_DEM_WR_MODUS_MOD_FMRADIO_EU_50U);
	} else {
		wModus |= (AUD_DEM_WR_MODUS_MOD_FMRADIO_US_75U);
	}

	wModus &= (u16_t) ~ AUD_DEM_WR_MODUS_MOD_BTSC__M;
	if (extAttr->audData.btscDetect == DRX_BTSC_STEREO) {
		wModus |= (AUD_DEM_WR_MODUS_MOD_BTSC_BTSC_STEREO);
	} else {		/* DRX_BTSC_MONO_AND_SAP */

		wModus |= (AUD_DEM_WR_MODUS_MOD_BTSC_BTSC_SAP);
	}

	if (wModus != rModus) {
		WR16(devAddr, AUD_DEM_WR_MODUS__A, wModus);
	}

	WR16(devAddr, AUD_DEM_WR_STANDARD_SEL__A, wStandard);

   /**************************************************************************/
	/* NOT calling AUDCtrlSetCfgVolume to avoid interfering standard          */
	/* detection, need to keep things very minimal here, but keep audio       */
	/* buffers intact                                                         */
   /**************************************************************************/
	extAttr->audData.volume.mute = muteBuffer;
	if (extAttr->audData.volume.mute == FALSE) {
		wVolume |= (u16_t) ((volumeBuffer + AUD_VOLUME_ZERO_DB) <<
				    AUD_DSP_WR_VOLUME_VOL_MAIN__B);
		WR16(devAddr, AUD_DSP_WR_VOLUME__A, wVolume);
	}

	/* write standard selected */
	extAttr->audData.audioStandard = *standard;

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;
}

/*============================================================================*/
/**
* \brief Get the current audio standard
* \param demod instance of demodulator
* \param pointer to DRXAudStandard_t
* \return DRXStatus_t.
*
*/
static DRXStatus_t
AUDCtrlGetStandard(pDRXDemodInstance_t demod, pDRXAudStandard_t standard)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	u16_t rData = 0;

	if (standard == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = (struct i2c_device_addr *) demod->myI2CDevAddr;

	/* power up */
	if (extAttr->audData.audioIsActive == FALSE) {
		CHK_ERROR(PowerUpAud(demod, TRUE));
		extAttr->audData.audioIsActive = TRUE;
	}

	*standard = DRX_AUD_STANDARD_UNKNOWN;

	RR16(devAddr, AUD_DEM_RD_STANDARD_RES__A, &rData);

	/* return OK if the detection is not ready yet */
	if (rData >= AUD_DEM_RD_STANDARD_RES_STD_RESULT_DETECTION_STILL_ACTIVE) {
		*standard = DRX_AUD_STANDARD_NOT_READY;
		return DRX_STS_OK;
	}

	/* detection done, return correct standard */
	switch (rData) {
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

	return DRX_STS_OK;
rw_error:
	return DRX_STS_ERROR;

}

/*============================================================================*/
/**
* \brief Retreive lock status in case of FM standard
* \param demod instance of demodulator
* \param pointer to lock status
* \return DRXStatus_t.
*
*/
static DRXStatus_t
FmLockStatus(pDRXDemodInstance_t demod, pDRXLockStatus_t lockStat)
{
	DRXAudStatus_t status;

	/* Check detection of audio carriers */
	CHK_ERROR(AUDCtrlGetCarrierDetectStatus(demod, &status));

	/* locked if either primary or secondary carrier is detected */
	if ((status.carrierA == TRUE) || (status.carrierB == TRUE)) {
		*lockStat = DRX_LOCKED;
	} else {
		*lockStat = DRX_NOT_LOCKED;
	}

	return (DRX_STS_OK);

rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/
/**
* \brief retreive signal quality in case of FM standard
* \param demod instance of demodulator
* \param pointer to signal quality
* \return DRXStatus_t.
*
* Only the quality indicator field is will be supplied.
* This will either be 0% or 100%, nothing in between.
*
*/
static DRXStatus_t
FmSigQuality(pDRXDemodInstance_t demod, pDRXSigQuality_t sigQuality)
{
	DRXLockStatus_t lockStatus = DRX_NOT_LOCKED;

	CHK_ERROR(FmLockStatus(demod, &lockStatus));
	if (lockStatus == DRX_LOCKED) {
		sigQuality->indicator = 100;
	} else {
		sigQuality->indicator = 0;
	}

	return (DRX_STS_OK);

rw_error:
	return (DRX_STS_ERROR);
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
* \fn DRXStatus_t GetOOBLockStatus ()
* \brief Get OOB lock status.
* \param devAddr I2C address
  \      oobLock OOB lock status.
* \return DRXStatus_t.
*
* Gets OOB lock status
*
*/
static DRXStatus_t
GetOOBLockStatus(pDRXDemodInstance_t demod,
		 struct i2c_device_addr *devAddr, pDRXLockStatus_t oobLock)
{
	DRXJSCUCmd_t scuCmd;
	u16_t cmdResult[2];
	u16_t OOBLockState;

	*oobLock = DRX_NOT_LOCKED;

	scuCmd.command = SCU_RAM_COMMAND_STANDARD_OOB |
	    SCU_RAM_COMMAND_CMD_DEMOD_GET_LOCK;
	scuCmd.resultLen = 2;
	scuCmd.result = cmdResult;
	scuCmd.parameterLen = 0;

	CHK_ERROR(SCUCommand(devAddr, &scuCmd));

	if (scuCmd.result[1] < 0x4000) {
		/* 0x00 NOT LOCKED */
		*oobLock = DRX_NOT_LOCKED;
	} else if (scuCmd.result[1] < 0x8000) {
		/* 0x40 DEMOD LOCKED */
		*oobLock = DRXJ_OOB_SYNC_LOCK;
	} else if (scuCmd.result[1] < 0xC000) {
		/* 0x80 DEMOD + OOB LOCKED (system lock) */
		OOBLockState = scuCmd.result[1] & 0x00FF;

		if (OOBLockState & 0x0008) {
			*oobLock = DRXJ_OOB_SYNC_LOCK;
		} else if ((OOBLockState & 0x0002) && (OOBLockState & 0x0001)) {
			*oobLock = DRXJ_OOB_AGC_LOCK;
		}
	} else {
		/* 0xC0 NEVER LOCKED (system will never be able to lock to the signal) */
		*oobLock = DRX_NEVER_LOCK;
	}

	/* *oobLock = scuCmd.result[1]; */

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t GetOOBSymbolRateOffset ()
* \brief Get OOB Symbol rate offset. Unit is [ppm]
* \param devAddr I2C address
* \      Symbol Rate Offset OOB parameter.
* \return DRXStatus_t.
*
* Gets OOB frequency offset
*
*/
static DRXStatus_t
GetOOBSymbolRateOffset(struct i2c_device_addr *devAddr, ps32_t SymbolRateOffset)
{
/*  offset = -{(timingOffset/2^19)*(symbolRate/12,656250MHz)}*10^6 [ppm]  */
/*  offset = -{(timingOffset/2^19)*(symbolRate/12656250)}*10^6 [ppm]  */
/*  after reconfiguration: */
/*  offset = -{(timingOffset*symbolRate)/(2^19*12656250)}*10^6 [ppm]  */
/*  shift symbol rate left by 5 without lossing information */
/*  offset = -{(timingOffset*(symbolRate * 2^-5))/(2^14*12656250)}*10^6 [ppm]*/
/*  shift 10^6 left by 6 without loosing information */
/*  offset = -{(timingOffset*(symbolRate * 2^-5))/(2^8*12656250)}*15625 [ppm]*/
/*  trim 12656250/15625 = 810 */
/*  offset = -{(timingOffset*(symbolRate * 2^-5))/(2^8*810)} [ppm]  */
/*  offset = -[(symbolRate * 2^-5)*(timingOffset)/(2^8)]/810 [ppm]  */
	s32_t timingOffset = 0;
	u32_t unsignedTimingOffset = 0;
	s32_t divisionFactor = 810;
	u16_t data = 0;
	u32_t symbolRate = 0;
	Bool_t negative = FALSE;

	*SymbolRateOffset = 0;
	/* read data rate */
	SARR16(devAddr, SCU_RAM_ORX_RF_RX_DATA_RATE__A, &data);
	switch (data & SCU_RAM_ORX_RF_RX_DATA_RATE__M) {
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC_ALT:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC_ALT:
		symbolRate = 1024000;	/* bps */
		break;
	case SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_INVSPEC:
		symbolRate = 772000;	/* bps */
		break;
	case SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_INVSPEC:
		symbolRate = 1544000;	/* bps */
		break;
	default:
		return (DRX_STS_ERROR);
	}

	RR16(devAddr, ORX_CON_CTI_DTI_R__A, &data);
	/* convert data to positive and keep information about sign */
	if ((data & 0x8000) == 0x8000) {
		if (data == 0x8000)
			unsignedTimingOffset = 32768;
		else
			unsignedTimingOffset = 0x00007FFF & (u32_t) (-data);
		negative = TRUE;
	} else
		unsignedTimingOffset = (u32_t) data;

	symbolRate = symbolRate >> 5;
	unsignedTimingOffset = (unsignedTimingOffset * symbolRate);
	unsignedTimingOffset = Frac(unsignedTimingOffset, 256, FRAC_ROUND);
	unsignedTimingOffset = Frac(unsignedTimingOffset,
				    divisionFactor, FRAC_ROUND);
	if (negative)
		timingOffset = (s32_t) unsignedTimingOffset;
	else
		timingOffset = -(s32_t) unsignedTimingOffset;

	*SymbolRateOffset = timingOffset;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t GetOOBFreqOffset ()
* \brief Get OOB lock status.
* \param devAddr I2C address
* \      freqOffset OOB frequency offset.
* \return DRXStatus_t.
*
* Gets OOB frequency offset
*
*/
static DRXStatus_t
GetOOBFreqOffset(pDRXDemodInstance_t demod, pDRXFrequency_t freqOffset)
{
	u16_t data = 0;
	u16_t rot = 0;
	u16_t symbolRateReg = 0;
	u32_t symbolRate = 0;
	s32_t coarseFreqOffset = 0;
	s32_t fineFreqOffset = 0;
	s32_t fineSign = 1;
	s32_t coarseSign = 1;
	u32_t data64Hi = 0;
	u32_t data64Lo = 0;
	u32_t tempFreqOffset = 0;
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) (NULL);
	struct i2c_device_addr *devAddr = NULL;

	/* check arguments */
	if ((demod == NULL) || (freqOffset == NULL)) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;

	*freqOffset = 0;

	/* read sign (spectrum inversion) */
	RR16(devAddr, ORX_FWP_IQM_FRQ_W__A, &rot);

	/* read frequency offset */
	SARR16(devAddr, SCU_RAM_ORX_FRQ_OFFSET__A, &data);
	/* find COARSE frequency offset */
	/* coarseFreqOffset = ( 25312500Hz*FRQ_OFFSET >> 21 ); */
	if (data & 0x8000) {
		data = (0xffff - data + 1);
		coarseSign = -1;
	}
	Mult32(data, (commonAttr->sysClockFreq * 1000) / 6, &data64Hi,
	       &data64Lo);
	tempFreqOffset = (((data64Lo >> 21) & 0x7ff) | (data64Hi << 11));

	/* get value in KHz */
	coarseFreqOffset = coarseSign * Frac(tempFreqOffset, 1000, FRAC_ROUND);	/* KHz */
	/* read data rate */
	SARR16(devAddr, SCU_RAM_ORX_RF_RX_DATA_RATE__A, &symbolRateReg);
	switch (symbolRateReg & SCU_RAM_ORX_RF_RX_DATA_RATE__M) {
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC_ALT:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC_ALT:
		symbolRate = 1024000;
		break;
	case SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_INVSPEC:
		symbolRate = 772000;
		break;
	case SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_REGSPEC:
	case SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_INVSPEC:
		symbolRate = 1544000;
		break;
	default:
		return (DRX_STS_ERROR);
	}

	/* find FINE frequency offset */
	/* fineFreqOffset = ( (CORRECTION_VALUE*symbolRate) >> 18 ); */
	RR16(devAddr, ORX_CON_CPH_FRQ_R__A, &data);
	/* at least 5 MSB are 0 so first divide with 2^5 without information loss */
	fineFreqOffset = (symbolRate >> 5);
	if (data & 0x8000) {
		fineFreqOffset *= 0xffff - data + 1;	/* Hz */
		fineSign = -1;
	} else {
		fineFreqOffset *= data;	/* Hz */
	}
	/* Left to divide with 8192 (2^13) */
	fineFreqOffset = Frac(fineFreqOffset, 8192, FRAC_ROUND);
	/* and to divide with 1000 to get KHz */
	fineFreqOffset = fineSign * Frac(fineFreqOffset, 1000, FRAC_ROUND);	/* KHz */

	if ((rot & 0x8000) == 0x8000)
		*freqOffset = -(coarseFreqOffset + fineFreqOffset);
	else
		*freqOffset = (coarseFreqOffset + fineFreqOffset);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t GetOOBFrequency ()
* \brief Get OOB frequency (Unit:KHz).
* \param devAddr I2C address
* \      frequency OOB frequency parameters.
* \return DRXStatus_t.
*
* Gets OOB frequency
*
*/
static DRXStatus_t
GetOOBFrequency(pDRXDemodInstance_t demod, pDRXFrequency_t frequency)
{
	u16_t data = 0;
	DRXFrequency_t freqOffset = 0;
	DRXFrequency_t freq = 0;
	struct i2c_device_addr *devAddr = NULL;

	devAddr = demod->myI2CDevAddr;

	*frequency = 0;		/* KHz */

	SARR16(devAddr, SCU_RAM_ORX_RF_RX_FREQUENCY_VALUE__A, &data);

	freq = (DRXFrequency_t) ((DRXFrequency_t) data * 50 + 50000L);

	CHK_ERROR(GetOOBFreqOffset(demod, &freqOffset));

	*frequency = freq + freqOffset;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t GetOOBMER ()
* \brief Get OOB MER.
* \param devAddr I2C address
  \      MER OOB parameter in dB.
* \return DRXStatus_t.
*
* Gets OOB MER. Table for MER is in Programming guide.
*
*/
static DRXStatus_t GetOOBMER(struct i2c_device_addr *devAddr, pu32_t mer)
{
	u16_t data = 0;

	*mer = 0;
	/* READ MER */
	RR16(devAddr, ORX_EQU_MER_MER_R__A, &data);
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
	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}
#endif /*#ifndef DRXJ_DIGITAL_ONLY */

/**
* \fn DRXStatus_t SetOrxNsuAox()
* \brief Configure OrxNsuAox for OOB
* \param demod instance of demodulator.
* \param active
* \return DRXStatus_t.
*/
static DRXStatus_t SetOrxNsuAox(pDRXDemodInstance_t demod, Bool_t active)
{
	u16_t data = 0;
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = demod->myI2CDevAddr;

	/* Configure NSU_AOX */
	RR16(devAddr, ORX_NSU_AOX_STDBY_W__A, &data);
	if (!active) {
		data &= ((~ORX_NSU_AOX_STDBY_W_STDBYADC_A2_ON)
			 & (~ORX_NSU_AOX_STDBY_W_STDBYAMP_A2_ON)
			 & (~ORX_NSU_AOX_STDBY_W_STDBYBIAS_A2_ON)
			 & (~ORX_NSU_AOX_STDBY_W_STDBYPLL_A2_ON)
			 & (~ORX_NSU_AOX_STDBY_W_STDBYPD_A2_ON)
			 & (~ORX_NSU_AOX_STDBY_W_STDBYTAGC_IF_A2_ON)
			 & (~ORX_NSU_AOX_STDBY_W_STDBYTAGC_RF_A2_ON)
			 & (~ORX_NSU_AOX_STDBY_W_STDBYFLT_A2_ON)
		    );
	} else {		/* active */

		data |= (ORX_NSU_AOX_STDBY_W_STDBYADC_A2_ON
			 | ORX_NSU_AOX_STDBY_W_STDBYAMP_A2_ON
			 | ORX_NSU_AOX_STDBY_W_STDBYBIAS_A2_ON
			 | ORX_NSU_AOX_STDBY_W_STDBYPLL_A2_ON
			 | ORX_NSU_AOX_STDBY_W_STDBYPD_A2_ON
			 | ORX_NSU_AOX_STDBY_W_STDBYTAGC_IF_A2_ON
			 | ORX_NSU_AOX_STDBY_W_STDBYTAGC_RF_A2_ON
			 | ORX_NSU_AOX_STDBY_W_STDBYFLT_A2_ON);
	}
	WR16(devAddr, ORX_NSU_AOX_STDBY_W__A, data);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t CtrlSetOOB()
* \brief Set OOB channel to be used.
* \param demod instance of demodulator
* \param oobParam OOB parameters for channel setting.
* \frequency should be in KHz
* \return DRXStatus_t.
*
* Accepts  only. Returns error otherwise.
* Demapper value is written after SCUCommand START
* because START command causes COMM_EXEC transition
* from 0 to 1 which causes all registers to be
* overwritten with initial value
*
*/

/* Nyquist filter impulse response */
#define IMPULSE_COSINE_ALPHA_0_3    {-3,-4,-1, 6,10, 7,-5,-20,-25,-10,29,79,123,140}	/*sqrt raised-cosine filter with alpha=0.3 */
#define IMPULSE_COSINE_ALPHA_0_5    { 2, 0,-2,-2, 2, 5, 2,-10,-20,-14,20,74,125,145}	/*sqrt raised-cosine filter with alpha=0.5 */
#define IMPULSE_COSINE_ALPHA_RO_0_5 { 0, 0, 1, 2, 3, 0,-7,-15,-16,  0,34,77,114,128}	/*full raised-cosine filter with alpha=0.5 (receiver only) */

/* Coefficients for the nyquist fitler (total: 27 taps) */
#define NYQFILTERLEN 27

static DRXStatus_t CtrlSetOOB(pDRXDemodInstance_t demod, pDRXOOB_t oobParam)
{
#ifndef DRXJ_DIGITAL_ONLY
	DRXOOBDownstreamStandard_t standard = DRX_OOB_MODE_A;
	DRXFrequency_t freq = 0;	/* KHz */
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	u16_t i = 0;
	Bool_t mirrorFreqSpectOOB = FALSE;
	u16_t trkFilterValue = 0;
	DRXJSCUCmd_t scuCmd;
	u16_t setParamParameters[3];
	u16_t cmdResult[2] = { 0, 0 };
	s16_t NyquistCoeffs[4][(NYQFILTERLEN + 1) / 2] = {
		IMPULSE_COSINE_ALPHA_0_3,	/* Target Mode 0 */
		IMPULSE_COSINE_ALPHA_0_3,	/* Target Mode 1 */
		IMPULSE_COSINE_ALPHA_0_5,	/* Target Mode 2 */
		IMPULSE_COSINE_ALPHA_RO_0_5	/* Target Mode 3 */
	};
	u8_t mode_val[4] = { 2, 2, 0, 1 };
	u8_t PFICoeffs[4][6] = {
		{DRXJ_16TO8(-92), DRXJ_16TO8(-108), DRXJ_16TO8(100)},	/* TARGET_MODE = 0:     PFI_A = -23/32; PFI_B = -54/32;  PFI_C = 25/32; fg = 0.5 MHz (Att=26dB) */
		{DRXJ_16TO8(-64), DRXJ_16TO8(-80), DRXJ_16TO8(80)},	/* TARGET_MODE = 1:     PFI_A = -16/32; PFI_B = -40/32;  PFI_C = 20/32; fg = 1.0 MHz (Att=28dB) */
		{DRXJ_16TO8(-80), DRXJ_16TO8(-98), DRXJ_16TO8(92)},	/* TARGET_MODE = 2, 3:  PFI_A = -20/32; PFI_B = -49/32;  PFI_C = 23/32; fg = 0.8 MHz (Att=25dB) */
		{DRXJ_16TO8(-80), DRXJ_16TO8(-98), DRXJ_16TO8(92)}	/* TARGET_MODE = 2, 3:  PFI_A = -20/32; PFI_B = -49/32;  PFI_C = 23/32; fg = 0.8 MHz (Att=25dB) */
	};
	u16_t mode_index;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	mirrorFreqSpectOOB = extAttr->mirrorFreqSpectOOB;

	/* Check parameters */
	if (oobParam == NULL) {
		/* power off oob module  */
		scuCmd.command = SCU_RAM_COMMAND_STANDARD_OOB
		    | SCU_RAM_COMMAND_CMD_DEMOD_STOP;
		scuCmd.parameterLen = 0;
		scuCmd.resultLen = 1;
		scuCmd.result = cmdResult;
		CHK_ERROR(SCUCommand(devAddr, &scuCmd));
		CHK_ERROR(SetOrxNsuAox(demod, FALSE));
		WR16(devAddr, ORX_COMM_EXEC__A, ORX_COMM_EXEC_STOP);

		extAttr->oobPowerOn = FALSE;
		return (DRX_STS_OK);
	}

	standard = oobParam->standard;

	freq = oobParam->frequency;
	if ((freq < 70000) || (freq > 130000))
		return (DRX_STS_ERROR);
	freq = (freq - 50000) / 50;

	{
		u16_t index = 0;
		u16_t remainder = 0;
		pu16_t trkFiltercfg = extAttr->oobTrkFilterCfg;

		index = (u16_t) ((freq - 400) / 200);
		remainder = (u16_t) ((freq - 400) % 200);
		trkFilterValue =
		    trkFiltercfg[index] - (trkFiltercfg[index] -
					   trkFiltercfg[index +
							1]) / 10 * remainder /
		    20;
	}

   /*********/
	/* Stop  */
   /*********/
	WR16(devAddr, ORX_COMM_EXEC__A, ORX_COMM_EXEC_STOP);
	scuCmd.command = SCU_RAM_COMMAND_STANDARD_OOB
	    | SCU_RAM_COMMAND_CMD_DEMOD_STOP;
	scuCmd.parameterLen = 0;
	scuCmd.resultLen = 1;
	scuCmd.result = cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &scuCmd));
   /*********/
	/* Reset */
   /*********/
	scuCmd.command = SCU_RAM_COMMAND_STANDARD_OOB
	    | SCU_RAM_COMMAND_CMD_DEMOD_RESET;
	scuCmd.parameterLen = 0;
	scuCmd.resultLen = 1;
	scuCmd.result = cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &scuCmd));
   /***********/
	/* SET_ENV */
   /***********/
	/* set frequency, spectrum inversion and data rate */
	scuCmd.command = SCU_RAM_COMMAND_STANDARD_OOB
	    | SCU_RAM_COMMAND_CMD_DEMOD_SET_ENV;
	scuCmd.parameterLen = 3;
	/* 1-data rate;2-frequency */
	switch (oobParam->standard) {
	case DRX_OOB_MODE_A:
		if (
			   /* signal is transmitted inverted */
			   ((oobParam->spectrumInverted == TRUE) &
			    /* and tuner is not mirroring the signal */
			    (mirrorFreqSpectOOB == FALSE)) |
			   /* or */
			   /* signal is transmitted noninverted */
			   ((oobParam->spectrumInverted == FALSE) &
			    /* and tuner is mirroring the signal */
			    (mirrorFreqSpectOOB == TRUE))
		    )
			setParamParameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_INVSPEC;
		else
			setParamParameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_2048KBPS_REGSPEC;
		break;
	case DRX_OOB_MODE_B_GRADE_A:
		if (
			   /* signal is transmitted inverted */
			   ((oobParam->spectrumInverted == TRUE) &
			    /* and tuner is not mirroring the signal */
			    (mirrorFreqSpectOOB == FALSE)) |
			   /* or */
			   /* signal is transmitted noninverted */
			   ((oobParam->spectrumInverted == FALSE) &
			    /* and tuner is mirroring the signal */
			    (mirrorFreqSpectOOB == TRUE))
		    )
			setParamParameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_INVSPEC;
		else
			setParamParameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_1544KBPS_REGSPEC;
		break;
	case DRX_OOB_MODE_B_GRADE_B:
	default:
		if (
			   /* signal is transmitted inverted */
			   ((oobParam->spectrumInverted == TRUE) &
			    /* and tuner is not mirroring the signal */
			    (mirrorFreqSpectOOB == FALSE)) |
			   /* or */
			   /* signal is transmitted noninverted */
			   ((oobParam->spectrumInverted == FALSE) &
			    /* and tuner is mirroring the signal */
			    (mirrorFreqSpectOOB == TRUE))
		    )
			setParamParameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_INVSPEC;
		else
			setParamParameters[0] =
			    SCU_RAM_ORX_RF_RX_DATA_RATE_3088KBPS_REGSPEC;
		break;
	}
	setParamParameters[1] = (u16_t) (freq & 0xFFFF);
	setParamParameters[2] = trkFilterValue;
	scuCmd.parameter = setParamParameters;
	scuCmd.resultLen = 1;
	scuCmd.result = cmdResult;
	mode_index = mode_val[(setParamParameters[0] & 0xC0) >> 6];
	CHK_ERROR(SCUCommand(devAddr, &scuCmd));

	WR16(devAddr, SIO_TOP_COMM_KEY__A, 0xFABA);	/*  Write magic word to enable pdr reg write  */
	WR16(devAddr, SIO_PDR_OOB_CRX_CFG__A,
	     OOB_CRX_DRIVE_STRENGTH << SIO_PDR_OOB_CRX_CFG_DRIVE__B
	     | 0x03 << SIO_PDR_OOB_CRX_CFG_MODE__B);
	WR16(devAddr, SIO_PDR_OOB_DRX_CFG__A,
	     OOB_DRX_DRIVE_STRENGTH << SIO_PDR_OOB_DRX_CFG_DRIVE__B
	     | 0x03 << SIO_PDR_OOB_DRX_CFG_MODE__B);
	WR16(devAddr, SIO_TOP_COMM_KEY__A, 0x0000);	/*  Write magic word to disable pdr reg write */

	WR16(devAddr, ORX_TOP_COMM_KEY__A, 0);
	WR16(devAddr, ORX_FWP_AAG_LEN_W__A, 16000);
	WR16(devAddr, ORX_FWP_AAG_THR_W__A, 40);

	/* ddc */
	WR16(devAddr, ORX_DDC_OFO_SET_W__A, ORX_DDC_OFO_SET_W__PRE);

	/* nsu */
	WR16(devAddr, ORX_NSU_AOX_LOPOW_W__A, extAttr->oobLoPow);

	/* initialization for target mode */
	WR16(devAddr, SCU_RAM_ORX_TARGET_MODE__A,
	     SCU_RAM_ORX_TARGET_MODE_2048KBPS_SQRT);
	WR16(devAddr, SCU_RAM_ORX_FREQ_GAIN_CORR__A,
	     SCU_RAM_ORX_FREQ_GAIN_CORR_2048KBPS);

	/* Reset bits for timing and freq. recovery */
	WR16(devAddr, SCU_RAM_ORX_RST_CPH__A, 0x0001);
	WR16(devAddr, SCU_RAM_ORX_RST_CTI__A, 0x0002);
	WR16(devAddr, SCU_RAM_ORX_RST_KRN__A, 0x0004);
	WR16(devAddr, SCU_RAM_ORX_RST_KRP__A, 0x0008);

	/* AGN_LOCK = {2048>>3, -2048, 8, -8, 0, 1}; */
	WR16(devAddr, SCU_RAM_ORX_AGN_LOCK_TH__A, 2048 >> 3);
	WR16(devAddr, SCU_RAM_ORX_AGN_LOCK_TOTH__A, (u16_t) (-2048));
	WR16(devAddr, SCU_RAM_ORX_AGN_ONLOCK_TTH__A, 8);
	WR16(devAddr, SCU_RAM_ORX_AGN_UNLOCK_TTH__A, (u16_t) (-8));
	WR16(devAddr, SCU_RAM_ORX_AGN_LOCK_MASK__A, 1);

	/* DGN_LOCK = {10, -2048, 8, -8, 0, 1<<1}; */
	WR16(devAddr, SCU_RAM_ORX_DGN_LOCK_TH__A, 10);
	WR16(devAddr, SCU_RAM_ORX_DGN_LOCK_TOTH__A, (u16_t) (-2048));
	WR16(devAddr, SCU_RAM_ORX_DGN_ONLOCK_TTH__A, 8);
	WR16(devAddr, SCU_RAM_ORX_DGN_UNLOCK_TTH__A, (u16_t) (-8));
	WR16(devAddr, SCU_RAM_ORX_DGN_LOCK_MASK__A, 1 << 1);

	/* FRQ_LOCK = {15,-2048, 8, -8, 0, 1<<2}; */
	WR16(devAddr, SCU_RAM_ORX_FRQ_LOCK_TH__A, 17);
	WR16(devAddr, SCU_RAM_ORX_FRQ_LOCK_TOTH__A, (u16_t) (-2048));
	WR16(devAddr, SCU_RAM_ORX_FRQ_ONLOCK_TTH__A, 8);
	WR16(devAddr, SCU_RAM_ORX_FRQ_UNLOCK_TTH__A, (u16_t) (-8));
	WR16(devAddr, SCU_RAM_ORX_FRQ_LOCK_MASK__A, 1 << 2);

	/* PHA_LOCK = {5000, -2048, 8, -8, 0, 1<<3}; */
	WR16(devAddr, SCU_RAM_ORX_PHA_LOCK_TH__A, 3000);
	WR16(devAddr, SCU_RAM_ORX_PHA_LOCK_TOTH__A, (u16_t) (-2048));
	WR16(devAddr, SCU_RAM_ORX_PHA_ONLOCK_TTH__A, 8);
	WR16(devAddr, SCU_RAM_ORX_PHA_UNLOCK_TTH__A, (u16_t) (-8));
	WR16(devAddr, SCU_RAM_ORX_PHA_LOCK_MASK__A, 1 << 3);

	/* TIM_LOCK = {300,      -2048, 8, -8, 0, 1<<4}; */
	WR16(devAddr, SCU_RAM_ORX_TIM_LOCK_TH__A, 400);
	WR16(devAddr, SCU_RAM_ORX_TIM_LOCK_TOTH__A, (u16_t) (-2048));
	WR16(devAddr, SCU_RAM_ORX_TIM_ONLOCK_TTH__A, 8);
	WR16(devAddr, SCU_RAM_ORX_TIM_UNLOCK_TTH__A, (u16_t) (-8));
	WR16(devAddr, SCU_RAM_ORX_TIM_LOCK_MASK__A, 1 << 4);

	/* EQU_LOCK = {20,      -2048, 8, -8, 0, 1<<5}; */
	WR16(devAddr, SCU_RAM_ORX_EQU_LOCK_TH__A, 20);
	WR16(devAddr, SCU_RAM_ORX_EQU_LOCK_TOTH__A, (u16_t) (-2048));
	WR16(devAddr, SCU_RAM_ORX_EQU_ONLOCK_TTH__A, 4);
	WR16(devAddr, SCU_RAM_ORX_EQU_UNLOCK_TTH__A, (u16_t) (-4));
	WR16(devAddr, SCU_RAM_ORX_EQU_LOCK_MASK__A, 1 << 5);

	/* PRE-Filter coefficients (PFI) */
	WRB(devAddr, ORX_FWP_PFI_A_W__A, sizeof(PFICoeffs[mode_index]),
	    ((pu8_t) PFICoeffs[mode_index]));
	WR16(devAddr, ORX_TOP_MDE_W__A, mode_index);

	/* NYQUIST-Filter coefficients (NYQ) */
	for (i = 0; i < (NYQFILTERLEN + 1) / 2; i++) {
		WR16(devAddr, ORX_FWP_NYQ_ADR_W__A, i);
		WR16(devAddr, ORX_FWP_NYQ_COF_RW__A,
		     NyquistCoeffs[mode_index][i]);
	}
	WR16(devAddr, ORX_FWP_NYQ_ADR_W__A, 31);
	WR16(devAddr, ORX_COMM_EXEC__A, ORX_COMM_EXEC_ACTIVE);
   /*********/
	/* Start */
   /*********/
	scuCmd.command = SCU_RAM_COMMAND_STANDARD_OOB
	    | SCU_RAM_COMMAND_CMD_DEMOD_START;
	scuCmd.parameterLen = 0;
	scuCmd.resultLen = 1;
	scuCmd.result = cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &scuCmd));

	CHK_ERROR(SetOrxNsuAox(demod, TRUE));
	WR16(devAddr, ORX_NSU_AOX_STHR_W__A, extAttr->oobPreSaw);

	extAttr->oobPowerOn = TRUE;

	return (DRX_STS_OK);
rw_error:
#endif
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t CtrlGetOOB()
* \brief Set modulation standard to be used.
* \param demod instance of demodulator
* \param oobStatus OOB status parameters.
* \return DRXStatus_t.
*/
static DRXStatus_t
CtrlGetOOB(pDRXDemodInstance_t demod, pDRXOOBStatus_t oobStatus)
{
#ifndef DRXJ_DIGITAL_ONLY
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	u16_t data = 0;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* check arguments */
	if (oobStatus == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	if (extAttr->oobPowerOn == FALSE)
		return (DRX_STS_ERROR);

	RR16(devAddr, ORX_DDC_OFO_SET_W__A, &data);
	RR16(devAddr, ORX_NSU_TUN_RFGAIN_W__A, &data);
	RR16(devAddr, ORX_FWP_AAG_THR_W__A, &data);
	SARR16(devAddr, SCU_RAM_ORX_DGN_KI__A, &data);
	RR16(devAddr, ORX_FWP_SRC_DGN_W__A, &data);

	CHK_ERROR(GetOOBLockStatus(demod, devAddr, &oobStatus->lock));
	CHK_ERROR(GetOOBFrequency(demod, &oobStatus->frequency));
	CHK_ERROR(GetOOBMER(devAddr, &oobStatus->mer));
	CHK_ERROR(GetOOBSymbolRateOffset
		  (devAddr, &oobStatus->symbolRateOffset));

	return (DRX_STS_OK);
rw_error:
#endif
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t CtrlSetCfgOOBPreSAW()
* \brief Configure PreSAW treshold value
* \param cfgData Pointer to configuration parameter
* \return Error code
*/
#ifndef DRXJ_DIGITAL_ONLY
static DRXStatus_t
CtrlSetCfgOOBPreSAW(pDRXDemodInstance_t demod, pu16_t cfgData)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	if (cfgData == NULL) {
		return (DRX_STS_INVALID_ARG);
	}
	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	WR16(devAddr, ORX_NSU_AOX_STHR_W__A, *cfgData);
	extAttr->oobPreSaw = *cfgData;
	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}
#endif

/**
* \fn DRXStatus_t CtrlGetCfgOOBPreSAW()
* \brief Configure PreSAW treshold value
* \param cfgData Pointer to configuration parameter
* \return Error code
*/
#ifndef DRXJ_DIGITAL_ONLY
static DRXStatus_t
CtrlGetCfgOOBPreSAW(pDRXDemodInstance_t demod, pu16_t cfgData)
{
	pDRXJData_t extAttr = NULL;

	if (cfgData == NULL) {
		return (DRX_STS_INVALID_ARG);
	}
	extAttr = (pDRXJData_t) demod->myExtAttr;

	*cfgData = extAttr->oobPreSaw;

	return (DRX_STS_OK);
}
#endif

/**
* \fn DRXStatus_t CtrlSetCfgOOBLoPower()
* \brief Configure LO Power value
* \param cfgData Pointer to pDRXJCfgOobLoPower_t
* \return Error code
*/
#ifndef DRXJ_DIGITAL_ONLY
static DRXStatus_t
CtrlSetCfgOOBLoPower(pDRXDemodInstance_t demod, pDRXJCfgOobLoPower_t cfgData)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	if (cfgData == NULL) {
		return (DRX_STS_INVALID_ARG);
	}
	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	WR16(devAddr, ORX_NSU_AOX_LOPOW_W__A, *cfgData);
	extAttr->oobLoPow = *cfgData;
	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}
#endif

/**
* \fn DRXStatus_t CtrlGetCfgOOBLoPower()
* \brief Configure LO Power value
* \param cfgData Pointer to pDRXJCfgOobLoPower_t
* \return Error code
*/
#ifndef DRXJ_DIGITAL_ONLY
static DRXStatus_t
CtrlGetCfgOOBLoPower(pDRXDemodInstance_t demod, pDRXJCfgOobLoPower_t cfgData)
{
	pDRXJData_t extAttr = NULL;

	if (cfgData == NULL) {
		return (DRX_STS_INVALID_ARG);
	}
	extAttr = (pDRXJData_t) demod->myExtAttr;

	*cfgData = extAttr->oobLoPow;

	return (DRX_STS_OK);
}
#endif
/*============================================================================*/
/*==                     END OOB DATAPATH FUNCTIONS                         ==*/
/*============================================================================*/

/*=============================================================================
  ===== MC command related functions ==========================================
  ===========================================================================*/

/*=============================================================================
  ===== CtrlSetChannel() ==========================================================
  ===========================================================================*/
/**
* \fn DRXStatus_t CtrlSetChannel()
* \brief Select a new transmission channel.
* \param demod instance of demod.
* \param channel Pointer to channel data.
* \return DRXStatus_t.
*
* In case the tuner module is not used and in case of NTSC/FM the pogrammer
* must tune the tuner to the centre frequency of the NTSC/FM channel.
*
*/
static DRXStatus_t
CtrlSetChannel(pDRXDemodInstance_t demod, pDRXChannel_t channel)
{

	DRXFrequency_t tunerSetFreq = 0;
	DRXFrequency_t tunerGetFreq = 0;
	DRXFrequency_t tunerFreqOffset = 0;
	DRXFrequency_t intermediateFreq = 0;
	pDRXJData_t extAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;
	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;
	TUNERMode_t tunerMode = 0;
	pDRXCommonAttr_t commonAttr = NULL;
	Bool_t bridgeClosed = FALSE;
#ifndef DRXJ_VSB_ONLY
	u32_t minSymbolRate = 0;
	u32_t maxSymbolRate = 0;
	int bandwidthTemp = 0;
	int bandwidth = 0;
#endif
   /*== check arguments ======================================================*/
	if ((demod == NULL) || (channel == NULL)) {
		return DRX_STS_INVALID_ARG;
	}

	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	standard = extAttr->standard;

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
		return (DRX_STS_INVALID_ARG);
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
			return (DRX_STS_INVALID_ARG);
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
			return (DRX_STS_INVALID_ARG);
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
			return (DRX_STS_INVALID_ARG);
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
		DRXUIOCfg_t UIOCfg = { DRX_UIO1, DRX_UIO_MODE_FIRMWARE_SAW };
		int bwRolloffFactor = 0;

		bwRolloffFactor = (standard == DRX_STANDARD_ITU_A) ? 115 : 113;
		minSymbolRate = DRXJ_QAM_SYMBOLRATE_MIN;
		maxSymbolRate = DRXJ_QAM_SYMBOLRATE_MAX;
		/* config SMA_TX pin to SAW switch mode */
		CHK_ERROR(CtrlSetUIOCfg(demod, &UIOCfg));

		if (channel->symbolrate < minSymbolRate ||
		    channel->symbolrate > maxSymbolRate) {
			return (DRX_STS_INVALID_ARG);
		}

		switch (channel->constellation) {
		case DRX_CONSTELLATION_QAM16:	/* fall through */
		case DRX_CONSTELLATION_QAM32:	/* fall through */
		case DRX_CONSTELLATION_QAM64:	/* fall through */
		case DRX_CONSTELLATION_QAM128:	/* fall through */
		case DRX_CONSTELLATION_QAM256:
			bandwidthTemp = channel->symbolrate * bwRolloffFactor;
			bandwidth = bandwidthTemp / 100;

			if ((bandwidthTemp % 100) >= 50) {
				bandwidth++;
			}

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
			return (DRX_STS_INVALID_ARG);
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
			return (DRX_STS_INVALID_ARG);
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
			return (DRX_STS_INVALID_ARG);
		}
	}

	if ((extAttr->uioSmaTxMode) == DRX_UIO_MODE_FIRMWARE_SAW) {
		/* SAW SW, user UIO is used for switchable SAW */
		DRXUIOData_t uio1 = { DRX_UIO1, FALSE };

		switch (channel->bandwidth) {
		case DRX_BANDWIDTH_8MHZ:
			uio1.value = TRUE;
			break;
		case DRX_BANDWIDTH_7MHZ:
			uio1.value = FALSE;
			break;
		case DRX_BANDWIDTH_6MHZ:
			uio1.value = FALSE;
			break;
		case DRX_BANDWIDTH_UNKNOWN:
		default:
			return (DRX_STS_INVALID_ARG);
		}

		CHK_ERROR(CtrlUIOWrite(demod, &uio1));
	}
#endif /* DRXJ_VSB_ONLY */
	WR16(devAddr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE);
   /*== Tune, fast mode ======================================================*/
	if (demod->myTuner != NULL) {
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
			tunerMode |= TUNER_MODE_ANALOG;
			tunerSetFreq = channel->frequency;
			break;
		case DRX_STANDARD_FM:
			/* center frequency (equals sound carrier) as input,
			   tune to edge of SAW */
			tunerMode |= TUNER_MODE_ANALOG;
			tunerSetFreq =
			    channel->frequency + DRXJ_FM_CARRIER_FREQ_OFFSET;
			break;
#endif
		case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
		case DRX_STANDARD_ITU_A:	/* fallthrough */
		case DRX_STANDARD_ITU_B:	/* fallthrough */
		case DRX_STANDARD_ITU_C:
#endif
			tunerMode |= TUNER_MODE_DIGITAL;
			tunerSetFreq = channel->frequency;
			break;
		case DRX_STANDARD_UNKNOWN:
		default:
			return (DRX_STS_ERROR);
		}		/* switch(standard) */

		tunerMode |= TUNER_MODE_SWITCH;
		switch (channel->bandwidth) {
		case DRX_BANDWIDTH_8MHZ:
			tunerMode |= TUNER_MODE_8MHZ;
			break;
		case DRX_BANDWIDTH_7MHZ:
			tunerMode |= TUNER_MODE_7MHZ;
			break;
		case DRX_BANDWIDTH_6MHZ:
			tunerMode |= TUNER_MODE_6MHZ;
			break;
		default:
			/* TODO: for FM which bandwidth to use ?
			   also check offset from centre frequency ?
			   For now using 6MHz.
			 */
			tunerMode |= TUNER_MODE_6MHZ;
			break;
			/* return (DRX_STS_INVALID_ARG); */
		}

		/* store bandwidth for GetChannel() */
		extAttr->currBandwidth = channel->bandwidth;
		extAttr->currSymbolRate = channel->symbolrate;
		extAttr->frequency = tunerSetFreq;
		if (commonAttr->tunerPortNr == 1) {
			/* close tuner bridge */
			bridgeClosed = TRUE;
			CHK_ERROR(CtrlI2CBridge(demod, &bridgeClosed));
			/* set tuner frequency */
		}

		CHK_ERROR(DRXBSP_TUNER_SetFrequency(demod->myTuner,
						    tunerMode, tunerSetFreq));
		if (commonAttr->tunerPortNr == 1) {
			/* open tuner bridge */
			bridgeClosed = FALSE;
			CHK_ERROR(CtrlI2CBridge(demod, &bridgeClosed));
		}

		/* Get actual frequency set by tuner and compute offset */
		CHK_ERROR(DRXBSP_TUNER_GetFrequency(demod->myTuner,
						    0,
						    &tunerGetFreq,
						    &intermediateFreq));
		tunerFreqOffset = tunerGetFreq - tunerSetFreq;
		commonAttr->intermediateFreq = intermediateFreq;
	} else {
		/* no tuner instance defined, use fixed intermediate frequency */
		tunerFreqOffset = 0;
		intermediateFreq = demod->myCommonAttr->intermediateFreq;
	}			/* if ( demod->myTuner != NULL ) */

   /*== Setup demod for specific standard ====================================*/
	switch (standard) {
	case DRX_STANDARD_8VSB:
		if (channel->mirror == DRX_MIRROR_AUTO) {
			extAttr->mirror = DRX_MIRROR_NO;
		} else {
			extAttr->mirror = channel->mirror;
		}
		CHK_ERROR(SetVSB(demod));
		CHK_ERROR(SetFrequency(demod, channel, tunerFreqOffset));
		break;
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:
		if (channel->mirror == DRX_MIRROR_AUTO) {
			extAttr->mirror = DRX_MIRROR_NO;
		} else {
			extAttr->mirror = channel->mirror;
		}
		CHK_ERROR(SetATVChannel(demod,
					tunerFreqOffset, channel, standard));
		break;
#endif
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		CHK_ERROR(SetQAMChannel(demod, channel, tunerFreqOffset));
		break;
#endif
	case DRX_STANDARD_UNKNOWN:
	default:
		return (DRX_STS_ERROR);
	}

   /*== Re-tune, slow mode ===================================================*/
	if (demod->myTuner != NULL) {
		/* tune to slow mode */
		tunerMode &= ~TUNER_MODE_SWITCH;
		tunerMode |= TUNER_MODE_LOCK;

		if (commonAttr->tunerPortNr == 1) {
			/* close tuner bridge */
			bridgeClosed = TRUE;
			CHK_ERROR(CtrlI2CBridge(demod, &bridgeClosed));
		}

		/* set tuner frequency */
		CHK_ERROR(DRXBSP_TUNER_SetFrequency(demod->myTuner,
						    tunerMode, tunerSetFreq));
		if (commonAttr->tunerPortNr == 1) {
			/* open tuner bridge */
			bridgeClosed = FALSE;
			CHK_ERROR(CtrlI2CBridge(demod, &bridgeClosed));
		}
	}

	/* if ( demod->myTuner !=NULL ) */
	/* flag the packet error counter reset */
	extAttr->resetPktErrAcc = TRUE;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*=============================================================================
  ===== CtrlGetChannel() ==========================================================
  ===========================================================================*/
/**
* \fn DRXStatus_t CtrlGetChannel()
* \brief Retreive parameters of current transmission channel.
* \param demod   Pointer to demod instance.
* \param channel Pointer to channel data.
* \return DRXStatus_t.
*/
static DRXStatus_t
CtrlGetChannel(pDRXDemodInstance_t demod, pDRXChannel_t channel)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	DRXLockStatus_t lockStatus = DRX_NOT_LOCKED;
	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;
	pDRXCommonAttr_t commonAttr = NULL;
	DRXFrequency_t intermediateFreq = 0;
	s32_t CTLFreqOffset = 0;
	u32_t iqmRcRateLo = 0;
	u32_t adcFrequency = 0;
#ifndef DRXJ_VSB_ONLY
	int bandwidthTemp = 0;
	int bandwidth = 0;
#endif

	/* check arguments */
	if ((demod == NULL) || (channel == NULL)) {
		return DRX_STS_INVALID_ARG;
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	standard = extAttr->standard;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;

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

	if (demod->myTuner != NULL) {
		DRXFrequency_t tunerFreqOffset = 0;
		Bool_t tunerMirror = commonAttr->mirrorFreqSpect ? FALSE : TRUE;

		/* Get frequency from tuner */
		CHK_ERROR(DRXBSP_TUNER_GetFrequency(demod->myTuner,
						    0,
						    &(channel->frequency),
						    &intermediateFreq));
		tunerFreqOffset = channel->frequency - extAttr->frequency;
		if (tunerMirror == TRUE) {
			/* positive image */
			channel->frequency += tunerFreqOffset;
		} else {
			/* negative image */
			channel->frequency -= tunerFreqOffset;
		}

		/* Handle sound carrier offset in RF domain */
		if (standard == DRX_STANDARD_FM) {
			channel->frequency -= DRXJ_FM_CARRIER_FREQ_OFFSET;
		}
	} else {
		intermediateFreq = commonAttr->intermediateFreq;
	}

	/* check lock status */
	CHK_ERROR(CtrlLockStatus(demod, &lockStatus));
	if ((lockStatus == DRX_LOCKED) || (lockStatus == DRXJ_DEMOD_LOCK)) {
		ARR32(devAddr, IQM_RC_RATE_LO__A, &iqmRcRateLo);
		adcFrequency = (commonAttr->sysClockFreq * 1000) / 3;

		channel->symbolrate =
		    Frac28(adcFrequency, (iqmRcRateLo + (1 << 23))) >> 7;

		switch (standard) {
		case DRX_STANDARD_8VSB:
			channel->bandwidth = DRX_BANDWIDTH_6MHZ;
			/* get the channel frequency */
			CHK_ERROR(GetCTLFreqOffset(demod, &CTLFreqOffset));
			channel->frequency -= CTLFreqOffset;
			/* get the channel constellation */
			channel->constellation = DRX_CONSTELLATION_AUTO;
			break;
#ifndef DRXJ_VSB_ONLY
		case DRX_STANDARD_ITU_A:
		case DRX_STANDARD_ITU_B:
		case DRX_STANDARD_ITU_C:
			{
				/* get the channel frequency */
				CHK_ERROR(GetCTLFreqOffset
					  (demod, &CTLFreqOffset));
				channel->frequency -= CTLFreqOffset;

				if (standard == DRX_STANDARD_ITU_B) {
					channel->bandwidth = DRX_BANDWIDTH_6MHZ;
				} else {
					/* annex A & C */

					u32_t rollOff = 113;	/* default annex C */

					if (standard == DRX_STANDARD_ITU_A) {
						rollOff = 115;
					}

					bandwidthTemp =
					    channel->symbolrate * rollOff;
					bandwidth = bandwidthTemp / 100;

					if ((bandwidthTemp % 100) >= 50) {
						bandwidth++;
					}

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
					DRXJSCUCmd_t cmdSCU =
					    { /* command      */ 0,
						/* parameterLen */ 0,
						/* resultLen    */ 0,
						/* parameter    */ NULL,
						/* result       */ NULL
					};
					u16_t cmdResult[3] = { 0, 0, 0 };

					cmdSCU.command =
					    SCU_RAM_COMMAND_STANDARD_QAM |
					    SCU_RAM_COMMAND_CMD_DEMOD_GET_PARAM;
					cmdSCU.parameterLen = 0;
					cmdSCU.resultLen = 3;
					cmdSCU.parameter = NULL;
					cmdSCU.result = cmdResult;
					CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

					channel->interleavemode =
					    (DRXInterleaveModes_t) (cmdSCU.
								    result[2]);
				}

				switch (extAttr->constellation) {
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
					return (DRX_STS_ERROR);
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
			CHK_ERROR(GetATVChannel(demod, channel, standard));
			break;
#endif
		case DRX_STANDARD_UNKNOWN:	/* fall trough */
		default:
			return (DRX_STS_ERROR);
		}		/* switch ( standard ) */

		if (lockStatus == DRX_LOCKED) {
			channel->mirror = extAttr->mirror;
		}
	}
	/* if ( lockStatus == DRX_LOCKED ) */
	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*=============================================================================
  ===== SigQuality() ==========================================================
  ===========================================================================*/

static u16_t
mer2indicator(u16_t mer, u16_t minMer, u16_t thresholdMer, u16_t maxMer)
{
	u16_t indicator = 0;

	if (mer < minMer) {
		indicator = 0;
	} else if (mer < thresholdMer) {
		if ((thresholdMer - minMer) != 0) {
			indicator =
			    25 * (mer - minMer) / (thresholdMer - minMer);
		}
	} else if (mer < maxMer) {
		if ((maxMer - thresholdMer) != 0) {
			indicator =
			    25 + 75 * (mer - thresholdMer) / (maxMer -
							      thresholdMer);
		} else {
			indicator = 25;
		}
	} else {
		indicator = 100;
	}

	return indicator;
}

/**
* \fn DRXStatus_t CtrlSigQuality()
* \brief Retreive signal quality form device.
* \param devmod Pointer to demodulator instance.
* \param sigQuality Pointer to signal quality data.
* \return DRXStatus_t.
* \retval DRX_STS_OK sigQuality contains valid data.
* \retval DRX_STS_INVALID_ARG sigQuality is NULL.
* \retval DRX_STS_ERROR Erroneous data, sigQuality contains invalid data.

*/
static DRXStatus_t
CtrlSigQuality(pDRXDemodInstance_t demod, pDRXSigQuality_t sigQuality)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;
	DRXLockStatus_t lockStatus = DRX_NOT_LOCKED;
	u16_t minMer = 0;
	u16_t maxMer = 0;
	u16_t thresholdMer = 0;

	/* Check arguments */
	if ((sigQuality == NULL) || (demod == NULL)) {
		return (DRX_STS_INVALID_ARG);
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	standard = extAttr->standard;

	/* get basic information */
	devAddr = demod->myI2CDevAddr;
	CHK_ERROR(CtrlLockStatus(demod, &lockStatus));
	switch (standard) {
	case DRX_STANDARD_8VSB:
#ifdef DRXJ_SIGNAL_ACCUM_ERR
		CHK_ERROR(GetAccPktErr(demod, &sigQuality->packetError));
#else
		CHK_ERROR(GetVSBPostRSPckErr
			  (devAddr, &sigQuality->packetError));
#endif
		if (lockStatus != DRXJ_DEMOD_LOCK && lockStatus != DRX_LOCKED) {
			sigQuality->postViterbiBER = 500000;
			sigQuality->MER = 20;
			sigQuality->preViterbiBER = 0;
		} else {
			/* PostViterbi is compute in steps of 10^(-6) */
			CHK_ERROR(GetVSBpreViterbiBer
				  (devAddr, &sigQuality->preViterbiBER));
			CHK_ERROR(GetVSBpostViterbiBer
				  (devAddr, &sigQuality->postViterbiBER));
			CHK_ERROR(GetVSBMER(devAddr, &sigQuality->MER));
		}
		minMer = 20;
		maxMer = 360;
		thresholdMer = 145;
		sigQuality->postReedSolomonBER = 0;
		sigQuality->scaleFactorBER = 1000000;
		sigQuality->indicator =
		    mer2indicator(sigQuality->MER, minMer, thresholdMer,
				  maxMer);
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		CHK_ERROR(CtrlGetQAMSigQuality(demod, sigQuality));
		if (lockStatus != DRXJ_DEMOD_LOCK && lockStatus != DRX_LOCKED) {
			switch (extAttr->constellation) {
			case DRX_CONSTELLATION_QAM256:
				sigQuality->MER = 210;
				break;
			case DRX_CONSTELLATION_QAM128:
				sigQuality->MER = 180;
				break;
			case DRX_CONSTELLATION_QAM64:
				sigQuality->MER = 150;
				break;
			case DRX_CONSTELLATION_QAM32:
				sigQuality->MER = 120;
				break;
			case DRX_CONSTELLATION_QAM16:
				sigQuality->MER = 90;
				break;
			default:
				sigQuality->MER = 0;
				return (DRX_STS_ERROR);
			}
		}

		switch (extAttr->constellation) {
		case DRX_CONSTELLATION_QAM256:
			minMer = 210;
			thresholdMer = 270;
			maxMer = 380;
			break;
		case DRX_CONSTELLATION_QAM64:
			minMer = 150;
			thresholdMer = 210;
			maxMer = 380;
			break;
		case DRX_CONSTELLATION_QAM128:
		case DRX_CONSTELLATION_QAM32:
		case DRX_CONSTELLATION_QAM16:
			break;
		default:
			return (DRX_STS_ERROR);
		}
		sigQuality->indicator =
		    mer2indicator(sigQuality->MER, minMer, thresholdMer,
				  maxMer);
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
	case DRX_STANDARD_PAL_SECAM_L:
	case DRX_STANDARD_PAL_SECAM_LP:
	case DRX_STANDARD_NTSC:
		CHK_ERROR(AtvSigQuality(demod, sigQuality));
		break;
	case DRX_STANDARD_FM:
		CHK_ERROR(FmSigQuality(demod, sigQuality));
		break;
#endif
	default:
		return (DRX_STS_ERROR);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlLockStatus()
* \brief Retreive lock status .
* \param devAddr Pointer to demodulator device address.
* \param lockStat Pointer to lock status structure.
* \return DRXStatus_t.
*
*/
static DRXStatus_t
CtrlLockStatus(pDRXDemodInstance_t demod, pDRXLockStatus_t lockStat)
{
	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;
	pDRXJData_t extAttr = NULL;
	struct i2c_device_addr *devAddr = NULL;
	DRXJSCUCmd_t cmdSCU = { /* command      */ 0,
		/* parameterLen */ 0,
		/* resultLen    */ 0,
		/* *parameter   */ NULL,
		/* *result      */ NULL
	};
	u16_t cmdResult[2] = { 0, 0 };
	u16_t demodLock = SCU_RAM_PARAM_1_RES_DEMOD_GET_LOCK_DEMOD_LOCKED;

	/* check arguments */
	if ((demod == NULL) || (lockStat == NULL)) {
		return (DRX_STS_INVALID_ARG);
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	standard = extAttr->standard;

	*lockStat = DRX_NOT_LOCKED;

	/* define the SCU command code */
	switch (standard) {
	case DRX_STANDARD_8VSB:
		cmdSCU.command = SCU_RAM_COMMAND_STANDARD_VSB |
		    SCU_RAM_COMMAND_CMD_DEMOD_GET_LOCK;
		demodLock |= 0x6;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		cmdSCU.command = SCU_RAM_COMMAND_STANDARD_QAM |
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
		cmdSCU.command = SCU_RAM_COMMAND_STANDARD_ATV |
		    SCU_RAM_COMMAND_CMD_DEMOD_GET_LOCK;
		break;
	case DRX_STANDARD_FM:
		return FmLockStatus(demod, lockStat);
#endif
	case DRX_STANDARD_UNKNOWN:	/* fallthrough */
	default:
		return (DRX_STS_ERROR);
	}

	/* define the SCU command paramters and execute the command */
	cmdSCU.parameterLen = 0;
	cmdSCU.resultLen = 2;
	cmdSCU.parameter = NULL;
	cmdSCU.result = cmdResult;
	CHK_ERROR(SCUCommand(devAddr, &cmdSCU));

	/* set the lock status */
	if (cmdSCU.result[1] < demodLock) {
		/* 0x0000 NOT LOCKED */
		*lockStat = DRX_NOT_LOCKED;
	} else if (cmdSCU.result[1] < SCU_RAM_PARAM_1_RES_DEMOD_GET_LOCK_LOCKED) {
		*lockStat = DRXJ_DEMOD_LOCK;
	} else if (cmdSCU.result[1] <
		   SCU_RAM_PARAM_1_RES_DEMOD_GET_LOCK_NEVER_LOCK) {
		/* 0x8000 DEMOD + FEC LOCKED (system lock) */
		*lockStat = DRX_LOCKED;
	} else {
		/* 0xC000 NEVER LOCKED */
		/* (system will never be able to lock to the signal) */
		*lockStat = DRX_NEVER_LOCK;
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlConstel()
* \brief Retreive a constellation point via I2C.
* \param demod Pointer to demodulator instance.
* \param complexNr Pointer to the structure in which to store the
		   constellation point.
* \return DRXStatus_t.
*/
static DRXStatus_t
CtrlConstel(pDRXDemodInstance_t demod, pDRXComplex_t complexNr)
{
	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;
						     /**< active standard */

	/* check arguments */
	if ((demod == NULL) || (complexNr == NULL)) {
		return (DRX_STS_INVALID_ARG);
	}

	/* read device info */
	standard = ((pDRXJData_t) demod->myExtAttr)->standard;

	/* Read constellation point  */
	switch (standard) {
	case DRX_STANDARD_8VSB:
		CHK_ERROR(CtrlGetVSBConstel(demod, complexNr));
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		CHK_ERROR(CtrlGetQAMConstel(demod, complexNr));
		break;
#endif
	case DRX_STANDARD_UNKNOWN:
	default:
		return (DRX_STS_ERROR);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlSetStandard()
* \brief Set modulation standard to be used.
* \param standard Modulation standard.
* \return DRXStatus_t.
*
* Setup stuff for the desired demodulation standard.
* Disable and power down the previous selected demodulation standard
*
*/
static DRXStatus_t
CtrlSetStandard(pDRXDemodInstance_t demod, pDRXStandard_t standard)
{
	pDRXJData_t extAttr = NULL;
	DRXStandard_t prevStandard;

	/* check arguments */
	if ((standard == NULL) || (demod == NULL)) {
		return (DRX_STS_INVALID_ARG);
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	prevStandard = extAttr->standard;

	/*
	   Stop and power down previous standard
	 */
	switch (prevStandard) {
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		CHK_ERROR(PowerDownQAM(demod, FALSE));
		break;
#endif
	case DRX_STANDARD_8VSB:
		CHK_ERROR(PowerDownVSB(demod, FALSE));
		break;
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:
		CHK_ERROR(PowerDownATV(demod, prevStandard, FALSE));
		break;
#endif
	case DRX_STANDARD_UNKNOWN:
		/* Do nothing */
		break;
	case DRX_STANDARD_AUTO:	/* fallthrough */
	default:
		return (DRX_STS_INVALID_ARG);
	}

	/*
	   Initialize channel independent registers
	   Power up new standard
	 */
	extAttr->standard = *standard;

	switch (*standard) {
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		DUMMY_READ();
		break;
#endif
	case DRX_STANDARD_8VSB:
		CHK_ERROR(SetVSBLeakNGain(demod));
		break;
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:
		CHK_ERROR(SetATVStandard(demod, standard));
		CHK_ERROR(PowerUpATV(demod, *standard));
		break;
#endif
	default:
		extAttr->standard = DRX_STANDARD_UNKNOWN;
		return (DRX_STS_INVALID_ARG);
		break;
	}

	return (DRX_STS_OK);
rw_error:
	/* Don't know what the standard is now ... try again */
	extAttr->standard = DRX_STANDARD_UNKNOWN;
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetStandard()
* \brief Get modulation standard currently used to demodulate.
* \param standard Modulation standard.
* \return DRXStatus_t.
*
* Returns 8VSB, NTSC, QAM only.
*
*/
static DRXStatus_t
CtrlGetStandard(pDRXDemodInstance_t demod, pDRXStandard_t standard)
{
	pDRXJData_t extAttr = NULL;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* check arguments */
	if (standard == NULL) {
		return (DRX_STS_INVALID_ARG);
	}
	(*standard) = extAttr->standard;
	DUMMY_READ();

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetCfgSymbolClockOffset()
* \brief Get frequency offsets of STR.
* \param pointer to s32_t.
* \return DRXStatus_t.
*
*/
static DRXStatus_t
CtrlGetCfgSymbolClockOffset(pDRXDemodInstance_t demod, ps32_t rateOffset)
{
	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	/* check arguments */
	if (rateOffset == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	standard = extAttr->standard;

	switch (standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
		CHK_ERROR(GetSTRFreqOffset(demod, rateOffset));
		break;
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_UNKNOWN:
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlPowerMode()
* \brief Set the power mode of the device to the specified power mode
* \param demod Pointer to demodulator instance.
* \param mode  Pointer to new power mode.
* \return DRXStatus_t.
* \retval DRX_STS_OK          Success
* \retval DRX_STS_ERROR       I2C error or other failure
* \retval DRX_STS_INVALID_ARG Invalid mode argument.
*
*
*/
static DRXStatus_t
CtrlPowerMode(pDRXDemodInstance_t demod, pDRXPowerMode_t mode)
{
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) NULL;
	pDRXJData_t extAttr = (pDRXJData_t) NULL;
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) NULL;
	u16_t sioCcPwdMode = 0;

	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	devAddr = demod->myI2CDevAddr;

	/* Check arguments */
	if (mode == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	/* If already in requested power mode, do nothing */
	if (commonAttr->currentPowerMode == *mode) {
		return (DRX_STS_OK);
	}

	switch (*mode) {
	case DRX_POWER_UP:
	case DRXJ_POWER_DOWN_MAIN_PATH:
		sioCcPwdMode = SIO_CC_PWD_MODE_LEVEL_NONE;
		break;
	case DRXJ_POWER_DOWN_CORE:
		sioCcPwdMode = SIO_CC_PWD_MODE_LEVEL_CLOCK;
		break;
	case DRXJ_POWER_DOWN_PLL:
		sioCcPwdMode = SIO_CC_PWD_MODE_LEVEL_PLL;
		break;
	case DRX_POWER_DOWN:
		sioCcPwdMode = SIO_CC_PWD_MODE_LEVEL_OSC;
		break;
	default:
		/* Unknow sleep mode */
		return (DRX_STS_INVALID_ARG);
		break;
	}

	/* Check if device needs to be powered up */
	if ((commonAttr->currentPowerMode != DRX_POWER_UP)) {
		CHK_ERROR(PowerUpDevice(demod));
	}

	if ((*mode == DRX_POWER_UP)) {
		/* Restore analog & pin configuartion */
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

		switch (extAttr->standard) {
		case DRX_STANDARD_ITU_A:
		case DRX_STANDARD_ITU_B:
		case DRX_STANDARD_ITU_C:
			CHK_ERROR(PowerDownQAM(demod, TRUE));
			break;
		case DRX_STANDARD_8VSB:
			CHK_ERROR(PowerDownVSB(demod, TRUE));
			break;
		case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
		case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
		case DRX_STANDARD_NTSC:	/* fallthrough */
		case DRX_STANDARD_FM:
			CHK_ERROR(PowerDownATV(demod, extAttr->standard, TRUE));
			break;
		case DRX_STANDARD_UNKNOWN:
			/* Do nothing */
			break;
		case DRX_STANDARD_AUTO:	/* fallthrough */
		default:
			return (DRX_STS_ERROR);
		}

		if (*mode != DRXJ_POWER_DOWN_MAIN_PATH) {
			WR16(devAddr, SIO_CC_PWD_MODE__A, sioCcPwdMode);
			WR16(devAddr, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY);

			/* Initialize HI, wakeup key especially before put IC to sleep */
			CHK_ERROR(InitHI(demod));

			extAttr->HICfgCtrl |= SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ;
			CHK_ERROR(HICfgCommand(demod));
		}
	}

	commonAttr->currentPowerMode = *mode;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlVersion()
* \brief Report version of microcode and if possible version of device
* \param demod Pointer to demodulator instance.
* \param versionList Pointer to pointer of linked list of versions.
* \return DRXStatus_t.
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
static DRXStatus_t
CtrlVersion(pDRXDemodInstance_t demod, pDRXVersionList_t * versionList)
{
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) (NULL);
	u16_t ucodeMajorMinor = 0;	/* BCD Ma:Ma:Ma:Mi */
	u16_t ucodePatch = 0;	/* BCD Pa:Pa:Pa:Pa */
	u16_t major = 0;
	u16_t minor = 0;
	u16_t patch = 0;
	u16_t idx = 0;
	u32_t jtag = 0;
	u16_t subtype = 0;
	u16_t mfx = 0;
	u16_t bid = 0;
	u16_t key = 0;

	static char ucodeName[] = "Microcode";
	static char deviceName[] = "Device";

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;

	/* Microcode version *************************************** */

	extAttr->vVersion[0].moduleType = DRX_MODULE_MICROCODE;
	extAttr->vVersion[0].moduleName = ucodeName;
	extAttr->vVersion[0].vString = extAttr->vText[0];

	if (commonAttr->isOpened == TRUE) {
		SARR16(devAddr, SCU_RAM_VERSION_HI__A, &ucodeMajorMinor);
		SARR16(devAddr, SCU_RAM_VERSION_LO__A, &ucodePatch);

		/* Translate BCD to numbers and string */
		/* TODO: The most significant Ma and Pa will be ignored, check with spec */
		minor = (ucodeMajorMinor & 0xF);
		ucodeMajorMinor >>= 4;
		major = (ucodeMajorMinor & 0xF);
		ucodeMajorMinor >>= 4;
		major += (10 * (ucodeMajorMinor & 0xF));
		patch = (ucodePatch & 0xF);
		ucodePatch >>= 4;
		patch += (10 * (ucodePatch & 0xF));
		ucodePatch >>= 4;
		patch += (100 * (ucodePatch & 0xF));
	} else {
		/* No microcode uploaded, No Rom existed, set version to 0.0.0 */
		patch = 0;
		minor = 0;
		major = 0;
	}
	extAttr->vVersion[0].vMajor = major;
	extAttr->vVersion[0].vMinor = minor;
	extAttr->vVersion[0].vPatch = patch;

	if (major / 10 != 0) {
		extAttr->vVersion[0].vString[idx++] =
		    ((char)(major / 10)) + '0';
		major %= 10;
	}
	extAttr->vVersion[0].vString[idx++] = ((char)major) + '0';
	extAttr->vVersion[0].vString[idx++] = '.';
	extAttr->vVersion[0].vString[idx++] = ((char)minor) + '0';
	extAttr->vVersion[0].vString[idx++] = '.';
	if (patch / 100 != 0) {
		extAttr->vVersion[0].vString[idx++] =
		    ((char)(patch / 100)) + '0';
		patch %= 100;
	}
	if (patch / 10 != 0) {
		extAttr->vVersion[0].vString[idx++] =
		    ((char)(patch / 10)) + '0';
		patch %= 10;
	}
	extAttr->vVersion[0].vString[idx++] = ((char)patch) + '0';
	extAttr->vVersion[0].vString[idx] = '\0';

	extAttr->vListElements[0].version = &(extAttr->vVersion[0]);
	extAttr->vListElements[0].next = &(extAttr->vListElements[1]);

	/* Device version *************************************** */
	/* Check device id */
	RR16(devAddr, SIO_TOP_COMM_KEY__A, &key);
	WR16(devAddr, SIO_TOP_COMM_KEY__A, 0xFABA);
	RR32(devAddr, SIO_TOP_JTAGID_LO__A, &jtag);
	RR16(devAddr, SIO_PDR_UIO_IN_HI__A, &bid);
	WR16(devAddr, SIO_TOP_COMM_KEY__A, key);

	extAttr->vVersion[1].moduleType = DRX_MODULE_DEVICE;
	extAttr->vVersion[1].moduleName = deviceName;
	extAttr->vVersion[1].vString = extAttr->vText[1];
	extAttr->vVersion[1].vString[0] = 'D';
	extAttr->vVersion[1].vString[1] = 'R';
	extAttr->vVersion[1].vString[2] = 'X';
	extAttr->vVersion[1].vString[3] = '3';
	extAttr->vVersion[1].vString[4] = '9';
	extAttr->vVersion[1].vString[7] = 'J';
	extAttr->vVersion[1].vString[8] = ':';
	extAttr->vVersion[1].vString[11] = '\0';

	/* DRX39xxJ type Ax */
	/* TODO semantics of mfx and spin are unclear */
	subtype = (u16_t) ((jtag >> 12) & 0xFF);
	mfx = (u16_t) (jtag >> 29);
	extAttr->vVersion[1].vMinor = 1;
	if (mfx == 0x03) {
		extAttr->vVersion[1].vPatch = mfx + 2;
	} else {
		extAttr->vVersion[1].vPatch = mfx + 1;
	}
	extAttr->vVersion[1].vString[6] = ((char)(subtype & 0xF)) + '0';
	extAttr->vVersion[1].vMajor = (subtype & 0x0F);
	subtype >>= 4;
	extAttr->vVersion[1].vString[5] = ((char)(subtype & 0xF)) + '0';
	extAttr->vVersion[1].vMajor += 10 * subtype;
	extAttr->vVersion[1].vString[9] = 'A';
	if (mfx == 0x03) {
		extAttr->vVersion[1].vString[10] = ((char)(mfx & 0xF)) + '2';
	} else {
		extAttr->vVersion[1].vString[10] = ((char)(mfx & 0xF)) + '1';
	}

	extAttr->vListElements[1].version = &(extAttr->vVersion[1]);
	extAttr->vListElements[1].next = (pDRXVersionList_t) (NULL);

	*versionList = &(extAttr->vListElements[0]);

	return (DRX_STS_OK);

rw_error:
	*versionList = (pDRXVersionList_t) (NULL);
	return (DRX_STS_ERROR);

}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlProbeDevice()
* \brief Probe device, check if it is present
* \param demod Pointer to demodulator instance.
* \return DRXStatus_t.
* \retval DRX_STS_OK    a drx39xxj device has been detected.
* \retval DRX_STS_ERROR no drx39xxj device detected.
*
* This funtion can be caled before open() and after close().
*
*/

static DRXStatus_t CtrlProbeDevice(pDRXDemodInstance_t demod)
{
	DRXPowerMode_t orgPowerMode = DRX_POWER_UP;
	DRXStatus_t retStatus = DRX_STS_OK;
	pDRXCommonAttr_t commonAttr = (pDRXCommonAttr_t) (NULL);

	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;

	if (commonAttr->isOpened == FALSE
	    || commonAttr->currentPowerMode != DRX_POWER_UP) {
		struct i2c_device_addr *devAddr = NULL;
		DRXPowerMode_t powerMode = DRX_POWER_UP;
		u32_t jtag = 0;

		devAddr = demod->myI2CDevAddr;

		/* Remeber original power mode */
		orgPowerMode = commonAttr->currentPowerMode;

		if (demod->myCommonAttr->isOpened == FALSE) {
			CHK_ERROR(PowerUpDevice(demod));
			commonAttr->currentPowerMode = DRX_POWER_UP;
		} else {
			/* Wake-up device, feedback from device */
			CHK_ERROR(CtrlPowerMode(demod, &powerMode));
		}
		/* Initialize HI, wakeup key especially */
		CHK_ERROR(InitHI(demod));

		/* Check device id */
		RR32(devAddr, SIO_TOP_JTAGID_LO__A, &jtag);
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
			retStatus = DRX_STS_ERROR;
			break;
		}

		/* Device was not opened, return to orginal powermode,
		   feedback from device */
		CHK_ERROR(CtrlPowerMode(demod, &orgPowerMode));
	} else {
		/* dummy read to make this function fail in case device
		   suddenly disappears after a succesful DRX_Open */
		DUMMY_READ();
	}

	return (retStatus);

rw_error:
	commonAttr->currentPowerMode = orgPowerMode;
	return (DRX_STS_ERROR);
}

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
/*============================================================================*/

/**
* \fn DRXStatus_t IsMCBlockAudio()
* \brief Check if MC block is Audio or not Audio.
* \param addr        Pointer to demodulator instance.
* \param audioUpload TRUE  if MC block is Audio
		     FALSE if MC block not Audio
* \return Bool_t.
*/
Bool_t IsMCBlockAudio(u32_t addr)
{
	if ((addr == AUD_XFP_PRAM_4K__A) || (addr == AUD_XDFP_PRAM_4K__A)) {
		return (TRUE);
	}
	return (FALSE);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlUCodeUpload()
* \brief Handle Audio or !Audio part of microcode upload.
* \param demod          Pointer to demodulator instance.
* \param mcInfo         Pointer to information about microcode data.
* \param action         Either UCODE_UPLOAD or UCODE_VERIFY.
* \param uploadAudioMC  TRUE  if Audio MC need to be uploaded.
			FALSE if !Audio MC need to be uploaded.
* \return DRXStatus_t.
*/
static DRXStatus_t
CtrlUCodeUpload(pDRXDemodInstance_t demod,
		pDRXUCodeInfo_t mcInfo,
		DRXUCodeAction_t action, Bool_t uploadAudioMC)
{
	u16_t i = 0;
	u16_t mcNrOfBlks = 0;
	u16_t mcMagicWord = 0;
	pu8_t mcData = (pu8_t) (NULL);
	struct i2c_device_addr *devAddr = (struct i2c_device_addr *) (NULL);
	pDRXJData_t extAttr = (pDRXJData_t) (NULL);

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* Check arguments */
	if ((mcInfo == NULL) ||
	    (mcInfo->mcData == NULL) || (mcInfo->mcSize == 0)) {
		return DRX_STS_INVALID_ARG;
	}

	mcData = mcInfo->mcData;

	/* Check data */
	mcMagicWord = UCodeRead16(mcData);
	mcData += sizeof(u16_t);
	mcNrOfBlks = UCodeRead16(mcData);
	mcData += sizeof(u16_t);

	if ((mcMagicWord != DRXJ_UCODE_MAGIC_WORD) || (mcNrOfBlks == 0)) {
		/* wrong endianess or wrong data ? */
		return DRX_STS_INVALID_ARG;
	}

	/* Process microcode blocks */
	for (i = 0; i < mcNrOfBlks; i++) {
		DRXUCodeBlockHdr_t blockHdr;
		u16_t mcBlockNrBytes = 0;

		/* Process block header */
		blockHdr.addr = UCodeRead32(mcData);
		mcData += sizeof(u32_t);
		blockHdr.size = UCodeRead16(mcData);
		mcData += sizeof(u16_t);
		blockHdr.flags = UCodeRead16(mcData);
		mcData += sizeof(u16_t);
		blockHdr.CRC = UCodeRead16(mcData);
		mcData += sizeof(u16_t);

		/* Check block header on:
		   - no data
		   - data larger then 64Kb
		   - if CRC enabled check CRC
		 */
		if ((blockHdr.size == 0) ||
		    (blockHdr.size > 0x7FFF) ||
		    (((blockHdr.flags & DRXJ_UCODE_CRC_FLAG) != 0) &&
		     (blockHdr.CRC != UCodeComputeCRC(mcData, blockHdr.size)))
		    ) {
			/* Wrong data ! */
			return DRX_STS_INVALID_ARG;
		}

		mcBlockNrBytes = blockHdr.size * sizeof(u16_t);

		/* Perform the desired action */
		/* Check which part of MC need to be uploaded - Audio or not Audio */
		if (IsMCBlockAudio(blockHdr.addr) == uploadAudioMC) {
			switch (action) {
	    /*===================================================================*/
			case UCODE_UPLOAD:
				{
					/* Upload microcode */
					if (demod->myAccessFunct->
					    writeBlockFunc(devAddr,
							   (DRXaddr_t) blockHdr.
							   addr, mcBlockNrBytes,
							   mcData,
							   0x0000) !=
					    DRX_STS_OK) {
						return (DRX_STS_ERROR);
					}
				};
				break;

	    /*===================================================================*/
			case UCODE_VERIFY:
				{
					int result = 0;
					u8_t mcDataBuffer
					    [DRXJ_UCODE_MAX_BUF_SIZE];
					u32_t bytesToCompare = 0;
					u32_t bytesLeftToCompare = 0;
					DRXaddr_t currAddr = (DRXaddr_t) 0;
					pu8_t currPtr = NULL;

					bytesLeftToCompare = mcBlockNrBytes;
					currAddr = blockHdr.addr;
					currPtr = mcData;

					while (bytesLeftToCompare != 0) {
						if (bytesLeftToCompare >
						    ((u32_t)
						     DRXJ_UCODE_MAX_BUF_SIZE)) {
							bytesToCompare =
							    ((u32_t)
							     DRXJ_UCODE_MAX_BUF_SIZE);
						} else {
							bytesToCompare =
							    bytesLeftToCompare;
						}

						if (demod->myAccessFunct->
						    readBlockFunc(devAddr,
								  currAddr,
								  (u16_t)
								  bytesToCompare,
								  (pu8_t)
								  mcDataBuffer,
								  0x0000) !=
						    DRX_STS_OK) {
							return (DRX_STS_ERROR);
						}

						result =
						    DRXBSP_HST_Memcmp(currPtr,
								      mcDataBuffer,
								      bytesToCompare);

						if (result != 0) {
							return (DRX_STS_ERROR);
						};

						currAddr +=
						    ((DRXaddr_t)
						     (bytesToCompare / 2));
						currPtr =
						    &(currPtr[bytesToCompare]);
						bytesLeftToCompare -=
						    ((u32_t) bytesToCompare);
					}	/* while( bytesToCompare > DRXJ_UCODE_MAX_BUF_SIZE ) */
				};
				break;

	    /*===================================================================*/
			default:
				return DRX_STS_INVALID_ARG;
				break;

			}	/* switch ( action ) */
		}

		/* if( IsMCBlockAudio( blockHdr.addr ) == uploadAudioMC ) */
		/* Next block */
		mcData += mcBlockNrBytes;
	}			/* for( i = 0 ; i<mcNrOfBlks ; i++ ) */

	if (uploadAudioMC == FALSE) {
		extAttr->flagAudMcUploaded = FALSE;
	}

	return (DRX_STS_OK);
}
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */

/*============================================================================*/
/*== CTRL Set/Get Config related functions ===================================*/
/*============================================================================*/

/*===== SigStrength() =========================================================*/
/**
* \fn DRXStatus_t CtrlSigStrength()
* \brief Retrieve signal strength.
* \param devmod Pointer to demodulator instance.
* \param sigQuality Pointer to signal strength data; range 0, .. , 100.
* \return DRXStatus_t.
* \retval DRX_STS_OK sigStrength contains valid data.
* \retval DRX_STS_INVALID_ARG sigStrength is NULL.
* \retval DRX_STS_ERROR Erroneous data, sigStrength contains invalid data.

*/
static DRXStatus_t
CtrlSigStrength(pDRXDemodInstance_t demod, pu16_t sigStrength)
{
	pDRXJData_t extAttr = NULL;
	DRXStandard_t standard = DRX_STANDARD_UNKNOWN;

	/* Check arguments */
	if ((sigStrength == NULL) || (demod == NULL)) {
		return (DRX_STS_INVALID_ARG);
	}

	extAttr = (pDRXJData_t) demod->myExtAttr;
	standard = extAttr->standard;
	*sigStrength = 0;

	/* Signal strength indication for each standard */
	switch (standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
		CHK_ERROR(GetSigStrength(demod, sigStrength));
		break;
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:	/* fallthrough */
	case DRX_STANDARD_FM:
		CHK_ERROR(GetAtvSigStrength(demod, sigStrength));
		break;
#endif
	case DRX_STANDARD_UNKNOWN:	/* fallthrough */
	default:
		return (DRX_STS_INVALID_ARG);
	}

	/* TODO */
	/* find out if signal strength is calculated in the same way for all standards */
	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/
/**
* \fn DRXStatus_t CtrlGetCfgOOBMisc()
* \brief Get current state information of OOB.
* \param pointer to DRXJCfgOOBMisc_t.
* \return DRXStatus_t.
*
*/
#ifndef DRXJ_DIGITAL_ONLY
static DRXStatus_t
CtrlGetCfgOOBMisc(pDRXDemodInstance_t demod, pDRXJCfgOOBMisc_t misc)
{
	struct i2c_device_addr *devAddr = NULL;
	u16_t lock = 0U;
	u16_t state = 0U;
	u16_t data = 0U;
	u16_t digitalAGCMant = 0U;
	u16_t digitalAGCExp = 0U;

	/* check arguments */
	if (misc == NULL) {
		return (DRX_STS_INVALID_ARG);
	}
	devAddr = demod->myI2CDevAddr;

	/* TODO */
	/* check if the same registers are used for all standards (QAM/VSB/ATV) */
	RR16(devAddr, ORX_NSU_TUN_IFGAIN_W__A, &misc->agc.IFAGC);
	RR16(devAddr, ORX_NSU_TUN_RFGAIN_W__A, &misc->agc.RFAGC);
	RR16(devAddr, ORX_FWP_SRC_DGN_W__A, &data);

	digitalAGCMant = data & ORX_FWP_SRC_DGN_W_MANT__M;
	digitalAGCExp = (data & ORX_FWP_SRC_DGN_W_EXP__M)
	    >> ORX_FWP_SRC_DGN_W_EXP__B;
	misc->agc.DigitalAGC = digitalAGCMant << digitalAGCExp;

	SARR16(devAddr, SCU_RAM_ORX_SCU_LOCK__A, &lock);

	misc->anaGainLock = ((lock & 0x0001) ? TRUE : FALSE);
	misc->digGainLock = ((lock & 0x0002) ? TRUE : FALSE);
	misc->freqLock = ((lock & 0x0004) ? TRUE : FALSE);
	misc->phaseLock = ((lock & 0x0008) ? TRUE : FALSE);
	misc->symTimingLock = ((lock & 0x0010) ? TRUE : FALSE);
	misc->eqLock = ((lock & 0x0020) ? TRUE : FALSE);

	SARR16(devAddr, SCU_RAM_ORX_SCU_STATE__A, &state);
	misc->state = (state >> 8) & 0xff;

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}
#endif

/**
* \fn DRXStatus_t CtrlGetCfgVSBMisc()
* \brief Get current state information of OOB.
* \param pointer to DRXJCfgOOBMisc_t.
* \return DRXStatus_t.
*
*/
static DRXStatus_t
CtrlGetCfgVSBMisc(pDRXDemodInstance_t demod, pDRXJCfgVSBMisc_t misc)
{
	struct i2c_device_addr *devAddr = NULL;

	/* check arguments */
	if (misc == NULL) {
		return (DRX_STS_INVALID_ARG);
	}
	devAddr = demod->myI2CDevAddr;

	CHK_ERROR(GetVSBSymbErr(devAddr, &misc->symbError));

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlSetCfgAgcIf()
* \brief Set IF AGC.
* \param demod demod instance
* \param agcSettings If agc configuration
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlSetCfgAgcIf(pDRXDemodInstance_t demod, pDRXJCfgAgc_t agcSettings)
{
	/* check arguments */
	if (agcSettings == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	switch (agcSettings->ctrlMode) {
	case DRX_AGC_CTRL_AUTO:	/* fallthrough */
	case DRX_AGC_CTRL_USER:	/* fallthrough */
	case DRX_AGC_CTRL_OFF:	/* fallthrough */
		break;
	default:
		return (DRX_STS_INVALID_ARG);
	}

	/* Distpatch */
	switch (agcSettings->standard) {
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
		return SetAgcIf(demod, agcSettings, TRUE);
	case DRX_STANDARD_UNKNOWN:
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetCfgAgcIf()
* \brief Retrieve IF AGC settings.
* \param demod demod instance
* \param agcSettings If agc configuration
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlGetCfgAgcIf(pDRXDemodInstance_t demod, pDRXJCfgAgc_t agcSettings)
{
	/* check arguments */
	if (agcSettings == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	/* Distpatch */
	switch (agcSettings->standard) {
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
		return GetAgcIf(demod, agcSettings);
	case DRX_STANDARD_UNKNOWN:
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlSetCfgAgcRf()
* \brief Set RF AGC.
* \param demod demod instance
* \param agcSettings rf agc configuration
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlSetCfgAgcRf(pDRXDemodInstance_t demod, pDRXJCfgAgc_t agcSettings)
{
	/* check arguments */
	if (agcSettings == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	switch (agcSettings->ctrlMode) {
	case DRX_AGC_CTRL_AUTO:	/* fallthrough */
	case DRX_AGC_CTRL_USER:	/* fallthrough */
	case DRX_AGC_CTRL_OFF:
		break;
	default:
		return (DRX_STS_INVALID_ARG);
	}

	/* Distpatch */
	switch (agcSettings->standard) {
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
		return SetAgcRf(demod, agcSettings, TRUE);
	case DRX_STANDARD_UNKNOWN:
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetCfgAgcRf()
* \brief Retrieve RF AGC settings.
* \param demod demod instance
* \param agcSettings Rf agc configuration
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlGetCfgAgcRf(pDRXDemodInstance_t demod, pDRXJCfgAgc_t agcSettings)
{
	/* check arguments */
	if (agcSettings == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	/* Distpatch */
	switch (agcSettings->standard) {
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
		return GetAgcRf(demod, agcSettings);
	case DRX_STANDARD_UNKNOWN:
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetCfgAgcInternal()
* \brief Retrieve internal AGC value.
* \param demod demod instance
* \param u16_t
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlGetCfgAgcInternal(pDRXDemodInstance_t demod, pu16_t agcInternal)
{
	struct i2c_device_addr *devAddr = NULL;
	DRXLockStatus_t lockStatus = DRX_NOT_LOCKED;
	pDRXJData_t extAttr = NULL;
	u16_t iqmCfScaleSh = 0;
	u16_t iqmCfPower = 0;
	u16_t iqmCfAmp = 0;
	u16_t iqmCfGain = 0;

	/* check arguments */
	if (agcInternal == NULL) {
		return (DRX_STS_INVALID_ARG);
	}
	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	CHK_ERROR(CtrlLockStatus(demod, &lockStatus));
	if (lockStatus != DRXJ_DEMOD_LOCK && lockStatus != DRX_LOCKED) {
		*agcInternal = 0;
		return DRX_STS_OK;
	}

	/* Distpatch */
	switch (extAttr->standard) {
	case DRX_STANDARD_8VSB:
		iqmCfGain = 57;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		switch (extAttr->constellation) {
		case DRX_CONSTELLATION_QAM256:
		case DRX_CONSTELLATION_QAM128:
		case DRX_CONSTELLATION_QAM32:
		case DRX_CONSTELLATION_QAM16:
			iqmCfGain = 57;
			break;
		case DRX_CONSTELLATION_QAM64:
			iqmCfGain = 56;
			break;
		default:
			return (DRX_STS_ERROR);
		}
		break;
#endif
	default:
		return (DRX_STS_INVALID_ARG);
	}

	RR16(devAddr, IQM_CF_POW__A, &iqmCfPower);
	RR16(devAddr, IQM_CF_SCALE_SH__A, &iqmCfScaleSh);
	RR16(devAddr, IQM_CF_AMP__A, &iqmCfAmp);
	/* IQM_CF_PWR_CORRECTION_dB = 3;
	   P5dB =10*log10(IQM_CF_POW)+12-6*9-IQM_CF_PWR_CORRECTION_dB; */
	/* P4dB = P5dB -20*log10(IQM_CF_AMP)-6*10
	   -IQM_CF_Gain_dB-18+6*(27-IQM_CF_SCALE_SH*2-10)
	   +6*7+10*log10(1+0.115/4); */
	/* PadcdB = P4dB +3 -6 +60; dBmV */
	*agcInternal = (u16_t) (Log10Times100(iqmCfPower)
				- 2 * Log10Times100(iqmCfAmp)
				- iqmCfGain - 120 * iqmCfScaleSh + 781);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlSetCfgPreSaw()
* \brief Set Pre-saw reference.
* \param demod demod instance
* \param pu16_t
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlSetCfgPreSaw(pDRXDemodInstance_t demod, pDRXJCfgPreSaw_t preSaw)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* check arguments */
	if ((preSaw == NULL) || (preSaw->reference > IQM_AF_PDREF__M)
	    ) {
		return (DRX_STS_INVALID_ARG);
	}

	/* Only if standard is currently active */
	if ((extAttr->standard == preSaw->standard) ||
	    (DRXJ_ISQAMSTD(extAttr->standard) &&
	     DRXJ_ISQAMSTD(preSaw->standard)) ||
	    (DRXJ_ISATVSTD(extAttr->standard) &&
	     DRXJ_ISATVSTD(preSaw->standard))) {
		WR16(devAddr, IQM_AF_PDREF__A, preSaw->reference);
	}

	/* Store pre-saw settings */
	switch (preSaw->standard) {
	case DRX_STANDARD_8VSB:
		extAttr->vsbPreSawCfg = *preSaw;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		extAttr->qamPreSawCfg = *preSaw;
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
		extAttr->atvPreSawCfg = *preSaw;
		break;
#endif
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlSetCfgAfeGain()
* \brief Set AFE Gain.
* \param demod demod instance
* \param pu16_t
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlSetCfgAfeGain(pDRXDemodInstance_t demod, pDRXJCfgAfeGain_t afeGain)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	u8_t gain = 0;

	/* check arguments */
	if (afeGain == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	switch (afeGain->standard) {
	case DRX_STANDARD_8VSB:	/* fallthrough */
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
#endif
		/* Do nothing */
		break;
	default:
		return (DRX_STS_INVALID_ARG);
	}

	/* TODO PGA gain is also written by microcode (at least by QAM and VSB)
	   So I (PJ) think interface requires choice between auto, user mode */

	if (afeGain->gain >= 329)
		gain = 15;
	else if (afeGain->gain <= 147)
		gain = 0;
	else
		gain = (afeGain->gain - 140 + 6) / 13;

	/* Only if standard is currently active */
	if (extAttr->standard == afeGain->standard)
		WR16(devAddr, IQM_AF_PGA_GAIN__A, gain);

	/* Store AFE Gain settings */
	switch (afeGain->standard) {
	case DRX_STANDARD_8VSB:
		extAttr->vsbPgaCfg = gain * 13 + 140;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		extAttr->qamPgaCfg = gain * 13 + 140;
		break;
#endif
	default:
		return (DRX_STS_ERROR);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetCfgPreSaw()
* \brief Get Pre-saw reference setting.
* \param demod demod instance
* \param pu16_t
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlGetCfgPreSaw(pDRXDemodInstance_t demod, pDRXJCfgPreSaw_t preSaw)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	/* check arguments */
	if (preSaw == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	switch (preSaw->standard) {
	case DRX_STANDARD_8VSB:
		*preSaw = extAttr->vsbPreSawCfg;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		*preSaw = extAttr->qamPreSawCfg;
		break;
#endif
#ifndef DRXJ_DIGITAL_ONLY
	case DRX_STANDARD_PAL_SECAM_BG:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_DK:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_I:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_L:	/* fallthrough */
	case DRX_STANDARD_PAL_SECAM_LP:	/* fallthrough */
	case DRX_STANDARD_NTSC:
		extAttr->atvPreSawCfg.standard = DRX_STANDARD_NTSC;
		*preSaw = extAttr->atvPreSawCfg;
		break;
	case DRX_STANDARD_FM:
		extAttr->atvPreSawCfg.standard = DRX_STANDARD_FM;
		*preSaw = extAttr->atvPreSawCfg;
		break;
#endif
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetCfgAfeGain()
* \brief Get AFE Gain.
* \param demod demod instance
* \param pu16_t
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlGetCfgAfeGain(pDRXDemodInstance_t demod, pDRXJCfgAfeGain_t afeGain)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;

	/* check arguments */
	if (afeGain == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	switch (afeGain->standard) {
	case DRX_STANDARD_8VSB:
		afeGain->gain = extAttr->vsbPgaCfg;
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:	/* fallthrough */
	case DRX_STANDARD_ITU_B:	/* fallthrough */
	case DRX_STANDARD_ITU_C:
		afeGain->gain = extAttr->qamPgaCfg;
		break;
#endif
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetFecMeasSeqCount()
* \brief Get FEC measurement sequnce number.
* \param demod demod instance
* \param pu16_t
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlGetFecMeasSeqCount(pDRXDemodInstance_t demod, pu16_t fecMeasSeqCount)
{
	/* check arguments */
	if (fecMeasSeqCount == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	RR16(demod->myI2CDevAddr, SCU_RAM_FEC_MEAS_COUNT__A, fecMeasSeqCount);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetAccumCrRSCwErr()
* \brief Get accumulative corrected RS codeword number.
* \param demod demod instance
* \param pu32_t
* \return DRXStatus_t.
*
* Check arguments
* Dispatch handling to standard specific function.
*
*/
static DRXStatus_t
CtrlGetAccumCrRSCwErr(pDRXDemodInstance_t demod, pu32_t accumCrRsCWErr)
{
	if (accumCrRsCWErr == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	RR32(demod->myI2CDevAddr, SCU_RAM_FEC_ACCUM_CW_CORRECTED_LO__A,
	     accumCrRsCWErr);

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/**
* \fn DRXStatus_t CtrlSetCfg()
* \brief Set 'some' configuration of the device.
* \param devmod Pointer to demodulator instance.
* \param config Pointer to configuration parameters (type and data).
* \return DRXStatus_t.

*/
static DRXStatus_t CtrlSetCfg(pDRXDemodInstance_t demod, pDRXCfg_t config)
{
	if (config == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	DUMMY_READ();
	switch (config->cfgType) {
	case DRX_CFG_MPEG_OUTPUT:
		return CtrlSetCfgMPEGOutput(demod,
					    (pDRXCfgMPEGOutput_t) config->
					    cfgData);
	case DRX_CFG_PINS_SAFE_MODE:
		return CtrlSetCfgPdrSafeMode(demod, (pBool_t) config->cfgData);
	case DRXJ_CFG_AGC_RF:
		return CtrlSetCfgAgcRf(demod, (pDRXJCfgAgc_t) config->cfgData);
	case DRXJ_CFG_AGC_IF:
		return CtrlSetCfgAgcIf(demod, (pDRXJCfgAgc_t) config->cfgData);
	case DRXJ_CFG_PRE_SAW:
		return CtrlSetCfgPreSaw(demod,
					(pDRXJCfgPreSaw_t) config->cfgData);
	case DRXJ_CFG_AFE_GAIN:
		return CtrlSetCfgAfeGain(demod,
					 (pDRXJCfgAfeGain_t) config->cfgData);
	case DRXJ_CFG_SMART_ANT:
		return CtrlSetCfgSmartAnt(demod,
					  (pDRXJCfgSmartAnt_t) (config->
								cfgData));
	case DRXJ_CFG_RESET_PACKET_ERR:
		return CtrlSetCfgResetPktErr(demod);
#ifndef DRXJ_DIGITAL_ONLY
	case DRXJ_CFG_OOB_PRE_SAW:
		return CtrlSetCfgOOBPreSAW(demod, (pu16_t) (config->cfgData));
	case DRXJ_CFG_OOB_LO_POW:
		return CtrlSetCfgOOBLoPower(demod,
					    (pDRXJCfgOobLoPower_t) (config->
								    cfgData));
	case DRXJ_CFG_ATV_MISC:
		return CtrlSetCfgAtvMisc(demod,
					 (pDRXJCfgAtvMisc_t) config->cfgData);
	case DRXJ_CFG_ATV_EQU_COEF:
		return CtrlSetCfgAtvEquCoef(demod,
					    (pDRXJCfgAtvEquCoef_t) config->
					    cfgData);
	case DRXJ_CFG_ATV_OUTPUT:
		return CtrlSetCfgATVOutput(demod,
					   (pDRXJCfgAtvOutput_t) config->
					   cfgData);
#endif
	case DRXJ_CFG_MPEG_OUTPUT_MISC:
		return CtrlSetCfgMpegOutputMisc(demod,
						(pDRXJCfgMpegOutputMisc_t)
						config->cfgData);
#ifndef DRXJ_EXCLUDE_AUDIO
	case DRX_CFG_AUD_VOLUME:
		return AUDCtrlSetCfgVolume(demod,
					   (pDRXCfgAudVolume_t) config->
					   cfgData);
	case DRX_CFG_I2S_OUTPUT:
		return AUDCtrlSetCfgOutputI2S(demod,
					      (pDRXCfgI2SOutput_t) config->
					      cfgData);
	case DRX_CFG_AUD_AUTOSOUND:
		return AUDCtrSetlCfgAutoSound(demod, (pDRXCfgAudAutoSound_t)
					      config->cfgData);
	case DRX_CFG_AUD_ASS_THRES:
		return AUDCtrlSetCfgASSThres(demod, (pDRXCfgAudASSThres_t)
					     config->cfgData);
	case DRX_CFG_AUD_CARRIER:
		return AUDCtrlSetCfgCarrier(demod,
					    (pDRXCfgAudCarriers_t) config->
					    cfgData);
	case DRX_CFG_AUD_DEVIATION:
		return AUDCtrlSetCfgDev(demod,
					(pDRXCfgAudDeviation_t) config->
					cfgData);
	case DRX_CFG_AUD_PRESCALE:
		return AUDCtrlSetCfgPrescale(demod,
					     (pDRXCfgAudPrescale_t) config->
					     cfgData);
	case DRX_CFG_AUD_MIXER:
		return AUDCtrlSetCfgMixer(demod,
					  (pDRXCfgAudMixer_t) config->cfgData);
	case DRX_CFG_AUD_AVSYNC:
		return AUDCtrlSetCfgAVSync(demod,
					   (pDRXCfgAudAVSync_t) config->
					   cfgData);

#endif
	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/

/**
* \fn DRXStatus_t CtrlGetCfg()
* \brief Get 'some' configuration of the device.
* \param devmod Pointer to demodulator instance.
* \param config Pointer to configuration parameters (type and data).
* \return DRXStatus_t.
*/

static DRXStatus_t CtrlGetCfg(pDRXDemodInstance_t demod, pDRXCfg_t config)
{
	if (config == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	DUMMY_READ();

	switch (config->cfgType) {
	case DRX_CFG_MPEG_OUTPUT:
		return CtrlGetCfgMPEGOutput(demod,
					    (pDRXCfgMPEGOutput_t) config->
					    cfgData);
	case DRX_CFG_PINS_SAFE_MODE:
		return CtrlGetCfgPdrSafeMode(demod, (pBool_t) config->cfgData);
	case DRXJ_CFG_AGC_RF:
		return CtrlGetCfgAgcRf(demod, (pDRXJCfgAgc_t) config->cfgData);
	case DRXJ_CFG_AGC_IF:
		return CtrlGetCfgAgcIf(demod, (pDRXJCfgAgc_t) config->cfgData);
	case DRXJ_CFG_AGC_INTERNAL:
		return CtrlGetCfgAgcInternal(demod, (pu16_t) config->cfgData);
	case DRXJ_CFG_PRE_SAW:
		return CtrlGetCfgPreSaw(demod,
					(pDRXJCfgPreSaw_t) config->cfgData);
	case DRXJ_CFG_AFE_GAIN:
		return CtrlGetCfgAfeGain(demod,
					 (pDRXJCfgAfeGain_t) config->cfgData);
	case DRXJ_CFG_ACCUM_CR_RS_CW_ERR:
		return CtrlGetAccumCrRSCwErr(demod, (pu32_t) config->cfgData);
	case DRXJ_CFG_FEC_MERS_SEQ_COUNT:
		return CtrlGetFecMeasSeqCount(demod, (pu16_t) config->cfgData);
	case DRXJ_CFG_VSB_MISC:
		return CtrlGetCfgVSBMisc(demod,
					 (pDRXJCfgVSBMisc_t) config->cfgData);
	case DRXJ_CFG_SYMBOL_CLK_OFFSET:
		return CtrlGetCfgSymbolClockOffset(demod,
						   (ps32_t) config->cfgData);
#ifndef DRXJ_DIGITAL_ONLY
	case DRXJ_CFG_OOB_MISC:
		return CtrlGetCfgOOBMisc(demod,
					 (pDRXJCfgOOBMisc_t) config->cfgData);
	case DRXJ_CFG_OOB_PRE_SAW:
		return CtrlGetCfgOOBPreSAW(demod, (pu16_t) (config->cfgData));
	case DRXJ_CFG_OOB_LO_POW:
		return CtrlGetCfgOOBLoPower(demod,
					    (pDRXJCfgOobLoPower_t) (config->
								    cfgData));
	case DRXJ_CFG_ATV_EQU_COEF:
		return CtrlGetCfgAtvEquCoef(demod,
					    (pDRXJCfgAtvEquCoef_t) config->
					    cfgData);
	case DRXJ_CFG_ATV_MISC:
		return CtrlGetCfgAtvMisc(demod,
					 (pDRXJCfgAtvMisc_t) config->cfgData);
	case DRXJ_CFG_ATV_OUTPUT:
		return CtrlGetCfgAtvOutput(demod,
					   (pDRXJCfgAtvOutput_t) config->
					   cfgData);
	case DRXJ_CFG_ATV_AGC_STATUS:
		return CtrlGetCfgAtvAgcStatus(demod,
					      (pDRXJCfgAtvAgcStatus_t) config->
					      cfgData);
#endif
	case DRXJ_CFG_MPEG_OUTPUT_MISC:
		return CtrlGetCfgMpegOutputMisc(demod,
						(pDRXJCfgMpegOutputMisc_t)
						config->cfgData);
	case DRXJ_CFG_HW_CFG:
		return CtrlGetCfgHwCfg(demod,
				       (pDRXJCfgHwCfg_t) config->cfgData);
#ifndef DRXJ_EXCLUDE_AUDIO
	case DRX_CFG_AUD_VOLUME:
		return AUDCtrlGetCfgVolume(demod,
					   (pDRXCfgAudVolume_t) config->
					   cfgData);
	case DRX_CFG_I2S_OUTPUT:
		return AUDCtrlGetCfgOutputI2S(demod,
					      (pDRXCfgI2SOutput_t) config->
					      cfgData);

	case DRX_CFG_AUD_RDS:
		return AUDCtrlGetCfgRDS(demod,
					(pDRXCfgAudRDS_t) config->cfgData);
	case DRX_CFG_AUD_AUTOSOUND:
		return AUDCtrlGetCfgAutoSound(demod,
					      (pDRXCfgAudAutoSound_t) config->
					      cfgData);
	case DRX_CFG_AUD_ASS_THRES:
		return AUDCtrlGetCfgASSThres(demod,
					     (pDRXCfgAudASSThres_t) config->
					     cfgData);
	case DRX_CFG_AUD_CARRIER:
		return AUDCtrlGetCfgCarrier(demod,
					    (pDRXCfgAudCarriers_t) config->
					    cfgData);
	case DRX_CFG_AUD_DEVIATION:
		return AUDCtrlGetCfgDev(demod,
					(pDRXCfgAudDeviation_t) config->
					cfgData);
	case DRX_CFG_AUD_PRESCALE:
		return AUDCtrlGetCfgPrescale(demod,
					     (pDRXCfgAudPrescale_t) config->
					     cfgData);
	case DRX_CFG_AUD_MIXER:
		return AUDCtrlGetCfgMixer(demod,
					  (pDRXCfgAudMixer_t) config->cfgData);
	case DRX_CFG_AUD_AVSYNC:
		return AUDCtrlGetCfgAVSync(demod,
					   (pDRXCfgAudAVSync_t) config->
					   cfgData);
#endif

	default:
		return (DRX_STS_INVALID_ARG);
	}

	return (DRX_STS_OK);
rw_error:
	return (DRX_STS_ERROR);
}

/*=============================================================================
===== EXPORTED FUNCTIONS ====================================================*/
/**
* \fn DRXJ_Open()
* \brief Open the demod instance, configure device, configure drxdriver
* \return Status_t Return status.
*
* DRXJ_Open() can be called with a NULL ucode image => no ucode upload.
* This means that DRXJ_Open() must NOT contain SCU commands or, in general,
* rely on SCU or AUD ucode to be present.
*
*/
DRXStatus_t DRXJ_Open(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	pDRXCommonAttr_t commonAttr = NULL;
	u32_t driverVersion = 0;
	DRXUCodeInfo_t ucodeInfo;
	DRXCfgMPEGOutput_t cfgMPEGOutput;

	/* Check arguments */
	if (demod->myExtAttr == NULL) {
		return (DRX_STS_INVALID_ARG);
	}

	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;
	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;

	CHK_ERROR(PowerUpDevice(demod));
	commonAttr->currentPowerMode = DRX_POWER_UP;

	/* has to be in front of setIqmAf and setOrxNsuAox */
	CHK_ERROR(GetDeviceCapabilities(demod));

	/* Soft reset of sys- and osc-clockdomain */
	WR16(devAddr, SIO_CC_SOFT_RST__A, (SIO_CC_SOFT_RST_SYS__M |
					   SIO_CC_SOFT_RST_OSC__M));
	WR16(devAddr, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY);
	CHK_ERROR(DRXBSP_HST_Sleep(1));

	/* TODO first make sure that everything keeps working before enabling this */
	/* PowerDownAnalogBlocks() */
	WR16(devAddr, ATV_TOP_STDBY__A, (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE)
	     | ATV_TOP_STDBY_SIF_STDBY_STANDBY);

	CHK_ERROR(SetIqmAf(demod, FALSE));
	CHK_ERROR(SetOrxNsuAox(demod, FALSE));

	CHK_ERROR(InitHI(demod));

	/* disable mpegoutput pins */
	cfgMPEGOutput.enableMPEGOutput = FALSE;
	CHK_ERROR(CtrlSetCfgMPEGOutput(demod, &cfgMPEGOutput));
	/* Stop AUD Inform SetAudio it will need to do all setting */
	CHK_ERROR(PowerDownAud(demod));
	/* Stop SCU */
	WR16(devAddr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_STOP);

	/* Upload microcode */
	if (commonAttr->microcode != NULL) {
		/* Dirty trick to use common ucode upload & verify,
		   pretend device is already open */
		commonAttr->isOpened = TRUE;
		ucodeInfo.mcData = commonAttr->microcode;
		ucodeInfo.mcSize = commonAttr->microcodeSize;

#ifdef DRXJ_SPLIT_UCODE_UPLOAD
		/* Upload microcode without audio part */
		CHK_ERROR(CtrlUCodeUpload
			  (demod, &ucodeInfo, UCODE_UPLOAD, FALSE));
#else
		CHK_ERROR(DRX_Ctrl(demod, DRX_CTRL_LOAD_UCODE, &ucodeInfo));
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */
		if (commonAttr->verifyMicrocode == TRUE) {
#ifdef DRXJ_SPLIT_UCODE_UPLOAD
			CHK_ERROR(CtrlUCodeUpload
				  (demod, &ucodeInfo, UCODE_VERIFY, FALSE));
#else
			CHK_ERROR(DRX_Ctrl
				  (demod, DRX_CTRL_VERIFY_UCODE, &ucodeInfo));
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */
		}
		commonAttr->isOpened = FALSE;
	}

	/* Run SCU for a little while to initialize microcode version numbers */
	WR16(devAddr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE);

	/* Open tuner instance */
	if (demod->myTuner != NULL) {
		demod->myTuner->myCommonAttr->myUserData = (void *)demod;

		if (commonAttr->tunerPortNr == 1) {
			Bool_t bridgeClosed = TRUE;
			CHK_ERROR(CtrlI2CBridge(demod, &bridgeClosed));
		}

		CHK_ERROR(DRXBSP_TUNER_Open(demod->myTuner));

		if (commonAttr->tunerPortNr == 1) {
			Bool_t bridgeClosed = FALSE;
			CHK_ERROR(CtrlI2CBridge(demod, &bridgeClosed));
		}
		commonAttr->tunerMinFreqRF =
		    ((demod->myTuner)->myCommonAttr->minFreqRF);
		commonAttr->tunerMaxFreqRF =
		    ((demod->myTuner)->myCommonAttr->maxFreqRF);
	}

	/* Initialize scan timeout */
	commonAttr->scanDemodLockTimeout = DRXJ_SCAN_TIMEOUT;
	commonAttr->scanDesiredLock = DRX_LOCKED;

	/* Initialize default AFE configuartion for QAM */
	if (extAttr->hasLNA) {
		/* IF AGC off, PGA active */
#ifndef DRXJ_VSB_ONLY
		extAttr->qamIfAgcCfg.standard = DRX_STANDARD_ITU_B;
		extAttr->qamIfAgcCfg.ctrlMode = DRX_AGC_CTRL_OFF;
		extAttr->qamPgaCfg = 140 + (11 * 13);
#endif
		extAttr->vsbIfAgcCfg.standard = DRX_STANDARD_8VSB;
		extAttr->vsbIfAgcCfg.ctrlMode = DRX_AGC_CTRL_OFF;
		extAttr->vsbPgaCfg = 140 + (11 * 13);
	} else {
		/* IF AGC on, PGA not active */
#ifndef DRXJ_VSB_ONLY
		extAttr->qamIfAgcCfg.standard = DRX_STANDARD_ITU_B;
		extAttr->qamIfAgcCfg.ctrlMode = DRX_AGC_CTRL_AUTO;
		extAttr->qamIfAgcCfg.minOutputLevel = 0;
		extAttr->qamIfAgcCfg.maxOutputLevel = 0x7FFF;
		extAttr->qamIfAgcCfg.speed = 3;
		extAttr->qamIfAgcCfg.top = 1297;
		extAttr->qamPgaCfg = 140;
#endif
		extAttr->vsbIfAgcCfg.standard = DRX_STANDARD_8VSB;
		extAttr->vsbIfAgcCfg.ctrlMode = DRX_AGC_CTRL_AUTO;
		extAttr->vsbIfAgcCfg.minOutputLevel = 0;
		extAttr->vsbIfAgcCfg.maxOutputLevel = 0x7FFF;
		extAttr->vsbIfAgcCfg.speed = 3;
		extAttr->vsbIfAgcCfg.top = 1024;
		extAttr->vsbPgaCfg = 140;
	}
	/* TODO: remove minOutputLevel and maxOutputLevel for both QAM and VSB after */
	/* mc has not used them */
#ifndef DRXJ_VSB_ONLY
	extAttr->qamRfAgcCfg.standard = DRX_STANDARD_ITU_B;
	extAttr->qamRfAgcCfg.ctrlMode = DRX_AGC_CTRL_AUTO;
	extAttr->qamRfAgcCfg.minOutputLevel = 0;
	extAttr->qamRfAgcCfg.maxOutputLevel = 0x7FFF;
	extAttr->qamRfAgcCfg.speed = 3;
	extAttr->qamRfAgcCfg.top = 9500;
	extAttr->qamRfAgcCfg.cutOffCurrent = 4000;
	extAttr->qamPreSawCfg.standard = DRX_STANDARD_ITU_B;
	extAttr->qamPreSawCfg.reference = 0x07;
	extAttr->qamPreSawCfg.usePreSaw = TRUE;
#endif
	/* Initialize default AFE configuartion for VSB */
	extAttr->vsbRfAgcCfg.standard = DRX_STANDARD_8VSB;
	extAttr->vsbRfAgcCfg.ctrlMode = DRX_AGC_CTRL_AUTO;
	extAttr->vsbRfAgcCfg.minOutputLevel = 0;
	extAttr->vsbRfAgcCfg.maxOutputLevel = 0x7FFF;
	extAttr->vsbRfAgcCfg.speed = 3;
	extAttr->vsbRfAgcCfg.top = 9500;
	extAttr->vsbRfAgcCfg.cutOffCurrent = 4000;
	extAttr->vsbPreSawCfg.standard = DRX_STANDARD_8VSB;
	extAttr->vsbPreSawCfg.reference = 0x07;
	extAttr->vsbPreSawCfg.usePreSaw = TRUE;

#ifndef DRXJ_DIGITAL_ONLY
	/* Initialize default AFE configuartion for ATV */
	extAttr->atvRfAgcCfg.standard = DRX_STANDARD_NTSC;
	extAttr->atvRfAgcCfg.ctrlMode = DRX_AGC_CTRL_AUTO;
	extAttr->atvRfAgcCfg.top = 9500;
	extAttr->atvRfAgcCfg.cutOffCurrent = 4000;
	extAttr->atvRfAgcCfg.speed = 3;
	extAttr->atvIfAgcCfg.standard = DRX_STANDARD_NTSC;
	extAttr->atvIfAgcCfg.ctrlMode = DRX_AGC_CTRL_AUTO;
	extAttr->atvIfAgcCfg.speed = 3;
	extAttr->atvIfAgcCfg.top = 2400;
	extAttr->atvPreSawCfg.reference = 0x0007;
	extAttr->atvPreSawCfg.usePreSaw = TRUE;
	extAttr->atvPreSawCfg.standard = DRX_STANDARD_NTSC;
#endif
	extAttr->standard = DRX_STANDARD_UNKNOWN;

	CHK_ERROR(SmartAntInit(demod));

	/* Stamp driver version number in SCU data RAM in BCD code
	   Done to enable field application engineers to retreive drxdriver version
	   via I2C from SCU RAM
	 */
	driverVersion = (VERSION_MAJOR / 100) % 10;
	driverVersion <<= 4;
	driverVersion += (VERSION_MAJOR / 10) % 10;
	driverVersion <<= 4;
	driverVersion += (VERSION_MAJOR % 10);
	driverVersion <<= 4;
	driverVersion += (VERSION_MINOR % 10);
	driverVersion <<= 4;
	driverVersion += (VERSION_PATCH / 1000) % 10;
	driverVersion <<= 4;
	driverVersion += (VERSION_PATCH / 100) % 10;
	driverVersion <<= 4;
	driverVersion += (VERSION_PATCH / 10) % 10;
	driverVersion <<= 4;
	driverVersion += (VERSION_PATCH % 10);
	WR16(devAddr, SCU_RAM_DRIVER_VER_HI__A, (u16_t) (driverVersion >> 16));
	WR16(devAddr, SCU_RAM_DRIVER_VER_LO__A,
	     (u16_t) (driverVersion & 0xFFFF));

	/* refresh the audio data structure with default */
	extAttr->audData = DRXJDefaultAudData_g;

	return (DRX_STS_OK);
rw_error:
	commonAttr->isOpened = FALSE;
	return (DRX_STS_ERROR);
}

/*============================================================================*/
/**
* \fn DRXJ_Close()
* \brief Close the demod instance, power down the device
* \return Status_t Return status.
*
*/
DRXStatus_t DRXJ_Close(pDRXDemodInstance_t demod)
{
	struct i2c_device_addr *devAddr = NULL;
	pDRXJData_t extAttr = NULL;
	pDRXCommonAttr_t commonAttr = NULL;
	DRXPowerMode_t powerMode = DRX_POWER_UP;

	commonAttr = (pDRXCommonAttr_t) demod->myCommonAttr;
	devAddr = demod->myI2CDevAddr;
	extAttr = (pDRXJData_t) demod->myExtAttr;

	/* power up */
	CHK_ERROR(CtrlPowerMode(demod, &powerMode));

	if (demod->myTuner != NULL) {
		/* Check if bridge is used */
		if (commonAttr->tunerPortNr == 1) {
			Bool_t bridgeClosed = TRUE;
			CHK_ERROR(CtrlI2CBridge(demod, &bridgeClosed));
		}
		CHK_ERROR(DRXBSP_TUNER_Close(demod->myTuner));
		if (commonAttr->tunerPortNr == 1) {
			Bool_t bridgeClosed = FALSE;
			CHK_ERROR(CtrlI2CBridge(demod, &bridgeClosed));
		}
	};

	WR16(devAddr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE);
	powerMode = DRX_POWER_DOWN;
	CHK_ERROR(CtrlPowerMode(demod, &powerMode));

	return DRX_STS_OK;
rw_error:
	return (DRX_STS_ERROR);
}

/*============================================================================*/
/**
* \fn DRXJ_Ctrl()
* \brief DRXJ specific control function
* \return Status_t Return status.
*/
DRXStatus_t
DRXJ_Ctrl(pDRXDemodInstance_t demod, DRXCtrlIndex_t ctrl, void *ctrlData)
{
	switch (ctrl) {
      /*======================================================================*/
	case DRX_CTRL_SET_CHANNEL:
		{
			return CtrlSetChannel(demod, (pDRXChannel_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_CHANNEL:
		{
			return CtrlGetChannel(demod, (pDRXChannel_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SIG_QUALITY:
		{
			return CtrlSigQuality(demod,
					      (pDRXSigQuality_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SIG_STRENGTH:
		{
			return CtrlSigStrength(demod, (pu16_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_CONSTEL:
		{
			return CtrlConstel(demod, (pDRXComplex_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SET_CFG:
		{
			return CtrlSetCfg(demod, (pDRXCfg_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_CFG:
		{
			return CtrlGetCfg(demod, (pDRXCfg_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_I2C_BRIDGE:
		{
			return CtrlI2CBridge(demod, (pBool_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_LOCK_STATUS:
		{
			return CtrlLockStatus(demod,
					      (pDRXLockStatus_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SET_STANDARD:
		{
			return CtrlSetStandard(demod,
					       (pDRXStandard_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_STANDARD:
		{
			return CtrlGetStandard(demod,
					       (pDRXStandard_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_POWER_MODE:
		{
			return CtrlPowerMode(demod, (pDRXPowerMode_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_VERSION:
		{
			return CtrlVersion(demod,
					   (pDRXVersionList_t *) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_PROBE_DEVICE:
		{
			return CtrlProbeDevice(demod);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SET_OOB:
		{
			return CtrlSetOOB(demod, (pDRXOOB_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_OOB:
		{
			return CtrlGetOOB(demod, (pDRXOOBStatus_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_SET_UIO_CFG:
		{
			return CtrlSetUIOCfg(demod, (pDRXUIOCfg_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_GET_UIO_CFG:
		{
			return CtrlGetUIOCfg(demod, (pDRXUIOCfg_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_UIO_READ:
		{
			return CtrlUIORead(demod, (pDRXUIOData_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_UIO_WRITE:
		{
			return CtrlUIOWrite(demod, (pDRXUIOData_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_AUD_SET_STANDARD:
		{
			return AUDCtrlSetStandard(demod,
						  (pDRXAudStandard_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_AUD_GET_STANDARD:
		{
			return AUDCtrlGetStandard(demod,
						  (pDRXAudStandard_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_AUD_GET_STATUS:
		{
			return AUDCtrlGetStatus(demod,
						(pDRXAudStatus_t) ctrlData);
		}
		break;
      /*======================================================================*/
	case DRX_CTRL_AUD_BEEP:
		{
			return AUDCtrlBeep(demod, (pDRXAudBeep_t) ctrlData);
		}
		break;

      /*======================================================================*/
	case DRX_CTRL_I2C_READWRITE:
		{
			return CtrlI2CWriteRead(demod,
						(pDRXI2CData_t) ctrlData);
		}
		break;
#ifdef DRXJ_SPLIT_UCODE_UPLOAD
	case DRX_CTRL_LOAD_UCODE:
		{
			return CtrlUCodeUpload(demod,
					       (pDRXUCodeInfo_t) ctrlData,
					       UCODE_UPLOAD, FALSE);
		}
		break;
	case DRX_CTRL_VERIFY_UCODE:
		{
			return CtrlUCodeUpload(demod,
					       (pDRXUCodeInfo_t) ctrlData,
					       UCODE_VERIFY, FALSE);
		}
		break;
#endif /* DRXJ_SPLIT_UCODE_UPLOAD */
	case DRX_CTRL_VALIDATE_UCODE:
		{
			return CtrlValidateUCode(demod);
		}
		break;
	default:
		return (DRX_STS_FUNC_NOT_AVAILABLE);
	}
	return (DRX_STS_OK);
}

/* END OF FILE */
