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

  The Linux DVB Driver for Micronas DRX39xx family (drx3933j) was
  written by Devin Heitmueller <devin.heitmueller@kernellabs.com>

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
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*-----------------------------------------------------------------------------
INCLUDE FILES
----------------------------------------------------------------------------*/

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/div64.h>

#include <media/dvb_frontend.h>
#include "drx39xxj.h"

#include "drxj.h"
#include "drxj_map.h"

/*============================================================================*/
/*=== DEFINES ================================================================*/
/*============================================================================*/

#define DRX39XX_MAIN_FIRMWARE "dvb-fe-drxj-mc-1.0.8.fw"

/*
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
/*** START DJCOMBO patches to DRXJ registermap constants *********************/
/*** registermap 200706071303 from drxj **************************************/
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
/*** END DJCOMBO patches to DRXJ registermap *********************************/

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

/*
* \def DRXJ_DEF_I2C_ADDR
* \brief Default I2C address of a demodulator instance.
*/
#define DRXJ_DEF_I2C_ADDR (0x52)

/*
* \def DRXJ_DEF_DEMOD_DEV_ID
* \brief Default device identifier of a demodultor instance.
*/
#define DRXJ_DEF_DEMOD_DEV_ID      (1)

/*
* \def DRXJ_SCAN_TIMEOUT
* \brief Timeout value for waiting on demod lock during channel scan (millisec).
*/
#define DRXJ_SCAN_TIMEOUT    1000

/*
* \def HI_I2C_DELAY
* \brief HI timing delay for I2C timing (in nano seconds)
*
*  Used to compute HI_CFG_DIV
*/
#define HI_I2C_DELAY    42

/*
* \def HI_I2C_BRIDGE_DELAY
* \brief HI timing delay for I2C timing (in nano seconds)
*
*  Used to compute HI_CFG_BDL
*/
#define HI_I2C_BRIDGE_DELAY   750

/*
* \brief Time Window for MER and SER Measurement in Units of Segment duration.
*/
#define VSB_TOP_MEASUREMENT_PERIOD  64
#define SYMBOLS_PER_SEGMENT         832

/*
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
/*
* \brief Min supported symbolrates.
*/
#ifndef DRXJ_QAM_SYMBOLRATE_MIN
#define DRXJ_QAM_SYMBOLRATE_MIN          (520000)
#endif

/*
* \brief Max supported symbolrates.
*/
#ifndef DRXJ_QAM_SYMBOLRATE_MAX
#define DRXJ_QAM_SYMBOLRATE_MAX         (7233000)
#endif

/*
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

/*
* \def SCU status and results
* \brief SCU
*/
#define DRX_SCU_READY               0
#define DRXJ_MAX_WAITTIME           100	/* ms */
#define FEC_RS_MEASUREMENT_PERIOD   12894	/* 1 sec */
#define FEC_RS_MEASUREMENT_PRESCALE 1	/* n sec */

/*
* \def DRX_AUD_MAX_DEVIATION
* \brief Needed for calculation of prescale feature in AUD
*/
#ifndef DRXJ_AUD_MAX_FM_DEVIATION
#define DRXJ_AUD_MAX_FM_DEVIATION  100	/* kHz */
#endif

/*
* \brief Needed for calculation of NICAM prescale feature in AUD
*/
#ifndef DRXJ_AUD_MAX_NICAM_PRESCALE
#define DRXJ_AUD_MAX_NICAM_PRESCALE  (9)	/* dB */
#endif

/*
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

/* Magic word for checking correct Endianness of microcode data */
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

/* Pin safe mode macro */
#define DRXJ_PIN_SAFE_MODE 0x0000
/*============================================================================*/
/*=== GLOBAL VARIABLEs =======================================================*/
/*============================================================================*/
/*
*/

/*
* \brief Temporary register definitions.
*        (register definitions that are not yet available in register master)
*/

/*****************************************************************************/
/* Audio block 0x103 is write only. To avoid shadowing in driver accessing   */
/* RAM addresses directly. This must be READ ONLY to avoid problems.         */
/* Writing to the interface addresses are more than only writing the RAM     */
/* locations                                                                 */
/*****************************************************************************/
/*
* \brief RAM location of MODUS registers
*/
#define AUD_DEM_RAM_MODUS_HI__A              0x10204A3
#define AUD_DEM_RAM_MODUS_HI__M              0xF000

#define AUD_DEM_RAM_MODUS_LO__A              0x10204A4
#define AUD_DEM_RAM_MODUS_LO__M              0x0FFF

/*
* \brief RAM location of I2S config registers
*/
#define AUD_DEM_RAM_I2S_CONFIG1__A           0x10204B1
#define AUD_DEM_RAM_I2S_CONFIG2__A           0x10204B2

/*
* \brief RAM location of DCO config registers
*/
#define AUD_DEM_RAM_DCO_B_HI__A              0x1020461
#define AUD_DEM_RAM_DCO_B_LO__A              0x1020462
#define AUD_DEM_RAM_DCO_A_HI__A              0x1020463
#define AUD_DEM_RAM_DCO_A_LO__A              0x1020464

/*
* \brief RAM location of Threshold registers
*/
#define AUD_DEM_RAM_NICAM_THRSHLD__A         0x102045A
#define AUD_DEM_RAM_A2_THRSHLD__A            0x10204BB
#define AUD_DEM_RAM_BTSC_THRSHLD__A          0x10204A6

/*
* \brief RAM location of Carrier Threshold registers
*/
#define AUD_DEM_RAM_CM_A_THRSHLD__A          0x10204AF
#define AUD_DEM_RAM_CM_B_THRSHLD__A          0x10204B0

/*
* \brief FM Matrix register fix
*/
#ifdef AUD_DEM_WR_FM_MATRIX__A
#undef  AUD_DEM_WR_FM_MATRIX__A
#endif
#define AUD_DEM_WR_FM_MATRIX__A              0x105006F

/*============================================================================*/
/*
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

/*
* \brief Needed for calculation of prescale feature in AUD
*/
#ifndef DRX_AUD_MAX_FM_DEVIATION
#define DRX_AUD_MAX_FM_DEVIATION  (100)	/* kHz */
#endif

/*
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

/*
* This macro is used to create byte arrays for block writes.
* Block writes speed up I2C traffic between host and demod.
* The macro takes care of the required byte order in a 16 bits word.
* x -> lowbyte(x), highbyte(x)
*/
#define DRXJ_16TO8(x) ((u8) (((u16)x) & 0xFF)), \
		       ((u8)((((u16)x)>>8)&0xFF))
/*
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

/*
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

static int drxdap_fasi_read_block(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u16 datasize,
				      u8 *data, u32 flags);


static int drxj_dap_read_modify_write_reg16(struct i2c_device_addr *dev_addr,
						 u32 waddr,
						 u32 raddr,
						 u16 wdata, u16 *rdata);

static int drxj_dap_read_reg16(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u16 *data, u32 flags);

static int drxdap_fasi_read_reg32(struct i2c_device_addr *dev_addr,
				      u32 addr,
				      u32 *data, u32 flags);

static int drxdap_fasi_write_block(struct i2c_device_addr *dev_addr,
				       u32 addr,
				       u16 datasize,
				       u8 *data, u32 flags);

static int drxj_dap_write_reg16(struct i2c_device_addr *dev_addr,
				       u32 addr,
				       u16 data, u32 flags);

static int drxdap_fasi_write_reg32(struct i2c_device_addr *dev_addr,
				       u32 addr,
				       u32 data, u32 flags);

static struct drxj_data drxj_data_g = {
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
	/* UIO configuration */
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

	/* ATV configuration */
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
	true,			/* flag CVBS output enable       */
	false,			/* flag SIF output enable        */
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

/*
* \var drxj_default_addr_g
* \brief Default I2C address and device identifier.
*/
static struct i2c_device_addr drxj_default_addr_g = {
	DRXJ_DEF_I2C_ADDR,	/* i2c address */
	DRXJ_DEF_DEMOD_DEV_ID	/* device id */
};

/*
* \var drxj_default_comm_attr_g
* \brief Default common attributes of a drxj demodulator instance.
*/
static struct drx_common_attr drxj_default_comm_attr_g = {
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
	 true,			/* If true, enable MPEG output   */
	 false,			/* If true, insert RS byte       */
	 false,			/* If true, parallel out otherwise serial */
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
	/* Initilisations below can be omitted, they require no user input and
	   are initially 0, NULL or false. The compiler will initialize them to these
	   values when omitted.  */
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
	1,			/* nr of I2C port to which tuner is    */
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

/*
* \var drxj_default_demod_g
* \brief Default drxj demodulator instance.
*/
static struct drx_demod_instance drxj_default_demod_g = {
	&drxj_default_addr_g,	/* i2c address & device id */
	&drxj_default_comm_attr_g,	/* demod common attributes */
	&drxj_data_g		/* demod device specific attributes */
};

/*
* \brief Default audio data structure for DRK demodulator instance.
*
* This structure is DRXK specific.
*
*/
static struct drx_aud_data drxj_default_aud_data_g = {
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

/*
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

static int
ctrl_set_cfg_pre_saw(struct drx_demod_instance *demod, struct drxj_cfg_pre_saw *pre_saw);

static int
ctrl_set_cfg_afe_gain(struct drx_demod_instance *demod, struct drxj_cfg_afe_gain *afe_gain);

/*============================================================================*/
/*============================================================================*/
/*==                          HELPER FUNCTIONS                              ==*/
/*============================================================================*/
/*============================================================================*/


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

/*
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

	/* correction for division: log(x) = log(x/y)+log(y) */
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

/*
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


/*
* \brief Values for NICAM prescaler gain. Computed from dB to integer
*        and rounded. For calc used formula: 16*10^(prescaleGain[dB]/20).
*
*/
#if 0
/* Currently, unused as we lack support for analog TV */
static const u16 nicam_presc_table_val[43] = {
	1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4,
	5, 5, 6, 6, 7, 8, 9, 10, 11, 13, 14, 16,
	18, 20, 23, 25, 28, 32, 36, 40, 45,
	51, 57, 64, 71, 80, 90, 101, 113, 127
};
#endif

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

/*
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

int drxbsp_i2c_write_read(struct i2c_device_addr *w_dev_addr,
				 u16 w_count,
				 u8 *wData,
				 struct i2c_device_addr *r_dev_addr,
				 u16 r_count, u8 *r_data)
{
	struct drx39xxj_state *state;
	struct i2c_msg msg[2];
	unsigned int num_msgs;

	if (w_dev_addr == NULL) {
		/* Read only */
		state = r_dev_addr->user_data;
		msg[0].addr = r_dev_addr->i2c_addr >> 1;
		msg[0].flags = I2C_M_RD;
		msg[0].buf = r_data;
		msg[0].len = r_count;
		num_msgs = 1;
	} else if (r_dev_addr == NULL) {
		/* Write only */
		state = w_dev_addr->user_data;
		msg[0].addr = w_dev_addr->i2c_addr >> 1;
		msg[0].flags = 0;
		msg[0].buf = wData;
		msg[0].len = w_count;
		num_msgs = 1;
	} else {
		/* Both write and read */
		state = w_dev_addr->user_data;
		msg[0].addr = w_dev_addr->i2c_addr >> 1;
		msg[0].flags = 0;
		msg[0].buf = wData;
		msg[0].len = w_count;
		msg[1].addr = r_dev_addr->i2c_addr >> 1;
		msg[1].flags = I2C_M_RD;
		msg[1].buf = r_data;
		msg[1].len = r_count;
		num_msgs = 2;
	}

	if (state->i2c == NULL) {
		pr_err("i2c was zero, aborting\n");
		return 0;
	}
	if (i2c_transfer(state->i2c, msg, num_msgs) != num_msgs) {
		pr_warn("drx3933: I2C write/read failed\n");
		return -EREMOTEIO;
	}

#ifdef DJH_DEBUG
	if (w_dev_addr == NULL || r_dev_addr == NULL)
		return 0;

	state = w_dev_addr->user_data;

	if (state->i2c == NULL)
		return 0;

	msg[0].addr = w_dev_addr->i2c_addr;
	msg[0].flags = 0;
	msg[0].buf = wData;
	msg[0].len = w_count;
	msg[1].addr = r_dev_addr->i2c_addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = r_data;
	msg[1].len = r_count;
	num_msgs = 2;

	pr_debug("drx3933 i2c operation addr=%x i2c=%p, wc=%x rc=%x\n",
	       w_dev_addr->i2c_addr, state->i2c, w_count, r_count);

	if (i2c_transfer(state->i2c, msg, 2) != 2) {
		pr_warn("drx3933: I2C write/read failed\n");
		return -EREMOTEIO;
	}
#endif
	return 0;
}

/*============================================================================*/

/*****************************
*
* int drxdap_fasi_read_block (
*      struct i2c_device_addr *dev_addr,      -- address of I2C device
*      u32 addr,         -- address of chip register/memory
*      u16            datasize,     -- number of bytes to read
*      u8 *data,         -- data to receive
*      u32 flags)        -- special device flags
*
* Read block data from chip address. Because the chip is word oriented,
* the number of bytes to read must be even.
*
* Make sure that the buffer to receive the data is large enough.
*
* Although this function expects an even number of bytes, it is still byte
* oriented, and the data read back is NOT translated to the endianness of
* the target platform.
*
* Output:
* - 0     if reading was successful
*                  in that case: data read is in *data.
* - -EIO  if anything went wrong
*
******************************/

static int drxdap_fasi_read_block(struct i2c_device_addr *dev_addr,
					 u32 addr,
					 u16 datasize,
					 u8 *data, u32 flags)
{
	u8 buf[4];
	u16 bufx;
	int rc;
	u16 overhead_size = 0;

	/* Check parameters ******************************************************* */
	if (dev_addr == NULL)
		return -EINVAL;

	overhead_size = (IS_I2C_10BIT(dev_addr->i2c_addr) ? 2 : 1) +
	    (DRXDAP_FASI_LONG_FORMAT(addr) ? 4 : 2);

	if ((DRXDAP_FASI_OFFSET_TOO_LARGE(addr)) ||
	    ((!(DRXDAPFASI_LONG_ADDR_ALLOWED)) &&
	     DRXDAP_FASI_LONG_FORMAT(addr)) ||
	    (overhead_size > (DRXDAP_MAX_WCHUNKSIZE)) ||
	    ((datasize != 0) && (data == NULL)) || ((datasize & 1) == 1)) {
		return -EINVAL;
	}

	/* ReadModifyWrite & mode flag bits are not allowed */
	flags &= (~DRXDAP_FASI_RMW & ~DRXDAP_FASI_MODEFLAGS);
#if DRXDAP_SINGLE_MASTER
	flags |= DRXDAP_FASI_SINGLE_MASTER;
#endif

	/* Read block from I2C **************************************************** */
	do {
		u16 todo = (datasize < DRXDAP_MAX_RCHUNKSIZE ?
			      datasize : DRXDAP_MAX_RCHUNKSIZE);

		bufx = 0;

		addr &= ~DRXDAP_FASI_FLAGS;
		addr |= flags;

#if ((DRXDAPFASI_LONG_ADDR_ALLOWED == 1) && (DRXDAPFASI_SHORT_ADDR_ALLOWED == 1))
		/* short format address preferred but long format otherwise */
		if (DRXDAP_FASI_LONG_FORMAT(addr)) {
#endif
#if (DRXDAPFASI_LONG_ADDR_ALLOWED == 1)
			buf[bufx++] = (u8) (((addr << 1) & 0xFF) | 0x01);
			buf[bufx++] = (u8) ((addr >> 16) & 0xFF);
			buf[bufx++] = (u8) ((addr >> 24) & 0xFF);
			buf[bufx++] = (u8) ((addr >> 7) & 0xFF);
#endif
#if ((DRXDAPFASI_LONG_ADDR_ALLOWED == 1) && (DRXDAPFASI_SHORT_ADDR_ALLOWED == 1))
		} else {
#endif
#if (DRXDAPFASI_SHORT_ADDR_ALLOWED == 1)
			buf[bufx++] = (u8) ((addr << 1) & 0xFF);
			buf[bufx++] =
			    (u8) (((addr >> 16) & 0x0F) |
				    ((addr >> 18) & 0xF0));
#endif
#if ((DRXDAPFASI_LONG_ADDR_ALLOWED == 1) && (DRXDAPFASI_SHORT_ADDR_ALLOWED == 1))
		}
#endif

#if DRXDAP_SINGLE_MASTER
		/*
		 * In single master mode, split the read and write actions.
		 * No special action is needed for write chunks here.
		 */
		rc = drxbsp_i2c_write_read(dev_addr, bufx, buf,
					   NULL, 0, NULL);
		if (rc == 0)
			rc = drxbsp_i2c_write_read(NULL, 0, NULL, dev_addr, todo, data);
#else
		/* In multi master mode, do everything in one RW action */
		rc = drxbsp_i2c_write_read(dev_addr, bufx, buf, dev_addr, todo,
					  data);
#endif
		data += todo;
		addr += (todo >> 1);
		datasize -= todo;
	} while (datasize && rc == 0);

	return rc;
}


/*****************************
*
* int drxdap_fasi_read_reg16 (
*     struct i2c_device_addr *dev_addr, -- address of I2C device
*     u32 addr,    -- address of chip register/memory
*     u16 *data,    -- data to receive
*     u32 flags)   -- special device flags
*
* Read one 16-bit register or memory location. The data received back is
* converted back to the target platform's endianness.
*
* Output:
* - 0     if reading was successful
*                  in that case: read data is at *data
* - -EIO  if anything went wrong
*
******************************/

static int drxdap_fasi_read_reg16(struct i2c_device_addr *dev_addr,
					 u32 addr,
					 u16 *data, u32 flags)
{
	u8 buf[sizeof(*data)];
	int rc;

	if (!data)
		return -EINVAL;

	rc = drxdap_fasi_read_block(dev_addr, addr, sizeof(*data), buf, flags);
	*data = buf[0] + (((u16) buf[1]) << 8);
	return rc;
}

/*****************************
*
* int drxdap_fasi_read_reg32 (
*     struct i2c_device_addr *dev_addr, -- address of I2C device
*     u32 addr,    -- address of chip register/memory
*     u32 *data,    -- data to receive
*     u32 flags)   -- special device flags
*
* Read one 32-bit register or memory location. The data received back is
* converted back to the target platform's endianness.
*
* Output:
* - 0     if reading was successful
*                  in that case: read data is at *data
* - -EIO  if anything went wrong
*
******************************/

static int drxdap_fasi_read_reg32(struct i2c_device_addr *dev_addr,
					 u32 addr,
					 u32 *data, u32 flags)
{
	u8 buf[sizeof(*data)];
	int rc;

	if (!data)
		return -EINVAL;

	rc = drxdap_fasi_read_block(dev_addr, addr, sizeof(*data), buf, flags);
	*data = (((u32) buf[0]) << 0) +
	    (((u32) buf[1]) << 8) +
	    (((u32) buf[2]) << 16) + (((u32) buf[3]) << 24);
	return rc;
}

/*****************************
*
* int drxdap_fasi_write_block (
*      struct i2c_device_addr *dev_addr,    -- address of I2C device
*      u32 addr,       -- address of chip register/memory
*      u16            datasize,   -- number of bytes to read
*      u8 *data,       -- data to receive
*      u32 flags)      -- special device flags
*
* Write block data to chip address. Because the chip is word oriented,
* the number of bytes to write must be even.
*
* Although this function expects an even number of bytes, it is still byte
* oriented, and the data being written is NOT translated from the endianness of
* the target platform.
*
* Output:
* - 0     if writing was successful
* - -EIO  if anything went wrong
*
******************************/

static int drxdap_fasi_write_block(struct i2c_device_addr *dev_addr,
					  u32 addr,
					  u16 datasize,
					  u8 *data, u32 flags)
{
	u8 buf[DRXDAP_MAX_WCHUNKSIZE];
	int st = -EIO;
	int first_err = 0;
	u16 overhead_size = 0;
	u16 block_size = 0;

	/* Check parameters ******************************************************* */
	if (dev_addr == NULL)
		return -EINVAL;

	overhead_size = (IS_I2C_10BIT(dev_addr->i2c_addr) ? 2 : 1) +
	    (DRXDAP_FASI_LONG_FORMAT(addr) ? 4 : 2);

	if ((DRXDAP_FASI_OFFSET_TOO_LARGE(addr)) ||
	    ((!(DRXDAPFASI_LONG_ADDR_ALLOWED)) &&
	     DRXDAP_FASI_LONG_FORMAT(addr)) ||
	    (overhead_size > (DRXDAP_MAX_WCHUNKSIZE)) ||
	    ((datasize != 0) && (data == NULL)) || ((datasize & 1) == 1))
		return -EINVAL;

	flags &= DRXDAP_FASI_FLAGS;
	flags &= ~DRXDAP_FASI_MODEFLAGS;
#if DRXDAP_SINGLE_MASTER
	flags |= DRXDAP_FASI_SINGLE_MASTER;
#endif

	/* Write block to I2C ***************************************************** */
	block_size = ((DRXDAP_MAX_WCHUNKSIZE) - overhead_size) & ~1;
	do {
		u16 todo = 0;
		u16 bufx = 0;

		/* Buffer device address */
		addr &= ~DRXDAP_FASI_FLAGS;
		addr |= flags;
#if (((DRXDAPFASI_LONG_ADDR_ALLOWED) == 1) && ((DRXDAPFASI_SHORT_ADDR_ALLOWED) == 1))
		/* short format address preferred but long format otherwise */
		if (DRXDAP_FASI_LONG_FORMAT(addr)) {
#endif
#if ((DRXDAPFASI_LONG_ADDR_ALLOWED) == 1)
			buf[bufx++] = (u8) (((addr << 1) & 0xFF) | 0x01);
			buf[bufx++] = (u8) ((addr >> 16) & 0xFF);
			buf[bufx++] = (u8) ((addr >> 24) & 0xFF);
			buf[bufx++] = (u8) ((addr >> 7) & 0xFF);
#endif
#if (((DRXDAPFASI_LONG_ADDR_ALLOWED) == 1) && ((DRXDAPFASI_SHORT_ADDR_ALLOWED) == 1))
		} else {
#endif
#if ((DRXDAPFASI_SHORT_ADDR_ALLOWED) == 1)
			buf[bufx++] = (u8) ((addr << 1) & 0xFF);
			buf[bufx++] =
			    (u8) (((addr >> 16) & 0x0F) |
				    ((addr >> 18) & 0xF0));
#endif
#if (((DRXDAPFASI_LONG_ADDR_ALLOWED) == 1) && ((DRXDAPFASI_SHORT_ADDR_ALLOWED) == 1))
		}
#endif

		/*
		   In single master mode block_size can be 0. In such a case this I2C
		   sequense will be visible: (1) write address {i2c addr,
		   4 bytes chip address} (2) write data {i2c addr, 4 bytes data }
		   (3) write address (4) write data etc...
		   Address must be rewritten because HI is reset after data transport and
		   expects an address.
		 */
		todo = (block_size < datasize ? block_size : datasize);
		if (todo == 0) {
			u16 overhead_size_i2c_addr = 0;
			u16 data_block_size = 0;

			overhead_size_i2c_addr =
			    (IS_I2C_10BIT(dev_addr->i2c_addr) ? 2 : 1);
			data_block_size =
			    (DRXDAP_MAX_WCHUNKSIZE - overhead_size_i2c_addr) & ~1;

			/* write device address */
			st = drxbsp_i2c_write_read(dev_addr,
						  (u16) (bufx),
						  buf,
						  (struct i2c_device_addr *)(NULL),
						  0, (u8 *)(NULL));

			if ((st != 0) && (first_err == 0)) {
				/* at the end, return the first error encountered */
				first_err = st;
			}
			bufx = 0;
			todo =
			    (data_block_size <
			     datasize ? data_block_size : datasize);
		}
		memcpy(&buf[bufx], data, todo);
		/* write (address if can do and) data */
		st = drxbsp_i2c_write_read(dev_addr,
					  (u16) (bufx + todo),
					  buf,
					  (struct i2c_device_addr *)(NULL),
					  0, (u8 *)(NULL));

		if ((st != 0) && (first_err == 0)) {
			/* at the end, return the first error encountered */
			first_err = st;
		}
		datasize -= todo;
		data += todo;
		addr += (todo >> 1);
	} while (datasize);

	return first_err;
}

/*****************************
*
* int drxdap_fasi_write_reg16 (
*     struct i2c_device_addr *dev_addr, -- address of I2C device
*     u32 addr,    -- address of chip register/memory
*     u16            data,    -- data to send
*     u32 flags)   -- special device flags
*
* Write one 16-bit register or memory location. The data being written is
* converted from the target platform's endianness to little endian.
*
* Output:
* - 0     if writing was successful
* - -EIO  if anything went wrong
*
******************************/

static int drxdap_fasi_write_reg16(struct i2c_device_addr *dev_addr,
					  u32 addr,
					  u16 data, u32 flags)
{
	u8 buf[sizeof(data)];

	buf[0] = (u8) ((data >> 0) & 0xFF);
	buf[1] = (u8) ((data >> 8) & 0xFF);

	return drxdap_fasi_write_block(dev_addr, addr, sizeof(data), buf, flags);
}

/*****************************
*
* int drxdap_fasi_read_modify_write_reg16 (
*      struct i2c_device_addr *dev_addr,   -- address of I2C device
*      u32 waddr,     -- address of chip register/memory
*      u32 raddr,     -- chip address to read back from
*      u16            wdata,     -- data to send
*      u16 *rdata)     -- data to receive back
*
* Write 16-bit data, then read back the original contents of that location.
* Requires long addressing format to be allowed.
*
* Before sending data, the data is converted to little endian. The
* data received back is converted back to the target platform's endianness.
*
* WARNING: This function is only guaranteed to work if there is one
* master on the I2C bus.
*
* Output:
* - 0     if reading was successful
*                  in that case: read back data is at *rdata
* - -EIO  if anything went wrong
*
******************************/

static int drxdap_fasi_read_modify_write_reg16(struct i2c_device_addr *dev_addr,
						    u32 waddr,
						    u32 raddr,
						    u16 wdata, u16 *rdata)
{
	int rc = -EIO;

#if (DRXDAPFASI_LONG_ADDR_ALLOWED == 1)
	if (rdata == NULL)
		return -EINVAL;

	rc = drxdap_fasi_write_reg16(dev_addr, waddr, wdata, DRXDAP_FASI_RMW);
	if (rc == 0)
		rc = drxdap_fasi_read_reg16(dev_addr, raddr, rdata, 0);
#endif

	return rc;
}

/*****************************
*
* int drxdap_fasi_write_reg32 (
*     struct i2c_device_addr *dev_addr, -- address of I2C device
*     u32 addr,    -- address of chip register/memory
*     u32            data,    -- data to send
*     u32 flags)   -- special device flags
*
* Write one 32-bit register or memory location. The data being written is
* converted from the target platform's endianness to little endian.
*
* Output:
* - 0     if writing was successful
* - -EIO  if anything went wrong
*
******************************/

static int drxdap_fasi_write_reg32(struct i2c_device_addr *dev_addr,
					  u32 addr,
					  u32 data, u32 flags)
{
	u8 buf[sizeof(data)];

	buf[0] = (u8) ((data >> 0) & 0xFF);
	buf[1] = (u8) ((data >> 8) & 0xFF);
	buf[2] = (u8) ((data >> 16) & 0xFF);
	buf[3] = (u8) ((data >> 24) & 0xFF);

	return drxdap_fasi_write_block(dev_addr, addr, sizeof(data), buf, flags);
}

/*============================================================================*/

/*
* \fn int drxj_dap_rm_write_reg16short
* \brief Read modify write 16 bits audio register using short format only.
* \param dev_addr
* \param waddr    Address to write to
* \param raddr    Address to read from (usually SIO_HI_RA_RAM_S0_RMWBUF__A)
* \param wdata    Data to write
* \param rdata    Buffer for data to read
* \return int
* \retval 0 Success
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
	rc = drxdap_fasi_write_reg16(dev_addr,
					      SIO_HI_RA_RAM_S0_FLG_ACC__A,
					      SIO_HI_RA_RAM_S0_FLG_ACC_S0_RWM__M,
					      0x0000);
	if (rc == 0) {
		/* Write new data: triggers RMW */
		rc = drxdap_fasi_write_reg16(dev_addr, waddr, wdata,
						      0x0000);
	}
	if (rc == 0) {
		/* Read old data */
		rc = drxdap_fasi_read_reg16(dev_addr, raddr, rdata,
						     0x0000);
	}
	if (rc == 0) {
		/* Reset RMW flag */
		rc = drxdap_fasi_write_reg16(dev_addr,
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
	return drxdap_fasi_read_modify_write_reg16(dev_addr,
							  waddr,
							  raddr, wdata, rdata);
#else
	return drxj_dap_rm_write_reg16short(dev_addr, waddr, raddr, wdata, rdata);
#endif
}


/*============================================================================*/

/*
* \fn int drxj_dap_read_aud_reg16
* \brief Read 16 bits audio register
* \param dev_addr
* \param addr
* \param data
* \return int
* \retval 0 Success
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
		start_timer = jiffies_to_msecs(jiffies);
		do {
			/* RMW to aud TR IF until request is granted or timeout */
			stat = drxj_dap_read_modify_write_reg16(dev_addr,
							     addr,
							     SIO_HI_RA_RAM_S0_RMWBUF__A,
							     0x0000, &tr_status);

			if (stat != 0)
				break;

			current_timer = jiffies_to_msecs(jiffies);
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
		start_timer = jiffies_to_msecs(jiffies);

		while ((tr_status & AUD_TOP_TR_CTR_FIFO_RD_RDY__M) !=
		       AUD_TOP_TR_CTR_FIFO_RD_RDY_READY) {
			stat = drxj_dap_read_reg16(dev_addr,
						  AUD_TOP_TR_CTR__A,
						  &tr_status, 0x0000);
			if (stat != 0)
				break;

			current_timer = jiffies_to_msecs(jiffies);
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
		stat = drxdap_fasi_read_reg16(dev_addr, addr, data, flags);

	return stat;
}
/*============================================================================*/

/*
* \fn int drxj_dap_write_aud_reg16
* \brief Write 16 bits audio register
* \param dev_addr
* \param addr
* \param data
* \return int
* \retval 0 Success
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
		start_timer = jiffies_to_msecs(jiffies);
		do {
			/* RMW to aud TR IF until request is granted or timeout */
			stat = drxj_dap_read_modify_write_reg16(dev_addr,
							     addr,
							     SIO_HI_RA_RAM_S0_RMWBUF__A,
							     data, &tr_status);
			if (stat != 0)
				break;

			current_timer = jiffies_to_msecs(jiffies);
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
		stat = drxdap_fasi_write_reg16(dev_addr,
							    addr, data, flags);

	return stat;
}

/*============================================================================*/

/* Free data ram in SIO HI */
#define SIO_HI_RA_RAM_USR_BEGIN__A 0x420040
#define SIO_HI_RA_RAM_USR_END__A   0x420060

#define DRXJ_HI_ATOMIC_BUF_START (SIO_HI_RA_RAM_USR_BEGIN__A)
#define DRXJ_HI_ATOMIC_BUF_END   (SIO_HI_RA_RAM_USR_BEGIN__A + 7)
#define DRXJ_HI_ATOMIC_READ      SIO_HI_RA_RAM_PAR_3_ACP_RW_READ
#define DRXJ_HI_ATOMIC_WRITE     SIO_HI_RA_RAM_PAR_3_ACP_RW_WRITE

/*
* \fn int drxj_dap_atomic_read_write_block()
* \brief Basic access routine for atomic read or write access
* \param dev_addr  pointer to i2c dev address
* \param addr     destination/source address
* \param datasize size of data buffer in bytes
* \param data     pointer to data buffer
* \return int
* \retval 0 Success
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
			rc = drxj_dap_read_reg16(dev_addr,
						 (DRXJ_HI_ATOMIC_BUF_START + i),
						 &word, 0);
			if (rc) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data[2 * i] = (u8) (word & 0xFF);
			data[(2 * i) + 1] = (u8) (word >> 8);
		}
	}

	return 0;

rw_error:
	return rc;

}

/*============================================================================*/

/*
* \fn int drxj_dap_atomic_read_reg32()
* \brief Atomic read of 32 bits words
*/
static
int drxj_dap_atomic_read_reg32(struct i2c_device_addr *dev_addr,
				     u32 addr,
				     u32 *data, u32 flags)
{
	u8 buf[sizeof(*data)] = { 0 };
	int rc;
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

/*
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
	return rc;
}

/*
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
		rc = drxj_dap_write_reg16(dev_addr, SIO_HI_RA_RAM_PAR_6__A, cmd->param6, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_HI_RA_RAM_PAR_5__A, cmd->param5, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_HI_RA_RAM_PAR_4__A, cmd->param4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_HI_RA_RAM_PAR_3__A, cmd->param3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* fallthrough */
	case SIO_HI_RA_RAM_CMD_BRDCTRL:
		rc = drxj_dap_write_reg16(dev_addr, SIO_HI_RA_RAM_PAR_2__A, cmd->param2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_HI_RA_RAM_PAR_1__A, cmd->param1, 0);
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
	rc = drxj_dap_write_reg16(dev_addr, SIO_HI_RA_RAM_CMD__A, cmd->cmd, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	if ((cmd->cmd) == SIO_HI_RA_RAM_CMD_RESET)
		msleep(1);

	/* Detect power down to omit reading result */
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

			rc = drxj_dap_read_reg16(dev_addr, SIO_HI_RA_RAM_CMD__A, &wait_cmd, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} while (wait_cmd != 0);

		/* Read result */
		rc = drxj_dap_read_reg16(dev_addr, SIO_HI_RA_RAM_RES__A, result, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

	}
	/* if ( powerdown_cmd == true ) */
	return 0;
rw_error:
	return rc;
}

/*
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
	rc = drxj_dap_write_reg16(dev_addr, 0x4301D7, 0x801, 0);
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
	return rc;
}

/*============================================================================*/
/*==                   END HOST INTERFACE FUNCTIONS                         ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                        AUXILIARY FUNCTIONS                             ==*/
/*============================================================================*/
/*============================================================================*/

/*
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

	rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_read_reg16(dev_addr, SIO_PDR_OHW_CFG__A, &sio_pdr_ohw_cfg, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY__PRE, 0);
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
	rc = drxdap_fasi_read_reg32(dev_addr, SIO_TOP_JTAGID_LO__A, &sio_top_jtagid_lo, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->mfx = (u8) ((sio_top_jtagid_lo >> 29) & 0xF);

	switch ((sio_top_jtagid_lo >> 12) & 0xFF) {
	case 0x31:
		rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_read_reg16(dev_addr, SIO_PDR_UIO_IN_HI__A, &bid, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		bid = (bid >> 10) & 0xf;
		rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY__PRE, 0);
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
	return rc;
}

/*
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
		msleep(10);
		retry_count++;
	} while ((drxbsp_i2c_write_read
		  ((struct i2c_device_addr *) (NULL), 0, (u8 *)(NULL), dev_addr, 1,
		   &data)
		  != 0) && (retry_count < DRXJ_MAX_RETRIES_POWERUP));

	/* Need some recovery time .... */
	msleep(10);

	if (retry_count == DRXJ_MAX_RETRIES_POWERUP)
		return -EIO;

	return 0;
}

/*----------------------------------------------------------------------------*/
/* MPEG Output Configuration Functions - begin                                */
/*----------------------------------------------------------------------------*/
/*
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
		/* quick and dirty patch to set MPEG in case current std is not
		   producing MPEG */
		switch (ext_attr->standard) {
		case DRX_STANDARD_8VSB:
		case DRX_STANDARD_ITU_A:
		case DRX_STANDARD_ITU_B:
		case DRX_STANDARD_ITU_C:
			break;
		default:
			return 0;
		}

		rc = drxj_dap_write_reg16(dev_addr, FEC_OC_OCR_INVERT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		switch (ext_attr->standard) {
		case DRX_STANDARD_8VSB:
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_FCT_USAGE__A, 7, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* 2048 bytes fifo ram */
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_TMD_CTL_UPD_RATE__A, 10, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_TMD_INT_UPD_RATE__A, 10, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_AVR_PARM_A__A, 5, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_AVR_PARM_B__A, 7, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_RCN_GAIN__A, 10, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			/* Low Water Mark for synchronization  */
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_SNC_LWM__A, 3, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			/* High Water Mark for synchronization */
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_SNC_HWM__A, 5, 0);
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
			/* fall-through - as b/c Annex A/C need following settings */
		case DRX_STANDARD_ITU_B:
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_FCT_USAGE__A, FEC_OC_FCT_USAGE__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_TMD_CTL_UPD_RATE__A, FEC_OC_TMD_CTL_UPD_RATE__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_TMD_INT_UPD_RATE__A, 5, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_AVR_PARM_A__A, FEC_OC_AVR_PARM_A__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_AVR_PARM_B__A, FEC_OC_AVR_PARM_B__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			if (cfg_data->static_clk == true) {
				rc = drxj_dap_write_reg16(dev_addr, FEC_OC_RCN_GAIN__A, 0xD, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
			} else {
				rc = drxj_dap_write_reg16(dev_addr, FEC_OC_RCN_GAIN__A, FEC_OC_RCN_GAIN__PRE, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_SNC_LWM__A, 2, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_SNC_HWM__A, 12, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			break;
		default:
			break;
		}		/* switch (standard) */

		/* Check insertion of the Reed-Solomon parity bytes */
		rc = drxj_dap_read_reg16(dev_addr, FEC_OC_MODE__A, &fec_oc_reg_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_read_reg16(dev_addr, FEC_OC_IPR_MODE__A, &fec_oc_reg_ipr_mode, 0);
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

		if (cfg_data->enable_parallel == true) {	/* MPEG data output is parallel -> clear ipr_mode[0] */
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
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_DTO_RATE_HI__A, (u16)((dto_rate >> 16) & FEC_OC_DTO_RATE_HI__M), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_DTO_RATE_LO__A, (u16)(dto_rate & FEC_OC_DTO_RATE_LO_RATE_LO__M), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_DTO_MODE__A, FEC_OC_DTO_MODE_DYNAMIC__M | FEC_OC_DTO_MODE_OFFSET_ENABLE__M, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_FCT_MODE__A, FEC_OC_FCT_MODE_RAT_ENA__M | FEC_OC_FCT_MODE_VIRT_ENA__M, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_DTO_BURST_LEN__A, fec_oc_dto_burst_len, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			if (ext_attr->mpeg_output_clock_rate != DRXJ_MPEGOUTPUT_CLOCK_RATE_AUTO)
				fec_oc_dto_period = ext_attr->mpeg_output_clock_rate - 1;
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_DTO_PERIOD__A, fec_oc_dto_period, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} else {	/* Dynamic mode */

			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_DTO_MODE__A, FEC_OC_DTO_MODE_DYNAMIC__M, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, FEC_OC_FCT_MODE__A, 0, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}

		rc = drxdap_fasi_write_reg32(dev_addr, FEC_OC_RCN_CTL_RATE_LO__A, rcn_rate, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* Write appropriate registers with requested configuration */
		rc = drxj_dap_write_reg16(dev_addr, FEC_OC_MODE__A, fec_oc_reg_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, FEC_OC_IPR_MODE__A, fec_oc_reg_ipr_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, FEC_OC_IPR_INVERT__A, fec_oc_reg_ipr_invert, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* enabling for both parallel and serial now */
		/*  Write magic word to enable pdr reg write */
		rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, 0xFABA, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/*  Set MPEG TS pads to outputmode */
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MSTRT_CFG__A, 0x0013, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MERR_CFG__A, 0x0013, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MCLK_CFG__A, MPEG_OUTPUT_CLK_DRIVE_STRENGTH << SIO_PDR_MCLK_CFG_DRIVE__B | 0x03 << SIO_PDR_MCLK_CFG_MODE__B, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MVAL_CFG__A, 0x0013, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		sio_pdr_md_cfg =
		    MPEG_SERIAL_OUTPUT_PIN_DRIVE_STRENGTH <<
		    SIO_PDR_MD0_CFG_DRIVE__B | 0x03 << SIO_PDR_MD0_CFG_MODE__B;
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD0_CFG__A, sio_pdr_md_cfg, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (cfg_data->enable_parallel == true) {	/* MPEG data output is parallel -> set MD1 to MD7 to output mode */
			sio_pdr_md_cfg =
			    MPEG_PARALLEL_OUTPUT_PIN_DRIVE_STRENGTH <<
			    SIO_PDR_MD0_CFG_DRIVE__B | 0x03 <<
			    SIO_PDR_MD0_CFG_MODE__B;
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD0_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD1_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD2_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD3_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD4_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD5_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD6_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD7_CFG__A, sio_pdr_md_cfg, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} else {	/* MPEG data output is serial -> set MD1 to MD7 to tri-state */
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD1_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD2_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD3_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD4_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD5_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD6_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD7_CFG__A, 0x0000, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		/*  Enable Monitor Bus output over MPEG pads and ctl input */
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MON_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/*  Write nomagic word to enable pdr reg write */
		rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		/*  Write magic word to enable pdr reg write */
		rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, 0xFABA, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/*  Set MPEG TS pads to inputmode */
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MSTRT_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MERR_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MCLK_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MVAL_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD0_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD1_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD2_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD3_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD4_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD5_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD6_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MD7_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* Enable Monitor Bus output over MPEG pads and ctl input */
		rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_MON_CFG__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* Write nomagic word to enable pdr reg write */
		rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* save values for restore after re-acquire */
	common_attr->mpeg_cfg.enable_mpeg_output = cfg_data->enable_mpeg_output;

	return 0;
rw_error:
	return rc;
}

/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
/* MPEG Output Configuration Functions - end                                  */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* miscellaneous configurations - begin                           */
/*----------------------------------------------------------------------------*/

/*
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

	rc = drxj_dap_read_reg16(dev_addr, FEC_OC_DPR_MODE__A, &fec_oc_dpr_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_read_reg16(dev_addr, FEC_OC_SNC_MODE__A, &fec_oc_snc_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_read_reg16(dev_addr, FEC_OC_EMS_MODE__A, &fec_oc_ems_mode, 0);
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

	rc = drxj_dap_write_reg16(dev_addr, FEC_OC_DPR_MODE__A, fec_oc_dpr_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, FEC_OC_SNC_MODE__A, fec_oc_snc_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, FEC_OC_EMS_MODE__A, fec_oc_ems_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*----------------------------------------------------------------------------*/
/*
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

	rc = drxj_dap_read_reg16(dev_addr, FEC_OC_IPR_MODE__A, &fec_oc_ipr_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* reset to default (normal bit order) */
	fec_oc_ipr_mode &= (~FEC_OC_IPR_MODE_REVERSE_ORDER__M);

	if (ext_attr->bit_reverse_mpeg_outout)
		fec_oc_ipr_mode |= FEC_OC_IPR_MODE_REVERSE_ORDER__M;

	rc = drxj_dap_write_reg16(dev_addr, FEC_OC_IPR_MODE__A, fec_oc_ipr_mode, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*----------------------------------------------------------------------------*/
/*
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
		rc = drxj_dap_read_reg16(dev_addr, FEC_OC_COMM_MB__A, &fec_oc_comm_mb, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		fec_oc_comm_mb &= ~FEC_OC_COMM_MB_CTL_ON;
		if (ext_attr->mpeg_start_width == DRXJ_MPEG_START_WIDTH_8CLKCYC)
			fec_oc_comm_mb |= FEC_OC_COMM_MB_CTL_ON;
		rc = drxj_dap_write_reg16(dev_addr, FEC_OC_COMM_MB__A, fec_oc_comm_mb, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	return 0;
rw_error:
	return rc;
}

/*----------------------------------------------------------------------------*/
/* miscellaneous configurations - end                             */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* UIO Configuration Functions - begin                                        */
/*----------------------------------------------------------------------------*/
/*
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
	rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
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
		case DRX_UIO_MODE_FIRMWARE_SMA:	/* fall through */
		case DRX_UIO_MODE_FIRMWARE_SAW:	/* fall through */
		case DRX_UIO_MODE_READWRITE:
			ext_attr->uio_sma_tx_mode = uio_cfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			ext_attr->uio_sma_tx_mode = uio_cfg->mode;
			/* pad configuration register is set 0 - input mode */
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_SMA_TX_CFG__A, 0, 0);
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
		case DRX_UIO_MODE_FIRMWARE0:	/* fall through */
		case DRX_UIO_MODE_READWRITE:
			ext_attr->uio_sma_rx_mode = uio_cfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			ext_attr->uio_sma_rx_mode = uio_cfg->mode;
			/* pad configuration register is set 0 - input mode */
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_SMA_RX_CFG__A, 0, 0);
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
		case DRX_UIO_MODE_FIRMWARE0:	/* fall through */
		case DRX_UIO_MODE_READWRITE:
			ext_attr->uio_gpio_mode = uio_cfg->mode;
			break;
		case DRX_UIO_MODE_DISABLE:
			ext_attr->uio_gpio_mode = uio_cfg->mode;
			/* pad configuration register is set 0 - input mode */
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_GPIO_CFG__A, 0, 0);
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
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_IRQN_CFG__A, 0, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			ext_attr->uio_irqn_mode = uio_cfg->mode;
			break;
		case DRX_UIO_MODE_FIRMWARE0:	/* fall through */
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
	rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*
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
	rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
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
		rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_SMA_TX_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* use corresponding bit in io data output registar */
		rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (!uio_data->value)
			value &= 0x7FFF;	/* write zero to 15th bit - 1st UIO */
		else
			value |= 0x8000;	/* write one to 15th bit - 1st UIO */

		/* write back to io data output register */
		rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, value, 0);
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
		rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_SMA_RX_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* use corresponding bit in io data output registar */
		rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (!uio_data->value)
			value &= 0xBFFF;	/* write zero to 14th bit - 2nd UIO */
		else
			value |= 0x4000;	/* write one to 14th bit - 2nd UIO */

		/* write back to io data output register */
		rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, value, 0);
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
		rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_GPIO_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* use corresponding bit in io data output registar */
		rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_HI__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (!uio_data->value)
			value &= 0xFFFB;	/* write zero to 2nd bit - 3rd UIO */
		else
			value |= 0x0004;	/* write one to 2nd bit - 3rd UIO */

		/* write back to io data output register */
		rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_HI__A, value, 0);
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
		rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_IRQN_CFG__A, pin_cfg_value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		/* use corresponding bit in io data output registar */
		rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, &value, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (uio_data->value == false)
			value &= 0xEFFF;	/* write zero to 12th bit - 4th UIO */
		else
			value |= 0x1000;	/* write one to 12th bit - 4th UIO */

		/* write back to io data output register */
		rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_UIO_OUT_LO__A, value, 0);
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
	rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*---------------------------------------------------------------------------*/
/* UIO Configuration Functions - end                                         */
/*---------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* I2C Bridge Functions - begin                                               */
/*----------------------------------------------------------------------------*/
/*
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
/*
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
	rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* init smart antenna */
	rc = drxj_dap_read_reg16(dev_addr, SIO_SA_TX_COMMAND__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (ext_attr->smart_ant_inverted) {
		rc = drxj_dap_write_reg16(dev_addr, SIO_SA_TX_COMMAND__A, (data | SIO_SA_TX_COMMAND_TX_INVERT__M) | SIO_SA_TX_COMMAND_TX_ENABLE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	} else {
		rc = drxj_dap_write_reg16(dev_addr, SIO_SA_TX_COMMAND__A, (data & (~SIO_SA_TX_COMMAND_TX_INVERT__M)) | SIO_SA_TX_COMMAND_TX_ENABLE__M, 0);
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
	rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_SMA_TX_CFG__A, 0x13, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_PDR_SMA_TX_GPIO_FNC__A, 0x03, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/*  Write magic word to disable pdr reg write               */
	rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

static int scu_command(struct i2c_device_addr *dev_addr, struct drxjscu_cmd *cmd)
{
	int rc;
	u16 cur_cmd = 0;
	unsigned long timeout;

	/* Check param */
	if (cmd == NULL)
		return -EINVAL;

	/* Wait until SCU command interface is ready to receive command */
	rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_COMMAND__A, &cur_cmd, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (cur_cmd != DRX_SCU_READY)
		return -EIO;

	switch (cmd->parameter_len) {
	case 5:
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_PARAM_4__A, *(cmd->parameter + 4), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* fallthrough */
	case 4:
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_PARAM_3__A, *(cmd->parameter + 3), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* fallthrough */
	case 3:
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_PARAM_2__A, *(cmd->parameter + 2), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* fallthrough */
	case 2:
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_PARAM_1__A, *(cmd->parameter + 1), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* fallthrough */
	case 1:
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_PARAM_0__A, *(cmd->parameter + 0), 0);
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
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_COMMAND__A, cmd->command, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Wait until SCU has processed command */
	timeout = jiffies + msecs_to_jiffies(DRXJ_MAX_WAITTIME);
	while (time_is_after_jiffies(timeout)) {
		rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_COMMAND__A, &cur_cmd, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		if (cur_cmd == DRX_SCU_READY)
			break;
		usleep_range(1000, 2000);
	}

	if (cur_cmd != DRX_SCU_READY)
		return -EIO;

	/* read results */
	if ((cmd->result_len > 0) && (cmd->result != NULL)) {
		s16 err;

		switch (cmd->result_len) {
		case 4:
			rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_PARAM_3__A, cmd->result + 3, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* fallthrough */
		case 3:
			rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_PARAM_2__A, cmd->result + 2, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* fallthrough */
		case 2:
			rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_PARAM_1__A, cmd->result + 1, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* fallthrough */
		case 1:
			rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_PARAM_0__A, cmd->result + 0, 0);
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
	return rc;
}

/*
* \fn int DRXJ_DAP_SCUAtomicReadWriteBlock()
* \brief Basic access routine for SCU atomic read or write access
* \param dev_addr  pointer to i2c dev address
* \param addr     destination/source address
* \param datasize size of data buffer in bytes
* \param data     pointer to data buffer
* \return int
* \retval 0 Success
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
	u16 set_param_parameters[18];
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
	return rc;

}

/*============================================================================*/

/*
* \fn int DRXJ_DAP_AtomicReadReg16()
* \brief Atomic read of 16 bits words
*/
static
int drxj_dap_scu_atomic_read_reg16(struct i2c_device_addr *dev_addr,
					 u32 addr,
					 u16 *data, u32 flags)
{
	u8 buf[2] = { 0 };
	int rc;
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
/*
* \fn int drxj_dap_scu_atomic_write_reg16()
* \brief Atomic read of 16 bits words
*/
static
int drxj_dap_scu_atomic_write_reg16(struct i2c_device_addr *dev_addr,
					  u32 addr,
					  u16 data, u32 flags)
{
	u8 buf[2];
	int rc;

	buf[0] = (u8) (data & 0xff);
	buf[1] = (u8) ((data >> 8) & 0xff);

	rc = drxj_dap_scu_atomic_read_write_block(dev_addr, addr, 2, buf, false);

	return rc;
}

/* -------------------------------------------------------------------------- */
/*
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
	rc = drxj_dap_write_reg16(dev_addr, IQM_AF_COMM_EXEC__A, IQM_AF_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_AF_START_LOCK__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Wait at least 3*128*(1/sysclk) <<< 1 millisec */
	msleep(1);

	*count = 0;
	rc = drxj_dap_read_reg16(dev_addr, IQM_AF_PHASE0__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (data == 127)
		*count = *count + 1;
	rc = drxj_dap_read_reg16(dev_addr, IQM_AF_PHASE1__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (data == 127)
		*count = *count + 1;
	rc = drxj_dap_read_reg16(dev_addr, IQM_AF_PHASE2__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (data == 127)
		*count = *count + 1;

	return 0;
rw_error:
	return rc;
}

/*
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
		/* Try sampling on a different edge */
		u16 clk_neg = 0;

		rc = drxj_dap_read_reg16(dev_addr, IQM_AF_CLKNEG__A, &clk_neg, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		clk_neg ^= IQM_AF_CLKNEG_CLKNEGDATA__M;
		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_CLKNEG__A, clk_neg, 0);
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
	return rc;
}

/*============================================================================*/
/*==                      END AUXILIARY FUNCTIONS                           ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                8VSB & QAM COMMON DATAPATH FUNCTIONS                    ==*/
/*============================================================================*/
/*============================================================================*/
/*
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
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_MINGAIN__A, 0x7fff, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_MAXGAIN__A, 0x0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_SUM__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_CYCCNT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_DIR_WD__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_DIR_STP__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_SUM__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_CYCCNT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_DIR_WD__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_DIR_STP__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_INGAIN__A, 1024, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_VSB_AGC_POW_TGT__A, 22600, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_INGAIN_TGT__A, 13200, 0);
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
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_MINGAIN__A, 0x7fff, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_MAXGAIN__A, 0x0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_SUM__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_CYCCNT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_DIR_WD__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_DIR_STP__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_SUM__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_CYCCNT__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_DIR_WD__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_DIR_STP__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		p_agc_if_settings = &(ext_attr->qam_if_agc_cfg);
		p_agc_rf_settings = &(ext_attr->qam_rf_agc_cfg);
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_INGAIN_TGT__A, p_agc_if_settings->top, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_AGC_KI__A, &agc_ki, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		agc_ki &= 0xf000;
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI__A, agc_ki, 0);
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
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_INGAIN_TGT_MIN__A, p_agc_if_settings->top, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_INGAIN__A, p_agc_if_settings->top, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* Gain fed from inner to outer AGC */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_INGAIN_TGT_MAX__A, ingain_tgt_max, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_IF_IACCU_HI_TGT_MIN__A, if_iaccu_hi_tgt_min, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_IF_IACCU_HI__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* set to p_agc_settings->top before */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_IF_IACCU_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_RF_IACCU_HI__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_RF_IACCU_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_RF_MAX__A, 32767, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_SUM_MAX__A, clp_sum_max, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_SUM_MAX__A, sns_sum_max, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_INNERGAIN_MIN__A, ki_innergain_min, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_FAST_SNS_CTRL_DELAY__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_CYCLEN__A, 500, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_CYCLEN__A, 500, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_MAXMINGAIN_TH__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_MIN__A, ki_min, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_MAX__A, ki_max, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI_RED__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_SUM_MIN__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_CYCLEN__A, 500, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_DIR_TO__A, clp_dir_to, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_SUM_MIN__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_SNS_DIR_TO__A, sns_dir_to, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_FAST_CLP_CTRL_DELAY__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_CLP_CTRL_MODE__A, clp_ctrl_mode, 0);
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

	rc = drxj_dap_write_reg16(dev_addr, IQM_AF_AGC_RF__A, agc_rf, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_AF_AGC_IF__A, agc_if, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Set/restore Ki DGAIN factor */
	rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_AGC_KI__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	data &= ~SCU_RAM_AGC_KI_DGAIN__M;
	data |= (agc_ki_dgain << SCU_RAM_AGC_KI_DGAIN__B);
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_AGC_KI__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*
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
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_C:
	case DRX_STANDARD_PAL_SECAM_LP:
	case DRX_STANDARD_8VSB:
		select_pos_image = true;
		break;
	case DRX_STANDARD_FM:
		/* After IQM FS sound carrier must appear at 4 Mhz in spect.
		   Sound carrier is already 3Mhz above centre frequency due
		   to tuner setting so now add an extra shift of 1MHz... */
		fm_frequency_shift = 1000;
		/*fall through */
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_NTSC:
	case DRX_STANDARD_PAL_SECAM_BG:
	case DRX_STANDARD_PAL_SECAM_DK:
	case DRX_STANDARD_PAL_SECAM_I:
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
	rc = drxdap_fasi_write_reg32(dev_addr, IQM_FS_RATE_OFS_LO__A, iqm_fs_rate_ofs, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->iqm_fs_rate_ofs = iqm_fs_rate_ofs;
	ext_attr->pos_image = (bool) (rf_mirror ^ tuner_mirror ^ select_pos_image);

	return 0;
rw_error:
	return rc;
}

/*
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

	rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, &data, 0);
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
	return rc;
}
#endif


/*============================================================================*/

/*
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
		scu_rr16 = drxj_dap_read_reg16;
		scu_wr16 = drxj_dap_write_reg16;
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
			rc = drxj_dap_read_reg16(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data |= IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE;
			rc = drxj_dap_write_reg16(dev_addr, IQM_AF_STDBY__A, data, 0);
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
			rc = drxj_dap_read_reg16(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data |= IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE;
			rc = drxj_dap_write_reg16(dev_addr, IQM_AF_STDBY__A, data, 0);
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
			rc = drxj_dap_read_reg16(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= (~IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE);
			rc = drxj_dap_write_reg16(dev_addr, IQM_AF_STDBY__A, data, 0);
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
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return rc;
}

/*
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
		scu_rr16 = drxj_dap_read_reg16;
		scu_wr16 = drxj_dap_write_reg16;
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
			rc = drxj_dap_read_reg16(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data |= IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE;
			rc = drxj_dap_write_reg16(dev_addr, IQM_AF_STDBY__A, data, 0);
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
			rc = drxj_dap_read_reg16(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data |= IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE;
			rc = drxj_dap_write_reg16(dev_addr, IQM_AF_STDBY__A, data, 0);
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
			rc = drxj_dap_read_reg16(dev_addr, IQM_AF_STDBY__A, &data, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			data &= (~IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE);
			rc = drxj_dap_write_reg16(dev_addr, IQM_AF_STDBY__A, data, 0);
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
	default:
		return -EIO;
	}

	return 0;
rw_error:
	return rc;
}

/*
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
	rc = drxj_dap_read_reg16(dev_addr, IQM_AF_STDBY__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (!active)
		data &= ((~IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_PD_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE) & (~IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE));
	else
		data |= (IQM_AF_STDBY_STDBY_ADC_A2_ACTIVE | IQM_AF_STDBY_STDBY_AMP_A2_ACTIVE | IQM_AF_STDBY_STDBY_PD_A2_ACTIVE | IQM_AF_STDBY_STDBY_TAGC_IF_A2_ACTIVE | IQM_AF_STDBY_STDBY_TAGC_RF_A2_ACTIVE);
	rc = drxj_dap_write_reg16(dev_addr, IQM_AF_STDBY__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/
/*==              END 8VSB & QAM COMMON DATAPATH FUNCTIONS                  ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                       8VSB DATAPATH FUNCTIONS                          ==*/
/*============================================================================*/
/*============================================================================*/

/*
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
	rc = drxj_dap_write_reg16(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_COMM_EXEC__A, VSB_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (primary) {
		rc = drxj_dap_write_reg16(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_STOP, 0);
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
		rc = drxj_dap_write_reg16(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
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
	return rc;
}

/*
* \fn int set_vsb_leak_n_gain ()
* \brief Set ATSC demod.
* \param demod instance of demodulator.
* \return int.
*/
static int set_vsb_leak_n_gain(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = NULL;
	int rc;

	static const u8 vsb_ffe_leak_gain_ram0[] = {
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

	static const u8 vsb_ffe_leak_gain_ram1[] = {
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
	rc = drxdap_fasi_write_block(dev_addr, VSB_SYSCTRL_RAM0_FFETRAINLKRATIO1__A, sizeof(vsb_ffe_leak_gain_ram0), ((u8 *)vsb_ffe_leak_gain_ram0), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxdap_fasi_write_block(dev_addr, VSB_SYSCTRL_RAM1_FIRRCA1GAIN9__A, sizeof(vsb_ffe_leak_gain_ram1), ((u8 *)vsb_ffe_leak_gain_ram1), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*
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
	static const u8 vsb_taps_re[] = {
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
	rc = drxj_dap_write_reg16(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_COMM_EXEC__A, VSB_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
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

	rc = drxj_dap_write_reg16(dev_addr, IQM_AF_DCF_BYPASS__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_FS_ADJ_SEL__A, IQM_FS_ADJ_SEL_B_VSB, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_RC_ADJ_SEL__A, IQM_RC_ADJ_SEL_B_VSB, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->iqm_rc_rate_ofs = 0x00AD0D79;
	rc = drxdap_fasi_write_reg32(dev_addr, IQM_RC_RATE_OFS_LO__A, ext_attr->iqm_rc_rate_ofs, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_CFAGC_GAINSHIFT__A, 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_CYGN1TRK__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, IQM_RC_CROUT_ENA__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_RC_STRETCH__A, 28, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_RT_ACTIVE__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_CF_SYMMETRIC__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_CF_MIDTAP__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_CF_OUT_ENA__A, IQM_CF_OUT_ENA_VSB__M, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_CF_SCALE__A, 1393, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_CF_SCALE_SH__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_CF_POW_MEAS_LEN__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_RE0__A, sizeof(vsb_taps_re), ((u8 *)vsb_taps_re), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_IM0__A, sizeof(vsb_taps_re), ((u8 *)vsb_taps_re), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_BNTHRESH__A, 330, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* set higher threshold */
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_CLPLASTNUM__A, 90, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* burst detection on   */
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_SNRTH_RCA1__A, 0x0042, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* drop thresholds by 1 dB */
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_SNRTH_RCA2__A, 0x0053, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* drop thresholds by 2 dB */
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_EQCTRL__A, 0x1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* cma on               */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_GPIO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* GPIO               */

	/* Initialize the FEC Subsystem */
	rc = drxj_dap_write_reg16(dev_addr, FEC_TOP_ANNEX__A, FEC_TOP_ANNEX_D, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	{
		u16 fec_oc_snc_mode = 0;
		rc = drxj_dap_read_reg16(dev_addr, FEC_OC_SNC_MODE__A, &fec_oc_snc_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		/* output data even when not locked */
		rc = drxj_dap_write_reg16(dev_addr, FEC_OC_SNC_MODE__A, fec_oc_snc_mode | FEC_OC_SNC_MODE_UNLOCK_ENABLE__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	/* set clip */
	rc = drxj_dap_write_reg16(dev_addr, IQM_AF_CLP_LEN__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_AF_CLP_TH__A, 470, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, IQM_AF_SNS_LEN__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_SNRTH_PT__A, 0xD4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* no transparent, no A&C framing; parity is set in mpegoutput */
	{
		u16 fec_oc_reg_mode = 0;
		rc = drxj_dap_read_reg16(dev_addr, FEC_OC_MODE__A, &fec_oc_reg_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, FEC_OC_MODE__A, fec_oc_reg_mode & (~(FEC_OC_MODE_TRANSPARENT__M | FEC_OC_MODE_CLEAR__M | FEC_OC_MODE_RETAIN_FRAMING__M)), 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	rc = drxj_dap_write_reg16(dev_addr, FEC_DI_TIMEOUT_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* timeout counter for restarting */
	rc = drxj_dap_write_reg16(dev_addr, FEC_DI_TIMEOUT_HI__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, FEC_RS_MODE__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/* bypass disabled */
	/* initialize RS packet error measurement parameters */
	rc = drxj_dap_write_reg16(dev_addr, FEC_RS_MEASUREMENT_PERIOD__A, FEC_RS_MEASUREMENT_PERIOD, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, FEC_RS_MEASUREMENT_PRESCALE__A, FEC_RS_MEASUREMENT_PRESCALE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* init measurement period of MER/SER */
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_MEASUREMENT_PERIOD__A, VSB_TOP_MEASUREMENT_PERIOD, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxdap_fasi_write_reg32(dev_addr, SCU_RAM_FEC_ACCUM_CW_CORRECTED_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_FEC_MEAS_COUNT__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_CKGN1TRK__A, 128, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* B-Input to ADC, PGA+filter in standby */
	if (!ext_attr->has_lna) {
		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_AMUX__A, 0x02, 0);
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

		memcpy(&cfg_mpeg_output, &common_attr->mpeg_cfg, sizeof(cfg_mpeg_output));
		cfg_mpeg_output.enable_mpeg_output = true;

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

	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_BEAGC_GAINSHIFT__A, 0x0004, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_SNRTH_PT__A, 0x00D2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_SYSSMTRNCTRL__A, VSB_TOP_SYSSMTRNCTRL__PRE | VSB_TOP_SYSSMTRNCTRL_NCOTIMEOUTCNTEN__M, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_BEDETCTRL__A, 0x142, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_LBAGCREFLVL__A, 640, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_CYGN1ACQ__A, 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_CYGN1TRK__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_TOP_CYGN2TRK__A, 3, 0);
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

	rc = drxj_dap_write_reg16(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, VSB_COMM_EXEC__A, VSB_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*
* \fn static short get_vsb_post_rs_pck_err(struct i2c_device_addr *dev_addr, u16 *PckErrs)
* \brief Get the values of packet error in 8VSB mode
* \return Error code
*/
static int get_vsb_post_rs_pck_err(struct i2c_device_addr *dev_addr,
				   u32 *pck_errs, u32 *pck_count)
{
	int rc;
	u16 data = 0;
	u16 period = 0;
	u16 prescale = 0;
	u16 packet_errors_mant = 0;
	u16 packet_errors_exp = 0;

	rc = drxj_dap_read_reg16(dev_addr, FEC_RS_NR_FAILURES__A, &data, 0);
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
	*pck_errs = packet_errors_mant * (1 << packet_errors_exp);
	*pck_count = period * prescale * 77;

	return 0;
rw_error:
	return rc;
}

/*
* \fn static short GetVSBBer(struct i2c_device_addr *dev_addr, u32 *ber)
* \brief Get the values of ber in VSB mode
* \return Error code
*/
static int get_vs_bpost_viterbi_ber(struct i2c_device_addr *dev_addr,
				    u32 *ber, u32 *cnt)
{
	int rc;
	u16 data = 0;
	u16 period = 0;
	u16 prescale = 0;
	u16 bit_errors_mant = 0;
	u16 bit_errors_exp = 0;

	rc = drxj_dap_read_reg16(dev_addr, FEC_RS_NR_BIT_ERRORS__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	period = FEC_RS_MEASUREMENT_PERIOD;
	prescale = FEC_RS_MEASUREMENT_PRESCALE;

	bit_errors_mant = data & FEC_RS_NR_BIT_ERRORS_FIXED_MANT__M;
	bit_errors_exp = (data & FEC_RS_NR_BIT_ERRORS_EXP__M)
	    >> FEC_RS_NR_BIT_ERRORS_EXP__B;

	*cnt = period * prescale * 207 * ((bit_errors_exp > 2) ? 1 : 8);

	if (((bit_errors_mant << bit_errors_exp) >> 3) > 68700)
		*ber = (*cnt) * 26570;
	else {
		if (period * prescale == 0) {
			pr_err("error: period and/or prescale is zero!\n");
			return -EIO;
		}
		*ber = bit_errors_mant << ((bit_errors_exp > 2) ?
			(bit_errors_exp - 3) : bit_errors_exp);
	}

	return 0;
rw_error:
	return rc;
}

/*
* \fn static short get_vs_bpre_viterbi_ber(struct i2c_device_addr *dev_addr, u32 *ber)
* \brief Get the values of ber in VSB mode
* \return Error code
*/
static int get_vs_bpre_viterbi_ber(struct i2c_device_addr *dev_addr,
				   u32 *ber, u32 *cnt)
{
	u16 data = 0;
	int rc;

	rc = drxj_dap_read_reg16(dev_addr, VSB_TOP_NR_SYM_ERRS__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		return -EIO;
	}
	*ber = data;
	*cnt = VSB_TOP_MEASUREMENT_PERIOD * SYMBOLS_PER_SEGMENT;

	return 0;
}

/*
* \fn static int get_vsbmer(struct i2c_device_addr *dev_addr, u16 *mer)
* \brief Get the values of MER
* \return Error code
*/
static int get_vsbmer(struct i2c_device_addr *dev_addr, u16 *mer)
{
	int rc;
	u16 data_hi = 0;

	rc = drxj_dap_read_reg16(dev_addr, VSB_TOP_ERR_ENERGY_H__A, &data_hi, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	*mer =
	    (u16) (log1_times100(21504) - log1_times100((data_hi << 6) / 52));

	return 0;
rw_error:
	return rc;
}


/*============================================================================*/
/*==                     END 8VSB DATAPATH FUNCTIONS                        ==*/
/*============================================================================*/

/*============================================================================*/
/*============================================================================*/
/*==                       QAM DATAPATH FUNCTIONS                           ==*/
/*============================================================================*/
/*============================================================================*/

/*
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
	struct drx_common_attr *common_attr = demod->my_common_attr;
	u16 cmd_result = 0;

	/*
	   STOP demodulator
	   resets IQM, QAM and FEC HW blocks
	 */
	/* stop all comm_exec */
	rc = drxj_dap_write_reg16(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, QAM_COMM_EXEC__A, QAM_COMM_EXEC_STOP, 0);
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
		rc = drxj_dap_write_reg16(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_STOP, 0);
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
		rc = drxj_dap_write_reg16(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	memcpy(&cfg_mpeg_output, &common_attr->mpeg_cfg, sizeof(cfg_mpeg_output));
	cfg_mpeg_output.enable_mpeg_output = false;

	rc = ctrl_set_cfg_mpeg_output(demod, &cfg_mpeg_output);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/

/*
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
	struct drxj_data *ext_attr = NULL;	/* Global data container for DRXJ specific data */
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

	rc = drxj_dap_write_reg16(dev_addr, FEC_OC_SNC_FAIL_PERIOD__A, (u16)fec_oc_snc_fail_period, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, FEC_RS_MEASUREMENT_PERIOD__A, (u16)fec_rs_period, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, FEC_RS_MEASUREMENT_PRESCALE__A, fec_rs_prescale, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->fec_rs_period = (u16) fec_rs_period;
	ext_attr->fec_rs_prescale = fec_rs_prescale;
	rc = drxdap_fasi_write_reg32(dev_addr, SCU_RAM_FEC_ACCUM_CW_CORRECTED_LO__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_FEC_MEAS_COUNT__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, 0, 0);
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

		rc = drxj_dap_write_reg16(dev_addr, QAM_VD_MEASUREMENT_PERIOD__A, (u16)qam_vd_period, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_VD_MEASUREMENT_PRESCALE__A, qam_vd_prescale, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		ext_attr->qam_vd_period = (u16) qam_vd_period;
		ext_attr->qam_vd_prescale = qam_vd_prescale;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/

/*
* \fn int set_qam16 ()
* \brief QAM16 specific setup
* \param demod instance of demod.
* \return int.
*/
static int set_qam16(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	static const u8 qam_dq_qual_fun[] = {
		DRXJ_16TO8(2),	/* fun0  */
		DRXJ_16TO8(2),	/* fun1  */
		DRXJ_16TO8(2),	/* fun2  */
		DRXJ_16TO8(2),	/* fun3  */
		DRXJ_16TO8(3),	/* fun4  */
		DRXJ_16TO8(3),	/* fun5  */
	};
	static const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(13517),	/* RAD0  */
		DRXJ_16TO8(13517),	/* RAD1  */
		DRXJ_16TO8(13517),	/* RAD2  */
		DRXJ_16TO8(13517),	/* RAD3  */
		DRXJ_16TO8(13517),	/* RAD4  */
		DRXJ_16TO8(13517),	/* RAD5  */
	};

	rc = drxdap_fasi_write_block(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxdap_fasi_write_block(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 140, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 120, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 230, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 95, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 105, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 56, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 220, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 25, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 6, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16)(-24), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16)(-65), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-127), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 10, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 240, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 40960, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/

/*
* \fn int set_qam32 ()
* \brief QAM32 specific setup
* \param demod instance of demod.
* \return int.
*/
static int set_qam32(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	static const u8 qam_dq_qual_fun[] = {
		DRXJ_16TO8(3),	/* fun0  */
		DRXJ_16TO8(3),	/* fun1  */
		DRXJ_16TO8(3),	/* fun2  */
		DRXJ_16TO8(3),	/* fun3  */
		DRXJ_16TO8(4),	/* fun4  */
		DRXJ_16TO8(4),	/* fun5  */
	};
	static const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(6707),	/* RAD0  */
		DRXJ_16TO8(6707),	/* RAD1  */
		DRXJ_16TO8(6707),	/* RAD2  */
		DRXJ_16TO8(6707),	/* RAD3  */
		DRXJ_16TO8(6707),	/* RAD4  */
		DRXJ_16TO8(6707),	/* RAD5  */
	};

	rc = drxdap_fasi_write_block(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxdap_fasi_write_block(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 90, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 170, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 56, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 140, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, (u16)(-16), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16)(-26), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16)(-56), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-86), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 10, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 176, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 20480, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/

/*
* \fn int set_qam64 ()
* \brief QAM64 specific setup
* \param demod instance of demod.
* \return int.
*/
static int set_qam64(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	static const u8 qam_dq_qual_fun[] = {
		/* this is hw reset value. no necessary to re-write */
		DRXJ_16TO8(4),	/* fun0  */
		DRXJ_16TO8(4),	/* fun1  */
		DRXJ_16TO8(4),	/* fun2  */
		DRXJ_16TO8(4),	/* fun3  */
		DRXJ_16TO8(6),	/* fun4  */
		DRXJ_16TO8(6),	/* fun5  */
	};
	static const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(13336),	/* RAD0  */
		DRXJ_16TO8(12618),	/* RAD1  */
		DRXJ_16TO8(11988),	/* RAD2  */
		DRXJ_16TO8(13809),	/* RAD3  */
		DRXJ_16TO8(13809),	/* RAD4  */
		DRXJ_16TO8(15609),	/* RAD5  */
	};

	rc = drxdap_fasi_write_block(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxdap_fasi_write_block(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 105, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 60, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 195, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 84, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 141, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 7, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16)(-15), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16)(-45), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-80), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 30, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 48, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 160, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 43008, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/

/*
* \fn int set_qam128 ()
* \brief QAM128 specific setup
* \param demod: instance of demod.
* \return int.
*/
static int set_qam128(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	static const u8 qam_dq_qual_fun[] = {
		DRXJ_16TO8(6),	/* fun0  */
		DRXJ_16TO8(6),	/* fun1  */
		DRXJ_16TO8(6),	/* fun2  */
		DRXJ_16TO8(6),	/* fun3  */
		DRXJ_16TO8(9),	/* fun4  */
		DRXJ_16TO8(9),	/* fun5  */
	};
	static const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(6164),	/* RAD0  */
		DRXJ_16TO8(6598),	/* RAD1  */
		DRXJ_16TO8(6394),	/* RAD2  */
		DRXJ_16TO8(6409),	/* RAD3  */
		DRXJ_16TO8(6656),	/* RAD4  */
		DRXJ_16TO8(7238),	/* RAD5  */
	};

	rc = drxdap_fasi_write_block(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxdap_fasi_write_block(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 60, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 140, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 65, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16)(-1), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-23), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 32, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 144, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 20992, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/

/*
* \fn int set_qam256 ()
* \brief QAM256 specific setup
* \param demod: instance of demod.
* \return int.
*/
static int set_qam256(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	static const u8 qam_dq_qual_fun[] = {
		DRXJ_16TO8(8),	/* fun0  */
		DRXJ_16TO8(8),	/* fun1  */
		DRXJ_16TO8(8),	/* fun2  */
		DRXJ_16TO8(8),	/* fun3  */
		DRXJ_16TO8(12),	/* fun4  */
		DRXJ_16TO8(12),	/* fun5  */
	};
	static const u8 qam_eq_cma_rad[] = {
		DRXJ_16TO8(12345),	/* RAD0  */
		DRXJ_16TO8(12345),	/* RAD1  */
		DRXJ_16TO8(13626),	/* RAD2  */
		DRXJ_16TO8(12931),	/* RAD3  */
		DRXJ_16TO8(14719),	/* RAD4  */
		DRXJ_16TO8(15356),	/* RAD5  */
	};

	rc = drxdap_fasi_write_block(dev_addr, QAM_DQ_QUAL_FUN0__A, sizeof(qam_dq_qual_fun), ((u8 *)qam_dq_qual_fun), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxdap_fasi_write_block(dev_addr, SCU_RAM_QAM_EQ_CMA_RAD0__A, sizeof(qam_eq_cma_rad), ((u8 *)qam_eq_cma_rad), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RTH__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FTH__A, 60, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_PTH__A, 100, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_QTH__A, 150, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_CTH__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MTH__A, 110, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RATE_LIM__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_FREQ_LIM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_COUNT_LIM__A, 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, 74, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, 18, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, 13, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, 7, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_FINE__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CA_COARSE__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_MEDIUM__A, 50, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CP_COARSE__A, 255, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_FINE__A, 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_MEDIUM__A, 25, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CI_COARSE__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EP_COARSE__A, 24, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_FINE__A, 12, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_EI_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_FINE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_MEDIUM__A, 48, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF_COARSE__A, 80, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_FINE__A, 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 15, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_LC_CF1_COARSE__A, 16, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_SL_SIG_POWER__A, 43520, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/
#define QAM_SET_OP_ALL 0x1
#define QAM_SET_OP_CONSTELLATION 0x2
#define QAM_SET_OP_SPECTRUM 0X4

/*
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
	static const u8 qam_a_taps[] = {
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
	static const u8 qam_b64_taps[] = {
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
	static const u8 qam_b256_taps[] = {
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
	static const u8 qam_c_taps[] = {
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
		rc = drxj_dap_write_reg16(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_COMM_EXEC__A, QAM_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
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
		rc = drxdap_fasi_write_reg32(dev_addr, IQM_RC_RATE_OFS_LO__A, iqm_rc_rate, 0);
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

		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_SYMBOL_FREQ__A, lc_symbol_freq, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RC_STRETCH__A, iqm_rc_stretch, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	if (op & QAM_SET_OP_ALL) {
		if (!ext_attr->has_lna) {
			rc = drxj_dap_write_reg16(dev_addr, IQM_AF_AMUX__A, 0x02, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_SYMMETRIC__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_MIDTAP__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_OUT_ENA__A, IQM_CF_OUT_ENA_QAM__M, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_WR_RSV_0__A, 0x5f, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/* scu temporary shut down agc */

		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_SYNC_SEL__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_CLP_LEN__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_CLP_TH__A, 448, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_SNS_LEN__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_PDREF__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_STDBY__A, 0x10, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_PGA_GAIN__A, 11, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_POW_MEAS_LEN__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_SCALE_SH__A, IQM_CF_SCALE_SH__PRE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/*! reset default val ! */

		rc = drxj_dap_write_reg16(dev_addr, QAM_SY_TIMEOUT__A, QAM_SY_TIMEOUT__PRE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/*! reset default val ! */
		if (ext_attr->standard == DRX_STANDARD_ITU_B) {
			rc = drxj_dap_write_reg16(dev_addr, QAM_SY_SYNC_LWM__A, QAM_SY_SYNC_LWM__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/*! reset default val ! */
			rc = drxj_dap_write_reg16(dev_addr, QAM_SY_SYNC_AWM__A, QAM_SY_SYNC_AWM__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/*! reset default val ! */
			rc = drxj_dap_write_reg16(dev_addr, QAM_SY_SYNC_HWM__A, QAM_SY_SYNC_HWM__PRE, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/*! reset default val ! */
		} else {
			switch (channel->constellation) {
			case DRX_CONSTELLATION_QAM16:
			case DRX_CONSTELLATION_QAM64:
			case DRX_CONSTELLATION_QAM256:
				rc = drxj_dap_write_reg16(dev_addr, QAM_SY_SYNC_LWM__A, 0x03, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = drxj_dap_write_reg16(dev_addr, QAM_SY_SYNC_AWM__A, 0x04, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = drxj_dap_write_reg16(dev_addr, QAM_SY_SYNC_HWM__A, QAM_SY_SYNC_HWM__PRE, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}	/*! reset default val ! */
				break;
			case DRX_CONSTELLATION_QAM32:
			case DRX_CONSTELLATION_QAM128:
				rc = drxj_dap_write_reg16(dev_addr, QAM_SY_SYNC_LWM__A, 0x03, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = drxj_dap_write_reg16(dev_addr, QAM_SY_SYNC_AWM__A, 0x05, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = drxj_dap_write_reg16(dev_addr, QAM_SY_SYNC_HWM__A, 0x06, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				break;
			default:
				return -EIO;
			}	/* switch */
		}

		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_MODE__A, QAM_LC_MODE__PRE, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}	/*! reset default val ! */
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_RATE_LIMIT__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_LPF_FACTORP__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_LPF_FACTORI__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_MODE__A, 7, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB0__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB1__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB2__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB3__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB4__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB5__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB6__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB8__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB9__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB10__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB12__A, 2, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB15__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB16__A, 3, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB20__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_LC_QUAL_TAB25__A, 4, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		rc = drxj_dap_write_reg16(dev_addr, IQM_FS_ADJ_SEL__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RC_ADJ_SEL__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_ADJ_SEL__A, 1, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_POW_MEAS_LEN__A, 0, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_GPIO__A, 0, 0);
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
			rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_RE0__A, sizeof(qam_a_taps), ((u8 *)qam_a_taps), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_IM0__A, sizeof(qam_a_taps), ((u8 *)qam_a_taps), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
		} else if (ext_attr->standard == DRX_STANDARD_ITU_B) {
			switch (channel->constellation) {
			case DRX_CONSTELLATION_QAM64:
				rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_RE0__A, sizeof(qam_b64_taps), ((u8 *)qam_b64_taps), 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_IM0__A, sizeof(qam_b64_taps), ((u8 *)qam_b64_taps), 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				break;
			case DRX_CONSTELLATION_QAM256:
				rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_RE0__A, sizeof(qam_b256_taps), ((u8 *)qam_b256_taps), 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_IM0__A, sizeof(qam_b256_taps), ((u8 *)qam_b256_taps), 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				break;
			default:
				return -EIO;
			}
		} else if (ext_attr->standard == DRX_STANDARD_ITU_C) {
			rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_RE0__A, sizeof(qam_c_taps), ((u8 *)qam_c_taps), 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxdap_fasi_write_block(dev_addr, IQM_CF_TAP_IM0__A, sizeof(qam_c_taps), ((u8 *)qam_c_taps), 0);
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
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_SCALE_SH__A, 0, 0);
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

			memcpy(&cfg_mpeg_output, &common_attr->mpeg_cfg, sizeof(cfg_mpeg_output));
			cfg_mpeg_output.enable_mpeg_output = true;

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

	rc = drxj_dap_write_reg16(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, QAM_COMM_EXEC__A, QAM_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, FEC_COMM_EXEC__A, FEC_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/
static int ctrl_get_qam_sig_quality(struct drx_demod_instance *demod);

static int qam_flip_spec(struct drx_demod_instance *demod, struct drx_channel *channel)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drxj_data *ext_attr = demod->my_ext_attr;
	int rc;
	u32 iqm_fs_rate_ofs = 0;
	u32 iqm_fs_rate_lo = 0;
	u16 qam_ctl_ena = 0;
	u16 data = 0;
	u16 equ_mode = 0;
	u16 fsm_state = 0;
	int i = 0;
	int ofsofs = 0;

	/* Silence the controlling of lc, equ, and the acquisition state machine */
	rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_QAM_CTL_ENA__A, &qam_ctl_ena, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_CTL_ENA__A, qam_ctl_ena & ~(SCU_RAM_QAM_CTL_ENA_ACQ__M | SCU_RAM_QAM_CTL_ENA_EQU__M | SCU_RAM_QAM_CTL_ENA_LC__M), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* freeze the frequency control loop */
	rc = drxj_dap_write_reg16(dev_addr, QAM_LC_CF__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, QAM_LC_CF1__A, 0, 0);
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
	rc = drxj_dap_read_reg16(dev_addr, QAM_DQ_MODE__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	data = (data & 0xfff9);
	rc = drxj_dap_write_reg16(dev_addr, QAM_DQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, QAM_FQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* lc_cp / _ci / _ca */
	rc = drxj_dap_write_reg16(dev_addr, QAM_LC_CI__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, QAM_LC_EP__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, QAM_FQ_LA_FACTOR__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* flip the spec */
	rc = drxdap_fasi_write_reg32(dev_addr, IQM_FS_RATE_OFS_LO__A, iqm_fs_rate_ofs, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	ext_attr->iqm_fs_rate_ofs = iqm_fs_rate_ofs;
	ext_attr->pos_image = (ext_attr->pos_image) ? false : true;

	/* freeze dq/fq updating */
	rc = drxj_dap_read_reg16(dev_addr, QAM_DQ_MODE__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	equ_mode = data;
	data = (data & 0xfff9);
	rc = drxj_dap_write_reg16(dev_addr, QAM_DQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, QAM_FQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	for (i = 0; i < 28; i++) {
		rc = drxj_dap_read_reg16(dev_addr, QAM_DQ_TAP_IM_EL0__A + (2 * i), &data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_DQ_TAP_IM_EL0__A + (2 * i), -data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	for (i = 0; i < 24; i++) {
		rc = drxj_dap_read_reg16(dev_addr, QAM_FQ_TAP_IM_EL0__A + (2 * i), &data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, QAM_FQ_TAP_IM_EL0__A + (2 * i), -data, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}

	data = equ_mode;
	rc = drxj_dap_write_reg16(dev_addr, QAM_DQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, QAM_FQ_MODE__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_FSM_STATE_TGT__A, 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	i = 0;
	while ((fsm_state != 4) && (i++ < 100)) {
		rc = drxj_dap_read_reg16(dev_addr, SCU_RAM_QAM_FSM_STATE__A, &fsm_state, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_QAM_CTL_ENA__A, (qam_ctl_ena | 0x0016), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;

}

#define  NO_LOCK        0x0
#define  DEMOD_LOCKED   0x1
#define  SYNC_FLIPPED   0x2
#define  SPEC_MIRRORED  0x4
/*
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
	struct drxj_data *ext_attr = demod->my_ext_attr;
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drx39xxj_state *state = dev_addr->user_data;
	struct dtv_frontend_properties *p = &state->frontend.dtv_property_cache;
	int rc;
	u32 lck_state = NO_LOCK;
	u32 start_time = 0;
	u32 d_locked_time = 0;
	u32 timeout_ofs = 0;
	u16 data = 0;

	/* external attributes for storing acquired channel constellation */
	*lock_status = DRX_NOT_LOCKED;
	start_time = jiffies_to_msecs(jiffies);
	lck_state = NO_LOCK;
	do {
		rc = ctrl_lock_status(demod, lock_status);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		switch (lck_state) {
		case NO_LOCK:
			if (*lock_status == DRXJ_DEMOD_LOCK) {
				rc = ctrl_get_qam_sig_quality(demod);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				if (p->cnr.stat[0].svalue > 20800) {
					lck_state = DEMOD_LOCKED;
					/* some delay to see if fec_lock possible TODO find the right value */
					timeout_ofs += DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME;	/* see something, waiting longer */
					d_locked_time = jiffies_to_msecs(jiffies);
				}
			}
			break;
		case DEMOD_LOCKED:
			if ((*lock_status == DRXJ_DEMOD_LOCK) &&	/* still demod_lock in 150ms */
			    ((jiffies_to_msecs(jiffies) - d_locked_time) >
			     DRXJ_QAM_FEC_LOCK_WAITTIME)) {
				rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, &data, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, data | 0x1, 0);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				lck_state = SYNC_FLIPPED;
				msleep(10);
			}
			break;
		case SYNC_FLIPPED:
			if (*lock_status == DRXJ_DEMOD_LOCK) {
				if (channel->mirror == DRX_MIRROR_AUTO) {
					/* flip sync pattern back */
					rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, &data, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, data & 0xFFFE, 0);
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
					lck_state = SPEC_MIRRORED;
					/* reset timer TODO: still need 500ms? */
					start_time = d_locked_time =
					    jiffies_to_msecs(jiffies);
					timeout_ofs = 0;
				} else {	/* no need to wait lock */

					start_time =
					    jiffies_to_msecs(jiffies) -
					    DRXJ_QAM_MAX_WAITTIME - timeout_ofs;
				}
			}
			break;
		case SPEC_MIRRORED:
			if ((*lock_status == DRXJ_DEMOD_LOCK) &&	/* still demod_lock in 150ms */
			    ((jiffies_to_msecs(jiffies) - d_locked_time) >
			     DRXJ_QAM_FEC_LOCK_WAITTIME)) {
				rc = ctrl_get_qam_sig_quality(demod);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				if (p->cnr.stat[0].svalue > 20800) {
					rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, &data, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr, QAM_SY_TIMEOUT__A, data | 0x1, 0);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					/* no need to wait lock */
					start_time =
					    jiffies_to_msecs(jiffies) -
					    DRXJ_QAM_MAX_WAITTIME - timeout_ofs;
				}
			}
			break;
		default:
			break;
		}
		msleep(10);
	} while
	    ((*lock_status != DRX_LOCKED) &&
	     (*lock_status != DRX_NEVER_LOCK) &&
	     ((jiffies_to_msecs(jiffies) - start_time) <
	      (DRXJ_QAM_MAX_WAITTIME + timeout_ofs))
	    );
	/* Returning control to application ... */

	return 0;
rw_error:
	return rc;
}

/*
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
	struct drxj_data *ext_attr = demod->my_ext_attr;
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drx39xxj_state *state = dev_addr->user_data;
	struct dtv_frontend_properties *p = &state->frontend.dtv_property_cache;
	int rc;
	u32 lck_state = NO_LOCK;
	u32 start_time = 0;
	u32 d_locked_time = 0;
	u32 timeout_ofs = DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME;

	/* external attributes for storing acquired channel constellation */
	*lock_status = DRX_NOT_LOCKED;
	start_time = jiffies_to_msecs(jiffies);
	lck_state = NO_LOCK;
	do {
		rc = ctrl_lock_status(demod, lock_status);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		switch (lck_state) {
		case NO_LOCK:
			if (*lock_status == DRXJ_DEMOD_LOCK) {
				rc = ctrl_get_qam_sig_quality(demod);
				if (rc != 0) {
					pr_err("error %d\n", rc);
					goto rw_error;
				}
				if (p->cnr.stat[0].svalue > 26800) {
					lck_state = DEMOD_LOCKED;
					timeout_ofs += DRXJ_QAM_DEMOD_LOCK_EXT_WAITTIME;	/* see something, wait longer */
					d_locked_time = jiffies_to_msecs(jiffies);
				}
			}
			break;
		case DEMOD_LOCKED:
			if (*lock_status == DRXJ_DEMOD_LOCK) {
				if ((channel->mirror == DRX_MIRROR_AUTO) &&
				    ((jiffies_to_msecs(jiffies) - d_locked_time) >
				     DRXJ_QAM_FEC_LOCK_WAITTIME)) {
					ext_attr->mirror = DRX_MIRROR_YES;
					rc = qam_flip_spec(demod, channel);
					if (rc != 0) {
						pr_err("error %d\n", rc);
						goto rw_error;
					}
					lck_state = SPEC_MIRRORED;
					/* reset timer TODO: still need 300ms? */
					start_time = jiffies_to_msecs(jiffies);
					timeout_ofs = -DRXJ_QAM_MAX_WAITTIME / 2;
				}
			}
			break;
		case SPEC_MIRRORED:
			break;
		default:
			break;
		}
		msleep(10);
	} while
	    ((*lock_status < DRX_LOCKED) &&
	     (*lock_status != DRX_NEVER_LOCK) &&
	     ((jiffies_to_msecs(jiffies) - start_time) <
	      (DRXJ_QAM_MAX_WAITTIME + timeout_ofs)));

	return 0;
rw_error:
	return rc;
}

/*
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

	/* external attributes for storing acquired channel constellation */
	ext_attr = (struct drxj_data *) demod->my_ext_attr;

	/* set QAM channel constellation */
	switch (channel->constellation) {
	case DRX_CONSTELLATION_QAM16:
	case DRX_CONSTELLATION_QAM32:
	case DRX_CONSTELLATION_QAM128:
		return -EINVAL;
	case DRX_CONSTELLATION_QAM64:
	case DRX_CONSTELLATION_QAM256:
		if (ext_attr->standard != DRX_STANDARD_ITU_B)
			return -EINVAL;

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

		if (channel->constellation == DRX_CONSTELLATION_QAM64)
			rc = qam64auto(demod, channel, tuner_freq_offset,
				       &lock_status);
		else
			rc = qam256auto(demod, channel, tuner_freq_offset,
					&lock_status);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		break;
	case DRX_CONSTELLATION_AUTO:	/* for channel scan */
		if (ext_attr->standard == DRX_STANDARD_ITU_B) {
			u16 qam_ctl_ena = 0;

			auto_flag = true;

			/* try to lock default QAM constellation: QAM256 */
			channel->constellation = DRX_CONSTELLATION_QAM256;
			ext_attr->constellation = DRX_CONSTELLATION_QAM256;
			if (channel->mirror == DRX_MIRROR_AUTO)
				ext_attr->mirror = DRX_MIRROR_NO;
			else
				ext_attr->mirror = channel->mirror;
			rc = set_qam(demod, channel, tuner_freq_offset,
				     QAM_SET_OP_ALL);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = qam256auto(demod, channel, tuner_freq_offset,
					&lock_status);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			if (lock_status >= DRX_LOCKED) {
				channel->constellation = DRX_CONSTELLATION_AUTO;
				break;
			}

			/* QAM254 not locked. Try QAM64 constellation */
			channel->constellation = DRX_CONSTELLATION_QAM64;
			ext_attr->constellation = DRX_CONSTELLATION_QAM64;
			if (channel->mirror == DRX_MIRROR_AUTO)
				ext_attr->mirror = DRX_MIRROR_NO;
			else
				ext_attr->mirror = channel->mirror;

			rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr,
						     SCU_RAM_QAM_CTL_ENA__A,
						     &qam_ctl_ena, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr,
						      SCU_RAM_QAM_CTL_ENA__A,
						      qam_ctl_ena & ~SCU_RAM_QAM_CTL_ENA_ACQ__M, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr,
						      SCU_RAM_QAM_FSM_STATE_TGT__A,
						      0x2, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* force to rate hunting */

			rc = set_qam(demod, channel, tuner_freq_offset,
				     QAM_SET_OP_CONSTELLATION);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr,
						      SCU_RAM_QAM_CTL_ENA__A,
						      qam_ctl_ena, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			rc = qam64auto(demod, channel, tuner_freq_offset,
				       &lock_status);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}

			channel->constellation = DRX_CONSTELLATION_AUTO;
		} else if (ext_attr->standard == DRX_STANDARD_ITU_C) {
			u16 qam_ctl_ena = 0;

			channel->constellation = DRX_CONSTELLATION_QAM64;
			ext_attr->constellation = DRX_CONSTELLATION_QAM64;
			auto_flag = true;

			if (channel->mirror == DRX_MIRROR_AUTO)
				ext_attr->mirror = DRX_MIRROR_NO;
			else
				ext_attr->mirror = channel->mirror;
			rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr,
						     SCU_RAM_QAM_CTL_ENA__A,
						     &qam_ctl_ena, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr,
						      SCU_RAM_QAM_CTL_ENA__A,
						      qam_ctl_ena & ~SCU_RAM_QAM_CTL_ENA_ACQ__M, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr,
						      SCU_RAM_QAM_FSM_STATE_TGT__A,
						      0x2, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}	/* force to rate hunting */

			rc = set_qam(demod, channel, tuner_freq_offset,
				     QAM_SET_OP_CONSTELLATION);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = drxj_dap_write_reg16(demod->my_i2c_dev_addr,
						      SCU_RAM_QAM_CTL_ENA__A,
						      qam_ctl_ena, 0);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			rc = qam64auto(demod, channel, tuner_freq_offset,
				       &lock_status);
			if (rc != 0) {
				pr_err("error %d\n", rc);
				goto rw_error;
			}
			channel->constellation = DRX_CONSTELLATION_AUTO;
		} else {
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
	return rc;
}

/*============================================================================*/

/*
* \fn static short get_qamrs_err_count(struct i2c_device_addr *dev_addr)
* \brief Get RS error count in QAM mode (used for post RS BER calculation)
* \return Error code
*
* precondition: measurement period & measurement prescale must be set
*
*/
static int
get_qamrs_err_count(struct i2c_device_addr *dev_addr,
		    struct drxjrs_errors *rs_errors)
{
	int rc;
	u16 nr_bit_errors = 0,
	    nr_symbol_errors = 0,
	    nr_packet_errors = 0, nr_failures = 0, nr_snc_par_fail_count = 0;

	/* check arguments */
	if (dev_addr == NULL)
		return -EINVAL;

	/* all reported errors are received in the  */
	/* most recently finished measurement period */
	/*   no of pre RS bit errors */
	rc = drxj_dap_read_reg16(dev_addr, FEC_RS_NR_BIT_ERRORS__A, &nr_bit_errors, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/*   no of symbol errors      */
	rc = drxj_dap_read_reg16(dev_addr, FEC_RS_NR_SYMBOL_ERRORS__A, &nr_symbol_errors, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/*   no of packet errors      */
	rc = drxj_dap_read_reg16(dev_addr, FEC_RS_NR_PACKET_ERRORS__A, &nr_packet_errors, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/*   no of failures to decode */
	rc = drxj_dap_read_reg16(dev_addr, FEC_RS_NR_FAILURES__A, &nr_failures, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/*   no of post RS bit erros  */
	rc = drxj_dap_read_reg16(dev_addr, FEC_OC_SNC_FAIL_COUNT__A, &nr_snc_par_fail_count, 0);
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
	return rc;
}

/*============================================================================*/

/*
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

	rc = drxj_dap_read_reg16(dev_addr, IQM_AF_AGC_IF__A, &if_gain, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if_gain &= IQM_AF_AGC_IF__M;
	rc = drxj_dap_read_reg16(dev_addr, IQM_AF_AGC_RF__A, &rf_gain, 0);
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

	if (*sig_strength <= 7)
		*sig_strength = 0;

	return 0;
rw_error:
	return rc;
}

/*
* \fn int ctrl_get_qam_sig_quality()
* \brief Retrieve QAM signal quality from device.
* \param devmod Pointer to demodulator instance.
* \param sig_quality Pointer to signal quality data.
* \return int.
* \retval 0 sig_quality contains valid data.
* \retval -EINVAL sig_quality is NULL.
* \retval -EIO Erroneous data, sig_quality contains invalid data.

*  Pre-condition: Device must be started and in lock.
*/
static int
ctrl_get_qam_sig_quality(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drxj_data *ext_attr = demod->my_ext_attr;
	struct drx39xxj_state *state = dev_addr->user_data;
	struct dtv_frontend_properties *p = &state->frontend.dtv_property_cache;
	struct drxjrs_errors measuredrs_errors = { 0, 0, 0, 0, 0 };
	enum drx_modulation constellation = ext_attr->constellation;
	int rc;

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

	p->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	/* read the physical registers */
	/*   Get the RS error data */
	rc = get_qamrs_err_count(dev_addr, &measuredrs_errors);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* get the register value needed for MER */
	rc = drxj_dap_read_reg16(dev_addr, QAM_SL_ERR_POWER__A, &qam_sl_err_power, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/* get the register value needed for post RS BER */
	rc = drxj_dap_read_reg16(dev_addr, FEC_OC_SNC_FAIL_PERIOD__A, &fec_oc_period, 0);
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
	/* pre viterbi SER is good if it is below 0.025 */

	/* get the register value */
	/*   no of quadrature symbol errors */
	rc = drxj_dap_read_reg16(dev_addr, QAM_VD_NR_QSYM_ERRORS__A, &qsym_err_vd, 0);
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
		qam_vd_ser = 500000 * vd_bit_cnt * ((e > 2) ? 1 : 8) / 8;
	else
		qam_vd_ser = m << ((e > 2) ? (e - 3) : e);

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
		qam_pre_rs_ber = 500000 * rs_bit_cnt >> e;
	else
		qam_pre_rs_ber = ber_cnt;

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
	if (!fec_oc_period) {
		qam_post_rs_ber = 0xFFFFFFFF;
	} else {
		e = post_bit_err_rs * 742686;
		m = fec_oc_period * 100;
		qam_post_rs_ber = e / m;
	}

	/* fill signal quality data structure */
	p->pre_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	p->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	p->pre_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	p->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	p->block_error.stat[0].scale = FE_SCALE_COUNTER;
	p->cnr.stat[0].scale = FE_SCALE_DECIBEL;

	p->cnr.stat[0].svalue = ((u16) qam_sl_mer) * 100;
	if (ext_attr->standard == DRX_STANDARD_ITU_B) {
		p->pre_bit_error.stat[0].uvalue += qam_vd_ser;
		p->pre_bit_count.stat[0].uvalue += vd_bit_cnt * ((e > 2) ? 1 : 8) / 8;
	} else {
		p->pre_bit_error.stat[0].uvalue += qam_pre_rs_ber;
		p->pre_bit_count.stat[0].uvalue += rs_bit_cnt >> e;
	}

	p->post_bit_error.stat[0].uvalue += qam_post_rs_ber;
	p->post_bit_count.stat[0].uvalue += rs_bit_cnt >> e;

	p->block_error.stat[0].uvalue += pkt_errs;

#ifdef DRXJ_SIGNAL_ACCUM_ERR
	rc = get_acc_pkt_err(demod, &sig_quality->packet_error);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
#endif

	return 0;
rw_error:
	p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	return rc;
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
      The SIF AGC is needed for both SIF output and the internal SIF signal to
      the AUD block.

      RF and IF AGCs DACs are part of AFE, Video and SIF AGC DACs are part of
      the ATV block. The AGC control algorithms are all implemented in
      microcode.

   ATV SETTINGS

      (Shadow settings will not be used for now, they will be implemented
       later on because of the schedule)

      Several HW/SCU "settings" can be used for ATV. The standard selection
      will reset most of these settings. To avoid that the end user application
      has to perform these settings each time the ATV or FM standards is
      selected the driver will shadow these settings. This enables the end user
      to perform the settings only once after a drx_open(). The driver must
      write the shadow settings to HW/SCU in case:
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

/*
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
	rc = drxj_dap_write_reg16(dev_addr, ATV_TOP_STDBY__A, (ATV_TOP_STDBY_SIF_STDBY_STANDBY & (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE)), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, ATV_COMM_EXEC__A, ATV_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (primary) {
		rc = drxj_dap_write_reg16(dev_addr, IQM_COMM_EXEC__A, IQM_COMM_EXEC_STOP, 0);
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
		rc = drxj_dap_write_reg16(dev_addr, IQM_FS_COMM_EXEC__A, IQM_FS_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_FD_COMM_EXEC__A, IQM_FD_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RC_COMM_EXEC__A, IQM_RC_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_RT_COMM_EXEC__A, IQM_RT_COMM_EXEC_STOP, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, IQM_CF_COMM_EXEC__A, IQM_CF_COMM_EXEC_STOP, 0);
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
	return rc;
}

/*============================================================================*/

/*
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

	rc = drxj_dap_write_reg16(dev_addr, AUD_COMM_EXEC__A, AUD_COMM_EXEC_STOP, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	ext_attr->aud_data.audio_is_active = false;

	return 0;
rw_error:
	return rc;
}

/*
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
	rc = drxj_dap_read_reg16(dev_addr, ORX_NSU_AOX_STDBY_W__A, &data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (!active)
		data &= ((~ORX_NSU_AOX_STDBY_W_STDBYADC_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYAMP_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYBIAS_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYPLL_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYPD_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYTAGC_IF_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYTAGC_RF_A2_ON) & (~ORX_NSU_AOX_STDBY_W_STDBYFLT_A2_ON));
	else
		data |= (ORX_NSU_AOX_STDBY_W_STDBYADC_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYAMP_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYBIAS_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYPLL_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYPD_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYTAGC_IF_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYTAGC_RF_A2_ON | ORX_NSU_AOX_STDBY_W_STDBYFLT_A2_ON);
	rc = drxj_dap_write_reg16(dev_addr, ORX_NSU_AOX_STDBY_W__A, data, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	return 0;
rw_error:
	return rc;
}

/*
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

/* Coefficients for the nyquist filter (total: 27 taps) */
#define NYQFILTERLEN 27

static int ctrl_set_oob(struct drx_demod_instance *demod, struct drxoob *oob_param)
{
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
		rc = drxj_dap_write_reg16(dev_addr, ORX_COMM_EXEC__A, ORX_COMM_EXEC_STOP, 0);
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

   /********/
	/* Stop  */
   /********/
	rc = drxj_dap_write_reg16(dev_addr, ORX_COMM_EXEC__A, ORX_COMM_EXEC_STOP, 0);
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
   /********/
	/* Reset */
   /********/
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
   /**********/
	/* SET_ENV */
   /**********/
	/* set frequency, spectrum inversion and data rate */
	scu_cmd.command = SCU_RAM_COMMAND_STANDARD_OOB
	    | SCU_RAM_COMMAND_CMD_DEMOD_SET_ENV;
	scu_cmd.parameter_len = 3;
	/* 1-data rate;2-frequency */
	switch (oob_param->standard) {
	case DRX_OOB_MODE_A:
		if (
			   /* signal is transmitted inverted */
			   ((oob_param->spectrum_inverted == true) &&
			    /* and tuner is not mirroring the signal */
			    (!mirror_freq_spect_oob)) |
			   /* or */
			   /* signal is transmitted noninverted */
			   ((oob_param->spectrum_inverted == false) &&
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
			   ((oob_param->spectrum_inverted == true) &&
			    /* and tuner is not mirroring the signal */
			    (!mirror_freq_spect_oob)) |
			   /* or */
			   /* signal is transmitted noninverted */
			   ((oob_param->spectrum_inverted == false) &&
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
			   ((oob_param->spectrum_inverted == true) &&
			    /* and tuner is not mirroring the signal */
			    (!mirror_freq_spect_oob)) |
			   /* or */
			   /* signal is transmitted noninverted */
			   ((oob_param->spectrum_inverted == false) &&
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

	rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, 0xFABA, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/*  Write magic word to enable pdr reg write  */
	rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_OOB_CRX_CFG__A, OOB_CRX_DRIVE_STRENGTH << SIO_PDR_OOB_CRX_CFG_DRIVE__B | 0x03 << SIO_PDR_OOB_CRX_CFG_MODE__B, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SIO_PDR_OOB_DRX_CFG__A, OOB_DRX_DRIVE_STRENGTH << SIO_PDR_OOB_DRX_CFG_DRIVE__B | 0x03 << SIO_PDR_OOB_DRX_CFG_MODE__B, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SIO_TOP_COMM_KEY__A, 0x0000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}	/*  Write magic word to disable pdr reg write */

	rc = drxj_dap_write_reg16(dev_addr, ORX_TOP_COMM_KEY__A, 0, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, ORX_FWP_AAG_LEN_W__A, 16000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, ORX_FWP_AAG_THR_W__A, 40, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* ddc */
	rc = drxj_dap_write_reg16(dev_addr, ORX_DDC_OFO_SET_W__A, ORX_DDC_OFO_SET_W__PRE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* nsu */
	rc = drxj_dap_write_reg16(dev_addr, ORX_NSU_AOX_LOPOW_W__A, ext_attr->oob_lo_pow, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* initialization for target mode */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_TARGET_MODE__A, SCU_RAM_ORX_TARGET_MODE_2048KBPS_SQRT, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_FREQ_GAIN_CORR__A, SCU_RAM_ORX_FREQ_GAIN_CORR_2048KBPS, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* Reset bits for timing and freq. recovery */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_RST_CPH__A, 0x0001, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_RST_CTI__A, 0x0002, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_RST_KRN__A, 0x0004, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_RST_KRP__A, 0x0008, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* AGN_LOCK = {2048>>3, -2048, 8, -8, 0, 1}; */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_AGN_LOCK_TH__A, 2048 >> 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_AGN_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_AGN_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_AGN_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_AGN_LOCK_MASK__A, 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* DGN_LOCK = {10, -2048, 8, -8, 0, 1<<1}; */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_DGN_LOCK_TH__A, 10, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_DGN_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_DGN_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_DGN_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_DGN_LOCK_MASK__A, 1 << 1, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* FRQ_LOCK = {15,-2048, 8, -8, 0, 1<<2}; */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_FRQ_LOCK_TH__A, 17, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_FRQ_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_FRQ_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_FRQ_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_FRQ_LOCK_MASK__A, 1 << 2, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* PHA_LOCK = {5000, -2048, 8, -8, 0, 1<<3}; */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_PHA_LOCK_TH__A, 3000, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_PHA_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_PHA_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_PHA_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_PHA_LOCK_MASK__A, 1 << 3, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* TIM_LOCK = {300,      -2048, 8, -8, 0, 1<<4}; */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_TIM_LOCK_TH__A, 400, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_TIM_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_TIM_ONLOCK_TTH__A, 8, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_TIM_UNLOCK_TTH__A, (u16)(-8), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_TIM_LOCK_MASK__A, 1 << 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* EQU_LOCK = {20,      -2048, 8, -8, 0, 1<<5}; */
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_EQU_LOCK_TH__A, 20, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_EQU_LOCK_TOTH__A, (u16)(-2048), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_EQU_ONLOCK_TTH__A, 4, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_EQU_UNLOCK_TTH__A, (u16)(-4), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_ORX_EQU_LOCK_MASK__A, 1 << 5, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* PRE-Filter coefficients (PFI) */
	rc = drxdap_fasi_write_block(dev_addr, ORX_FWP_PFI_A_W__A, sizeof(pfi_coeffs[mode_index]), ((u8 *)pfi_coeffs[mode_index]), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, ORX_TOP_MDE_W__A, mode_index, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* NYQUIST-Filter coefficients (NYQ) */
	for (i = 0; i < (NYQFILTERLEN + 1) / 2; i++) {
		rc = drxj_dap_write_reg16(dev_addr, ORX_FWP_NYQ_ADR_W__A, i, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, ORX_FWP_NYQ_COF_RW__A, nyquist_coeffs[mode_index][i], 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
	}
	rc = drxj_dap_write_reg16(dev_addr, ORX_FWP_NYQ_ADR_W__A, 31, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, ORX_COMM_EXEC__A, ORX_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	/********/
	/* Start */
	/********/
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
	rc = drxj_dap_write_reg16(dev_addr, ORX_NSU_AOX_STHR_W__A, ext_attr->oob_pre_saw, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	ext_attr->oob_power_on = true;

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/
/*==                     END OOB DATAPATH FUNCTIONS                         ==*/
/*============================================================================*/

/*=============================================================================
  ===== MC command related functions ==========================================
  ===========================================================================*/

/*=============================================================================
  ===== ctrl_set_channel() ==========================================================
  ===========================================================================*/
/*
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
	s32 tuner_freq_offset = 0;
	struct drxj_data *ext_attr = NULL;
	struct i2c_device_addr *dev_addr = NULL;
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
#ifndef DRXJ_VSB_ONLY
	u32 min_symbol_rate = 0;
	u32 max_symbol_rate = 0;
	int bandwidth_temp = 0;
	int bandwidth = 0;
#endif
   /*== check arguments ======================================================*/
	if ((demod == NULL) || (channel == NULL))
		return -EINVAL;

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
	rc = drxj_dap_write_reg16(dev_addr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	tuner_freq_offset = 0;

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

	/* flag the packet error counter reset */
	ext_attr->reset_pkt_err_acc = true;

	return 0;
rw_error:
	return rc;
}

/*=============================================================================
  ===== SigQuality() ==========================================================
  ===========================================================================*/

/*
* \fn int ctrl_sig_quality()
* \brief Retrieve signal quality form device.
* \param devmod Pointer to demodulator instance.
* \param sig_quality Pointer to signal quality data.
* \return int.
* \retval 0 sig_quality contains valid data.
* \retval -EINVAL sig_quality is NULL.
* \retval -EIO Erroneous data, sig_quality contains invalid data.

*/
static int
ctrl_sig_quality(struct drx_demod_instance *demod,
		 enum drx_lock_status lock_status)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	struct drxj_data *ext_attr = demod->my_ext_attr;
	struct drx39xxj_state *state = dev_addr->user_data;
	struct dtv_frontend_properties *p = &state->frontend.dtv_property_cache;
	enum drx_standard standard = ext_attr->standard;
	int rc;
	u32 ber, cnt, err, pkt;
	u16 mer, strength = 0;

	rc = get_sig_strength(demod, &strength);
	if (rc < 0) {
		pr_err("error getting signal strength %d\n", rc);
		p->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	} else {
		p->strength.stat[0].scale = FE_SCALE_RELATIVE;
		p->strength.stat[0].uvalue = 65535UL *  strength/ 100;
	}

	switch (standard) {
	case DRX_STANDARD_8VSB:
#ifdef DRXJ_SIGNAL_ACCUM_ERR
		rc = get_acc_pkt_err(demod, &pkt);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
#endif
		if (lock_status != DRXJ_DEMOD_LOCK && lock_status != DRX_LOCKED) {
			p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			p->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		} else {
			rc = get_vsb_post_rs_pck_err(dev_addr, &err, &pkt);
			if (rc != 0) {
				pr_err("error %d getting UCB\n", rc);
				p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			} else {
				p->block_error.stat[0].scale = FE_SCALE_COUNTER;
				p->block_error.stat[0].uvalue += err;
				p->block_count.stat[0].scale = FE_SCALE_COUNTER;
				p->block_count.stat[0].uvalue += pkt;
			}

			/* PostViterbi is compute in steps of 10^(-6) */
			rc = get_vs_bpre_viterbi_ber(dev_addr, &ber, &cnt);
			if (rc != 0) {
				pr_err("error %d getting pre-ber\n", rc);
				p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			} else {
				p->pre_bit_error.stat[0].scale = FE_SCALE_COUNTER;
				p->pre_bit_error.stat[0].uvalue += ber;
				p->pre_bit_count.stat[0].scale = FE_SCALE_COUNTER;
				p->pre_bit_count.stat[0].uvalue += cnt;
			}

			rc = get_vs_bpost_viterbi_ber(dev_addr, &ber, &cnt);
			if (rc != 0) {
				pr_err("error %d getting post-ber\n", rc);
				p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			} else {
				p->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
				p->post_bit_error.stat[0].uvalue += ber;
				p->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
				p->post_bit_count.stat[0].uvalue += cnt;
			}
			rc = get_vsbmer(dev_addr, &mer);
			if (rc != 0) {
				pr_err("error %d getting MER\n", rc);
				p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
			} else {
				p->cnr.stat[0].svalue = mer * 100;
				p->cnr.stat[0].scale = FE_SCALE_DECIBEL;
			}
		}
		break;
#ifndef DRXJ_VSB_ONLY
	case DRX_STANDARD_ITU_A:
	case DRX_STANDARD_ITU_B:
	case DRX_STANDARD_ITU_C:
		rc = ctrl_get_qam_sig_quality(demod);
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
	return rc;
}

/*============================================================================*/

/*
* \fn int ctrl_lock_status()
* \brief Retrieve lock status .
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
	case DRX_STANDARD_UNKNOWN:	/* fallthrough */
	default:
		return -EIO;
	}

	/* define the SCU command parameters and execute the command */
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
	return rc;
}

/*============================================================================*/

/*
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
			rc = drxj_dap_read_reg16(demod->my_i2c_dev_addr, SCU_RAM_VERSION_HI__A, &dummy, 0);
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
	default:
		ext_attr->standard = DRX_STANDARD_UNKNOWN;
		return -EINVAL;
		break;
	}

	return 0;
rw_error:
	/* Don't know what the standard is now ... try again */
	ext_attr->standard = DRX_STANDARD_UNKNOWN;
	return rc;
}

/*============================================================================*/

static void drxj_reset_mode(struct drxj_data *ext_attr)
{
	/* Initialize default AFE configuration for QAM */
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
	/* Initialize default AFE configuration for VSB */
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
}

/*
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

	if (*mode == DRX_POWER_UP) {
		/* Restore analog & pin configuration */

		/* Initialize default AFE configuration for VSB */
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
		ext_attr->standard = DRX_STANDARD_UNKNOWN;
	}

	if (*mode != DRXJ_POWER_DOWN_MAIN_PATH) {
		rc = drxj_dap_write_reg16(dev_addr, SIO_CC_PWD_MODE__A, sio_cc_pwd_mode, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}
		rc = drxj_dap_write_reg16(dev_addr, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY, 0);
		if (rc != 0) {
			pr_err("error %d\n", rc);
			goto rw_error;
		}

		if ((*mode != DRX_POWER_UP)) {
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
	return rc;
}

/*============================================================================*/
/*== CTRL Set/Get Config related functions ===================================*/
/*============================================================================*/

/*
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
		rc = drxj_dap_write_reg16(dev_addr, IQM_AF_PDREF__A, pre_saw->reference, 0);
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
	default:
		return -EINVAL;
	}

	return 0;
rw_error:
	return rc;
}

/*============================================================================*/

/*
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
			rc = drxj_dap_write_reg16(dev_addr, IQM_AF_PGA_GAIN__A, gain, 0);
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
	return rc;
}

/*============================================================================*/


/*=============================================================================
===== EXPORTED FUNCTIONS ====================================================*/

static int drx_ctrl_u_code(struct drx_demod_instance *demod,
		       struct drxu_code_info *mc_info,
		       enum drxu_code_action action);
static int drxj_set_lna_state(struct drx_demod_instance *demod, bool state);

/*
* \fn drxj_open()
* \brief Open the demod instance, configure device, configure drxdriver
* \return Status_t Return status.
*
* drxj_open() can be called with a NULL ucode image => no ucode upload.
* This means that drxj_open() must NOT contain SCU commands or, in general,
* rely on SCU or AUD ucode to be present.
*
*/

static int drxj_open(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = NULL;
	struct drxj_data *ext_attr = NULL;
	struct drx_common_attr *common_attr = NULL;
	u32 driver_version = 0;
	struct drxu_code_info ucode_info;
	struct drx_cfg_mpeg_output cfg_mpeg_output;
	int rc;
	enum drx_power_mode power_mode = DRX_POWER_UP;

	if ((demod == NULL) ||
	    (demod->my_common_attr == NULL) ||
	    (demod->my_ext_attr == NULL) ||
	    (demod->my_i2c_dev_addr == NULL) ||
	    (demod->my_common_attr->is_opened)) {
		return -EINVAL;
	}

	/* Check arguments */
	if (demod->my_ext_attr == NULL)
		return -EINVAL;

	dev_addr = demod->my_i2c_dev_addr;
	ext_attr = (struct drxj_data *) demod->my_ext_attr;
	common_attr = (struct drx_common_attr *) demod->my_common_attr;

	rc = ctrl_power_mode(demod, &power_mode);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	if (power_mode != DRX_POWER_UP) {
		rc = -EINVAL;
		pr_err("failed to powerup device\n");
		goto rw_error;
	}

	/* has to be in front of setIqmAf and setOrxNsuAox */
	rc = get_device_capabilities(demod);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/*
	 * Soft reset of sys- and osc-clockdomain
	 *
	 * HACK: On windows, it writes a 0x07 here, instead of just 0x03.
	 * As we didn't load the firmware here yet, we should do the same.
	 * Btw, this is coherent with DRX-K, where we send reset codes
	 * for modulation (OFTM, in DRX-k), SYS and OSC clock domains.
	 */
	rc = drxj_dap_write_reg16(dev_addr, SIO_CC_SOFT_RST__A, (0x04 | SIO_CC_SOFT_RST_SYS__M | SIO_CC_SOFT_RST_OSC__M), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	msleep(1);

	/* TODO first make sure that everything keeps working before enabling this */
	/* PowerDownAnalogBlocks() */
	rc = drxj_dap_write_reg16(dev_addr, ATV_TOP_STDBY__A, (~ATV_TOP_STDBY_CVBS_STDBY_A2_ACTIVE) | ATV_TOP_STDBY_SIF_STDBY_STANDBY, 0);
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
	memcpy(&cfg_mpeg_output, &common_attr->mpeg_cfg, sizeof(cfg_mpeg_output));
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
	rc = drxj_dap_write_reg16(dev_addr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_STOP, 0);
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

		if (DRX_ISPOWERDOWNMODE(demod->my_common_attr->current_power_mode)) {
			pr_err("Should powerup before loading the firmware.");
			return -EINVAL;
		}

		rc = drx_ctrl_u_code(demod, &ucode_info, UCODE_UPLOAD);
		if (rc != 0) {
			pr_err("error %d while uploading the firmware\n", rc);
			goto rw_error;
		}
		if (common_attr->verify_microcode == true) {
			rc = drx_ctrl_u_code(demod, &ucode_info, UCODE_VERIFY);
			if (rc != 0) {
				pr_err("error %d while verifying the firmware\n",
				       rc);
				goto rw_error;
			}
		}
		common_attr->is_opened = false;
	}

	/* Run SCU for a little while to initialize microcode version numbers */
	rc = drxj_dap_write_reg16(dev_addr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE, 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
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
	   Done to enable field application engineers to retrieve drxdriver version
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
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_DRIVER_VER_HI__A, (u16)(driver_version >> 16), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}
	rc = drxj_dap_write_reg16(dev_addr, SCU_RAM_DRIVER_VER_LO__A, (u16)(driver_version & 0xFFFF), 0);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = ctrl_set_oob(demod, NULL);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	/* refresh the audio data structure with default */
	ext_attr->aud_data = drxj_default_aud_data_g;

	demod->my_common_attr->is_opened = true;
	drxj_set_lna_state(demod, false);
	return 0;
rw_error:
	common_attr->is_opened = false;
	return rc;
}

/*============================================================================*/
/*
* \fn drxj_close()
* \brief Close the demod instance, power down the device
* \return Status_t Return status.
*
*/
static int drxj_close(struct drx_demod_instance *demod)
{
	struct i2c_device_addr *dev_addr = demod->my_i2c_dev_addr;
	int rc;
	enum drx_power_mode power_mode = DRX_POWER_UP;

	if ((demod->my_common_attr == NULL) ||
	    (demod->my_ext_attr == NULL) ||
	    (demod->my_i2c_dev_addr == NULL) ||
	    (!demod->my_common_attr->is_opened)) {
		return -EINVAL;
	}

	/* power up */
	rc = ctrl_power_mode(demod, &power_mode);
	if (rc != 0) {
		pr_err("error %d\n", rc);
		goto rw_error;
	}

	rc = drxj_dap_write_reg16(dev_addr, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE, 0);
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

	DRX_ATTR_ISOPENED(demod) = false;

	return 0;
rw_error:
	DRX_ATTR_ISOPENED(demod) = false;

	return rc;
}

/*
 * Microcode related functions
 */

/*
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
		crc_word |= (u32)be16_to_cpu(*(__be16 *)(block_data));
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

/*
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
	u16 mc_nr_of_blks = be16_to_cpu(*(__be16 *)(mc_data + sizeof(u16)));

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
		block_hdr.addr = be32_to_cpu(*(__be32 *)(mc_data + count));
		count += sizeof(u32);
		block_hdr.size = be16_to_cpu(*(__be16 *)(mc_data + count));
		count += sizeof(u16);
		block_hdr.flags = be16_to_cpu(*(__be16 *)(mc_data + count));
		count += sizeof(u16);
		block_hdr.CRC = be16_to_cpu(*(__be16 *)(mc_data + count));
		count += sizeof(u16);

		pr_debug("%u: addr %u, size %u, flags 0x%04x, CRC 0x%04x\n",
			count, block_hdr.addr, block_hdr.size, block_hdr.flags,
			block_hdr.CRC);

		if (block_hdr.flags & 0x8) {
			u8 *auxblk = ((void *)mc_data) + block_hdr.addr;
			u16 auxtype;

			if (block_hdr.addr + sizeof(u16) > size)
				goto eof;

			auxtype = be16_to_cpu(*(__be16 *)(auxblk));

			/* Aux block. Check type */
			if (DRX_ISMCVERTYPE(auxtype)) {
				if (block_hdr.addr + 2 * sizeof(u16) + 2 * sizeof (u32) > size)
					goto eof;

				auxblk += sizeof(u16);
				mc_dev_type = be32_to_cpu(*(__be32 *)(auxblk));
				auxblk += sizeof(u32);
				mc_version = be32_to_cpu(*(__be32 *)(auxblk));
				auxblk += sizeof(u32);
				mc_base_version = be32_to_cpu(*(__be32 *)(auxblk));

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

/*
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
 *	-EINVAL:
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
	char *mc_file;

	/* Check arguments */
	if (!mc_info || !mc_info->mc_file)
		return -EINVAL;

	mc_file = mc_info->mc_file;

	if (!demod->firmware) {
		const struct firmware *fw = NULL;

		rc = request_firmware(&fw, mc_file, demod->i2c->dev.parent);
		if (rc < 0) {
			pr_err("Couldn't read firmware %s\n", mc_file);
			return rc;
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
	mc_magic_word = be16_to_cpu(*(__be16 *)(mc_data));
	mc_data += sizeof(u16);
	mc_nr_of_blks = be16_to_cpu(*(__be16 *)(mc_data));
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
		pr_info("Uploading firmware %s\n", mc_file);
	} else {
		pr_info("Verifying if firmware upload was ok.\n");
	}

	/* Process microcode blocks */
	for (i = 0; i < mc_nr_of_blks; i++) {
		struct drxu_code_block_hdr block_hdr;
		u16 mc_block_nr_bytes = 0;

		/* Process block header */
		block_hdr.addr = be32_to_cpu(*(__be32 *)(mc_data));
		mc_data += sizeof(u32);
		block_hdr.size = be16_to_cpu(*(__be16 *)(mc_data));
		mc_data += sizeof(u16);
		block_hdr.flags = be16_to_cpu(*(__be16 *)(mc_data));
		mc_data += sizeof(u16);
		block_hdr.CRC = be16_to_cpu(*(__be16 *)(mc_data));
		mc_data += sizeof(u16);

		pr_debug("%zd: addr %u, size %u, flags 0x%04x, CRC 0x%04x\n",
			(mc_data - mc_data_init), block_hdr.addr,
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
			if (drxdap_fasi_write_block(dev_addr,
							block_hdr.addr,
							mc_block_nr_bytes,
							mc_data, 0x0000)) {
				rc = -EIO;
				pr_err("error writing firmware at pos %zd\n",
				       mc_data - mc_data_init);
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

				if (drxdap_fasi_read_block(dev_addr,
						    curr_addr,
						    (u16)bytes_to_comp,
						    (u8 *)mc_data_buffer,
						    0x0000)) {
					pr_err("error reading firmware at pos %zd\n",
					       mc_data - mc_data_init);
					return -EIO;
				}

				result = memcmp(curr_ptr, mc_data_buffer,
						bytes_to_comp);

				if (result) {
					pr_err("error verifying firmware at pos %zd\n",
					       mc_data - mc_data_init);
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

/* caller is expected to check if lna is supported before enabling */
static int drxj_set_lna_state(struct drx_demod_instance *demod, bool state)
{
	struct drxuio_cfg uio_cfg;
	struct drxuio_data uio_data;
	int result;

	uio_cfg.uio = DRX_UIO1;
	uio_cfg.mode = DRX_UIO_MODE_READWRITE;
	/* Configure user-I/O #3: enable read/write */
	result = ctrl_set_uio_cfg(demod, &uio_cfg);
	if (result) {
		pr_err("Failed to setup LNA GPIO!\n");
		return result;
	}

	uio_data.uio = DRX_UIO1;
	uio_data.value = state;
	result = ctrl_uio_write(demod, &uio_data);
	if (result != 0) {
		pr_err("Failed to %sable LNA!\n",
		       state ? "en" : "dis");
		return result;
	}
	return 0;
}

/*
 * The Linux DVB Driver for Micronas DRX39xx family (drx3933j)
 *
 * Written by Devin Heitmueller <devin.heitmueller@kernellabs.com>
 */

static int drx39xxj_set_powerstate(struct dvb_frontend *fe, int enable)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	struct drx_demod_instance *demod = state->demod;
	int result;
	enum drx_power_mode power_mode;

	if (enable)
		power_mode = DRX_POWER_UP;
	else
		power_mode = DRX_POWER_DOWN;

	result = ctrl_power_mode(demod, &power_mode);
	if (result != 0) {
		pr_err("Power state change failed\n");
		return 0;
	}

	return 0;
}

static int drx39xxj_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	struct drx_demod_instance *demod = state->demod;
	int result;
	enum drx_lock_status lock_status;

	*status = 0;

	result = ctrl_lock_status(demod, &lock_status);
	if (result != 0) {
		pr_err("drx39xxj: could not get lock status!\n");
		*status = 0;
	}

	switch (lock_status) {
	case DRX_NEVER_LOCK:
		*status = 0;
		pr_err("drx says NEVER_LOCK\n");
		break;
	case DRX_NOT_LOCKED:
		*status = 0;
		break;
	case DRX_LOCK_STATE_1:
	case DRX_LOCK_STATE_2:
	case DRX_LOCK_STATE_3:
	case DRX_LOCK_STATE_4:
	case DRX_LOCK_STATE_5:
	case DRX_LOCK_STATE_6:
	case DRX_LOCK_STATE_7:
	case DRX_LOCK_STATE_8:
	case DRX_LOCK_STATE_9:
		*status = FE_HAS_SIGNAL
		    | FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC;
		break;
	case DRX_LOCKED:
		*status = FE_HAS_SIGNAL
		    | FE_HAS_CARRIER
		    | FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
		break;
	default:
		pr_err("Lock state unknown %d\n", lock_status);
	}
	ctrl_sig_quality(demod, lock_status);

	return 0;
}

static int drx39xxj_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	if (p->pre_bit_error.stat[0].scale == FE_SCALE_NOT_AVAILABLE) {
		*ber = 0;
		return 0;
	}

	if (!p->pre_bit_count.stat[0].uvalue) {
		if (!p->pre_bit_error.stat[0].uvalue)
			*ber = 0;
		else
			*ber = 1000000;
	} else {
		*ber = frac_times1e6(p->pre_bit_error.stat[0].uvalue,
				     p->pre_bit_count.stat[0].uvalue);
	}
	return 0;
}

static int drx39xxj_read_signal_strength(struct dvb_frontend *fe,
					 u16 *strength)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	if (p->strength.stat[0].scale == FE_SCALE_NOT_AVAILABLE) {
		*strength = 0;
		return 0;
	}

	*strength = p->strength.stat[0].uvalue;
	return 0;
}

static int drx39xxj_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u64 tmp64;

	if (p->cnr.stat[0].scale == FE_SCALE_NOT_AVAILABLE) {
		*snr = 0;
		return 0;
	}

	tmp64 = p->cnr.stat[0].svalue;
	do_div(tmp64, 10);
	*snr = tmp64;
	return 0;
}

static int drx39xxj_read_ucblocks(struct dvb_frontend *fe, u32 *ucb)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	if (p->block_error.stat[0].scale == FE_SCALE_NOT_AVAILABLE) {
		*ucb = 0;
		return 0;
	}

	*ucb = p->block_error.stat[0].uvalue;
	return 0;
}

static int drx39xxj_set_frontend(struct dvb_frontend *fe)
{
#ifdef DJH_DEBUG
	int i;
#endif
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct drx39xxj_state *state = fe->demodulator_priv;
	struct drx_demod_instance *demod = state->demod;
	enum drx_standard standard = DRX_STANDARD_8VSB;
	struct drx_channel channel;
	int result;
	static const struct drx_channel def_channel = {
		/* frequency      */ 0,
		/* bandwidth      */ DRX_BANDWIDTH_6MHZ,
		/* mirror         */ DRX_MIRROR_NO,
		/* constellation  */ DRX_CONSTELLATION_AUTO,
		/* hierarchy      */ DRX_HIERARCHY_UNKNOWN,
		/* priority       */ DRX_PRIORITY_UNKNOWN,
		/* coderate       */ DRX_CODERATE_UNKNOWN,
		/* guard          */ DRX_GUARD_UNKNOWN,
		/* fftmode        */ DRX_FFTMODE_UNKNOWN,
		/* classification */ DRX_CLASSIFICATION_AUTO,
		/* symbolrate     */ 5057000,
		/* interleavemode */ DRX_INTERLEAVEMODE_UNKNOWN,
		/* ldpc           */ DRX_LDPC_UNKNOWN,
		/* carrier        */ DRX_CARRIER_UNKNOWN,
		/* frame mode     */ DRX_FRAMEMODE_UNKNOWN
	};
	u32 constellation = DRX_CONSTELLATION_AUTO;

	/* Bring the demod out of sleep */
	drx39xxj_set_powerstate(fe, 1);

	if (fe->ops.tuner_ops.set_params) {
		u32 int_freq;

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		/* Set tuner to desired frequency and standard */
		fe->ops.tuner_ops.set_params(fe);

		/* Use the tuner's IF */
		if (fe->ops.tuner_ops.get_if_frequency) {
			fe->ops.tuner_ops.get_if_frequency(fe, &int_freq);
			demod->my_common_attr->intermediate_freq = int_freq / 1000;
		}

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	switch (p->delivery_system) {
	case SYS_ATSC:
		standard = DRX_STANDARD_8VSB;
		break;
	case SYS_DVBC_ANNEX_B:
		standard = DRX_STANDARD_ITU_B;

		switch (p->modulation) {
		case QAM_64:
			constellation = DRX_CONSTELLATION_QAM64;
			break;
		case QAM_256:
			constellation = DRX_CONSTELLATION_QAM256;
			break;
		default:
			constellation = DRX_CONSTELLATION_AUTO;
			break;
		}
		break;
	default:
		return -EINVAL;
	}
	/* Set the standard (will be powered up if necessary */
	result = ctrl_set_standard(demod, &standard);
	if (result != 0) {
		pr_err("Failed to set standard! result=%02x\n",
			result);
		return -EINVAL;
	}

	/* set channel parameters */
	channel = def_channel;
	channel.frequency = p->frequency / 1000;
	channel.bandwidth = DRX_BANDWIDTH_6MHZ;
	channel.constellation = constellation;

	/* program channel */
	result = ctrl_set_channel(demod, &channel);
	if (result != 0) {
		pr_err("Failed to set channel!\n");
		return -EINVAL;
	}
	/* Just for giggles, let's shut off the LNA again.... */
	drxj_set_lna_state(demod, false);

	/* After set_frontend, except for strength, stats aren't available */
	p->strength.stat[0].scale = FE_SCALE_RELATIVE;

	return 0;
}

static int drx39xxj_sleep(struct dvb_frontend *fe)
{
	/* power-down the demodulator */
	return drx39xxj_set_powerstate(fe, 0);
}

static int drx39xxj_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	struct drx_demod_instance *demod = state->demod;
	bool i2c_gate_state;
	int result;

#ifdef DJH_DEBUG
	pr_debug("i2c gate call: enable=%d state=%d\n", enable,
	       state->i2c_gate_open);
#endif

	if (enable)
		i2c_gate_state = true;
	else
		i2c_gate_state = false;

	if (state->i2c_gate_open == enable) {
		/* We're already in the desired state */
		return 0;
	}

	result = ctrl_i2c_bridge(demod, &i2c_gate_state);
	if (result != 0) {
		pr_err("drx39xxj: could not open i2c gate [%d]\n",
		       result);
		dump_stack();
	} else {
		state->i2c_gate_open = enable;
	}
	return 0;
}

static int drx39xxj_init(struct dvb_frontend *fe)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	struct drx_demod_instance *demod = state->demod;
	int rc = 0;

	if (fe->exit == DVB_FE_DEVICE_RESUME) {
		/* so drxj_open() does what it needs to do */
		demod->my_common_attr->is_opened = false;
		rc = drxj_open(demod);
		if (rc != 0)
			pr_err("drx39xxj_init(): DRX open failed rc=%d!\n", rc);
	} else
		drx39xxj_set_powerstate(fe, 1);

	return rc;
}

static int drx39xxj_set_lna(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct drx39xxj_state *state = fe->demodulator_priv;
	struct drx_demod_instance *demod = state->demod;
	struct drxj_data *ext_attr = demod->my_ext_attr;

	if (c->lna) {
		if (!ext_attr->has_lna) {
			pr_err("LNA is not supported on this device!\n");
			return -EINVAL;

		}
	}

	return drxj_set_lna_state(demod, c->lna);
}

static int drx39xxj_get_tune_settings(struct dvb_frontend *fe,
				      struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void drx39xxj_release(struct dvb_frontend *fe)
{
	struct drx39xxj_state *state = fe->demodulator_priv;
	struct drx_demod_instance *demod = state->demod;

	/* if device is removed don't access it */
	if (fe->exit != DVB_FE_DEVICE_REMOVED)
		drxj_close(demod);

	kfree(demod->my_ext_attr);
	kfree(demod->my_common_attr);
	kfree(demod->my_i2c_dev_addr);
	release_firmware(demod->firmware);
	kfree(demod);
	kfree(state);
}

static const struct dvb_frontend_ops drx39xxj_ops;

struct dvb_frontend *drx39xxj_attach(struct i2c_adapter *i2c)
{
	struct drx39xxj_state *state = NULL;
	struct i2c_device_addr *demod_addr = NULL;
	struct drx_common_attr *demod_comm_attr = NULL;
	struct drxj_data *demod_ext_attr = NULL;
	struct drx_demod_instance *demod = NULL;
	struct dtv_frontend_properties *p;
	int result;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct drx39xxj_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	demod = kmemdup(&drxj_default_demod_g,
			sizeof(struct drx_demod_instance), GFP_KERNEL);
	if (demod == NULL)
		goto error;

	demod_addr = kmemdup(&drxj_default_addr_g,
			     sizeof(struct i2c_device_addr), GFP_KERNEL);
	if (demod_addr == NULL)
		goto error;

	demod_comm_attr = kmemdup(&drxj_default_comm_attr_g,
				  sizeof(struct drx_common_attr), GFP_KERNEL);
	if (demod_comm_attr == NULL)
		goto error;

	demod_ext_attr = kmemdup(&drxj_data_g, sizeof(struct drxj_data),
				 GFP_KERNEL);
	if (demod_ext_attr == NULL)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	state->demod = demod;

	/* setup the demod data */
	demod->my_i2c_dev_addr = demod_addr;
	demod->my_common_attr = demod_comm_attr;
	demod->my_i2c_dev_addr->user_data = state;
	demod->my_common_attr->microcode_file = DRX39XX_MAIN_FIRMWARE;
	demod->my_common_attr->verify_microcode = true;
	demod->my_common_attr->intermediate_freq = 5000;
	demod->my_common_attr->current_power_mode = DRX_POWER_DOWN;
	demod->my_ext_attr = demod_ext_attr;
	((struct drxj_data *)demod_ext_attr)->uio_sma_tx_mode = DRX_UIO_MODE_READWRITE;
	demod->i2c = i2c;

	result = drxj_open(demod);
	if (result != 0) {
		pr_err("DRX open failed!  Aborting\n");
		goto error;
	}

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &drx39xxj_ops,
	       sizeof(struct dvb_frontend_ops));

	state->frontend.demodulator_priv = state;

	/* Initialize stats - needed for DVBv5 stats to work */
	p = &state->frontend.dtv_property_cache;
	p->strength.len = 1;
	p->pre_bit_count.len = 1;
	p->pre_bit_error.len = 1;
	p->post_bit_count.len = 1;
	p->post_bit_error.len = 1;
	p->block_count.len = 1;
	p->block_error.len = 1;
	p->cnr.len = 1;

	p->strength.stat[0].scale = FE_SCALE_RELATIVE;
	p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	return &state->frontend;

error:
	kfree(demod_ext_attr);
	kfree(demod_comm_attr);
	kfree(demod_addr);
	kfree(demod);
	kfree(state);

	return NULL;
}
EXPORT_SYMBOL(drx39xxj_attach);

static const struct dvb_frontend_ops drx39xxj_ops = {
	.delsys = { SYS_ATSC, SYS_DVBC_ANNEX_B },
	.info = {
		 .name = "Micronas DRX39xxj family Frontend",
		 .frequency_min_hz =  51 * MHz,
		 .frequency_max_hz = 858 * MHz,
		 .frequency_stepsize_hz = 62500,
		 .caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},

	.init = drx39xxj_init,
	.i2c_gate_ctrl = drx39xxj_i2c_gate_ctrl,
	.sleep = drx39xxj_sleep,
	.set_frontend = drx39xxj_set_frontend,
	.get_tune_settings = drx39xxj_get_tune_settings,
	.read_status = drx39xxj_read_status,
	.read_ber = drx39xxj_read_ber,
	.read_signal_strength = drx39xxj_read_signal_strength,
	.read_snr = drx39xxj_read_snr,
	.read_ucblocks = drx39xxj_read_ucblocks,
	.release = drx39xxj_release,
	.set_lna = drx39xxj_set_lna,
};

MODULE_DESCRIPTION("Micronas DRX39xxj Frontend");
MODULE_AUTHOR("Devin Heitmueller");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(DRX39XX_MAIN_FIRMWARE);
