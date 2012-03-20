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
* \file $Id: drx_driver.h,v 1.84 2010/01/14 22:47:50 dingtao Exp $
*
* \brief DRX driver API
*
*/
#ifndef __DRXDRIVER_H__
#define __DRXDRIVER_H__
/*-------------------------------------------------------------------------
INCLUDES
-------------------------------------------------------------------------*/
#include "bsp_types.h"
#include "bsp_i2c.h"
#include "bsp_tuner.h"
#include "bsp_host.h"

#ifdef __cplusplus
extern "C" {
#endif
/*-------------------------------------------------------------------------
TYPEDEFS
-------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------
DEFINES
-------------------------------------------------------------------------*/

/**************
*
* This section configures the DRX Data Access Protocols (DAPs).
*
**************/

/**
* \def DRXDAP_SINGLE_MASTER
* \brief Enable I2C single or I2C multimaster mode on host.
*
* Set to 1 to enable single master mode
* Set to 0 to enable multi master mode
*
* The actual DAP implementation may be restricted to only one of the modes.
* A compiler warning or error will be generated if the DAP implementation
* overides or cannot handle the mode defined below.
*
*/
#ifndef DRXDAP_SINGLE_MASTER
#define DRXDAP_SINGLE_MASTER 0
#endif

/**
* \def DRXDAP_MAX_WCHUNKSIZE
* \brief Defines maximum chunksize of an i2c write action by host.
*
* This indicates the maximum size of data the I2C device driver is able to
* write at a time. This includes I2C device address and register addressing.
*
* This maximum size may be restricted by the actual DAP implementation.
* A compiler warning or error will be generated if the DAP implementation
* overides or cannot handle the chunksize defined below.
*
* Beware that the DAP uses  DRXDAP_MAX_WCHUNKSIZE to create a temporary data
* buffer. Do not undefine or choose too large, unless your system is able to
* handle a stack buffer of that size.
*
*/
#ifndef DRXDAP_MAX_WCHUNKSIZE
#define  DRXDAP_MAX_WCHUNKSIZE 60
#endif

/**
* \def DRXDAP_MAX_RCHUNKSIZE
* \brief Defines maximum chunksize of an i2c read action by host.
*
* This indicates the maximum size of data the I2C device driver is able to read
* at a time. Minimum value is 2. Also, the read chunk size must be even.
*
* This maximum size may be restricted by the actual DAP implementation.
* A compiler warning or error will be generated if the DAP implementation
* overides or cannot handle the chunksize defined below.
*
*/
#ifndef DRXDAP_MAX_RCHUNKSIZE
#define  DRXDAP_MAX_RCHUNKSIZE 60
#endif

/**************
*
* This section describes drxdriver defines.
*
**************/

/**
* \def DRX_UNKNOWN
* \brief Generic UNKNOWN value for DRX enumerated types.
*
* Used to indicate that the parameter value is unknown or not yet initalized.
*/
#ifndef DRX_UNKNOWN
#define DRX_UNKNOWN (254)
#endif

/**
* \def DRX_AUTO
* \brief Generic AUTO value for DRX enumerated types.
*
* Used to instruct the driver to automatically determine the value of the
* parameter.
*/
#ifndef DRX_AUTO
#define DRX_AUTO    (255)
#endif

/**************
*
* This section describes flag definitions for the device capbilities.
*
**************/

/**
* \brief LNA capability flag
*
* Device has a Low Noise Amplifier
*
*/
#define DRX_CAPABILITY_HAS_LNA           (1UL <<  0)
/**
* \brief OOB-RX capability flag
*
* Device has OOB-RX
*
*/
#define DRX_CAPABILITY_HAS_OOBRX         (1UL <<  1)
/**
* \brief ATV capability flag
*
* Device has ATV
*
*/
#define DRX_CAPABILITY_HAS_ATV           (1UL <<  2)
/**
* \brief DVB-T capability flag
*
* Device has DVB-T
*
*/
#define DRX_CAPABILITY_HAS_DVBT          (1UL <<  3)
/**
* \brief  ITU-B capability flag
*
* Device has ITU-B
*
*/
#define DRX_CAPABILITY_HAS_ITUB          (1UL <<  4)
/**
* \brief  Audio capability flag
*
* Device has Audio
*
*/
#define DRX_CAPABILITY_HAS_AUD           (1UL <<  5)
/**
* \brief  SAW switch capability flag
*
* Device has SAW switch
*
*/
#define DRX_CAPABILITY_HAS_SAWSW         (1UL <<  6)
/**
* \brief  GPIO1 capability flag
*
* Device has GPIO1
*
*/
#define DRX_CAPABILITY_HAS_GPIO1         (1UL <<  7)
/**
* \brief  GPIO2 capability flag
*
* Device has GPIO2
*
*/
#define DRX_CAPABILITY_HAS_GPIO2         (1UL <<  8)
/**
* \brief  IRQN capability flag
*
* Device has IRQN
*
*/
#define DRX_CAPABILITY_HAS_IRQN          (1UL <<  9)
/**
* \brief  8VSB capability flag
*
* Device has 8VSB
*
*/
#define DRX_CAPABILITY_HAS_8VSB          (1UL << 10)
/**
* \brief  SMA-TX capability flag
*
* Device has SMATX
*
*/
#define DRX_CAPABILITY_HAS_SMATX         (1UL << 11)
/**
* \brief  SMA-RX capability flag
*
* Device has SMARX
*
*/
#define DRX_CAPABILITY_HAS_SMARX         (1UL << 12)
/**
* \brief  ITU-A/C capability flag
*
* Device has ITU-A/C
*
*/
#define DRX_CAPABILITY_HAS_ITUAC         (1UL << 13)

/*-------------------------------------------------------------------------
MACROS
-------------------------------------------------------------------------*/
/* Macros to stringify the version number */
#define DRX_VERSIONSTRING( MAJOR, MINOR, PATCH ) \
	 DRX_VERSIONSTRING_HELP(MAJOR)"." \
	 DRX_VERSIONSTRING_HELP(MINOR)"." \
	 DRX_VERSIONSTRING_HELP(PATCH)
#define DRX_VERSIONSTRING_HELP( NUM ) #NUM

/**
* \brief Macro to create byte array elements from 16 bit integers.
* This macro is used to create byte arrays for block writes.
* Block writes speed up I2C traffic between host and demod.
* The macro takes care of the required byte order in a 16 bits word.
* x->lowbyte(x), highbyte(x)
*/
#define DRX_16TO8( x ) ((u8_t) (((u16_t)x)    &0xFF)), \
			((u8_t)((((u16_t)x)>>8)&0xFF))

/**
* \brief Macro to sign extend signed 9 bit value to signed  16 bit value
*/
#define DRX_S9TOS16(x) ((((u16_t)x)&0x100 )?((s16_t)((u16_t)(x)|0xFF00)):(x))

/**
* \brief Macro to sign extend signed 9 bit value to signed  16 bit value
*/
#define DRX_S24TODRXFREQ(x) ( ( ( (u32_t) x ) & 0x00800000UL ) ? \
				 (  (DRXFrequency_t) \
				    ( ( (u32_t) x ) | 0xFF000000 ) ) : \
				 ( (DRXFrequency_t) x ) )

/**
* \brief Macro to convert 16 bit register value to a DRXFrequency_t
*/
#define DRX_U16TODRXFREQ(x)   (  ( x & 0x8000 ) ? \
				 (  (DRXFrequency_t) \
				    ( ( (u32_t) x ) | 0xFFFF0000 ) ) : \
				 ( (DRXFrequency_t) x ) )

/*-------------------------------------------------------------------------
ENUM
-------------------------------------------------------------------------*/

/**
* \enum DRXStandard_t
* \brief Modulation standards.
*/
	typedef enum {
		DRX_STANDARD_DVBT = 0, /**< Terrestrial DVB-T.               */
		DRX_STANDARD_8VSB,     /**< Terrestrial 8VSB.                */
		DRX_STANDARD_NTSC,     /**< Terrestrial\Cable analog NTSC.   */
		DRX_STANDARD_PAL_SECAM_BG,
				       /**< Terrestrial analog PAL/SECAM B/G */
		DRX_STANDARD_PAL_SECAM_DK,
				       /**< Terrestrial analog PAL/SECAM D/K */
		DRX_STANDARD_PAL_SECAM_I,
				       /**< Terrestrial analog PAL/SECAM I   */
		DRX_STANDARD_PAL_SECAM_L,
				       /**< Terrestrial analog PAL/SECAM L
					     with negative modulation        */
		DRX_STANDARD_PAL_SECAM_LP,
				       /**< Terrestrial analog PAL/SECAM L
					     with positive modulation        */
		DRX_STANDARD_ITU_A,    /**< Cable ITU ANNEX A.               */
		DRX_STANDARD_ITU_B,    /**< Cable ITU ANNEX B.               */
		DRX_STANDARD_ITU_C,    /**< Cable ITU ANNEX C.               */
		DRX_STANDARD_ITU_D,    /**< Cable ITU ANNEX D.               */
		DRX_STANDARD_FM,       /**< Terrestrial\Cable FM radio       */
		DRX_STANDARD_DTMB,     /**< Terrestrial DTMB standard (China)*/
		DRX_STANDARD_UNKNOWN = DRX_UNKNOWN,
				       /**< Standard unknown.                */
		DRX_STANDARD_AUTO = DRX_AUTO
				       /**< Autodetect standard.             */
	} DRXStandard_t, *pDRXStandard_t;

/**
* \enum DRXStandard_t
* \brief Modulation sub-standards.
*/
	typedef enum {
		DRX_SUBSTANDARD_MAIN = 0, /**< Main subvariant of standard   */
		DRX_SUBSTANDARD_ATV_BG_SCANDINAVIA,
		DRX_SUBSTANDARD_ATV_DK_POLAND,
		DRX_SUBSTANDARD_ATV_DK_CHINA,
		DRX_SUBSTANDARD_UNKNOWN = DRX_UNKNOWN,
					  /**< Sub-standard unknown.         */
		DRX_SUBSTANDARD_AUTO = DRX_AUTO
					  /**< Auto (default) sub-standard   */
	} DRXSubstandard_t, *pDRXSubstandard_t;

/**
* \enum DRXBandwidth_t
* \brief Channel bandwidth or channel spacing.
*/
	typedef enum {
		DRX_BANDWIDTH_8MHZ = 0,	 /**< Bandwidth 8 MHz.   */
		DRX_BANDWIDTH_7MHZ,	 /**< Bandwidth 7 MHz.   */
		DRX_BANDWIDTH_6MHZ,	 /**< Bandwidth 6 MHz.   */
		DRX_BANDWIDTH_UNKNOWN = DRX_UNKNOWN,
					 /**< Bandwidth unknown. */
		DRX_BANDWIDTH_AUTO = DRX_AUTO
					 /**< Auto Set Bandwidth */
	} DRXBandwidth_t, *pDRXBandwidth_t;

/**
* \enum DRXMirror_t
* \brief Indicate if channel spectrum is mirrored or not.
*/
	typedef enum {
		DRX_MIRROR_NO = 0,   /**< Spectrum is not mirrored.           */
		DRX_MIRROR_YES,	     /**< Spectrum is mirrored.               */
		DRX_MIRROR_UNKNOWN = DRX_UNKNOWN,
				     /**< Unknown if spectrum is mirrored.    */
		DRX_MIRROR_AUTO = DRX_AUTO
				     /**< Autodetect if spectrum is mirrored. */
	} DRXMirror_t, *pDRXMirror_t;

/**
* \enum DRXConstellation_t
* \brief Constellation type of the channel.
*/
	typedef enum {
		DRX_CONSTELLATION_BPSK = 0,  /**< Modulation is BPSK.       */
		DRX_CONSTELLATION_QPSK,	     /**< Constellation is QPSK.    */
		DRX_CONSTELLATION_PSK8,	     /**< Constellation is PSK8.    */
		DRX_CONSTELLATION_QAM16,     /**< Constellation is QAM16.   */
		DRX_CONSTELLATION_QAM32,     /**< Constellation is QAM32.   */
		DRX_CONSTELLATION_QAM64,     /**< Constellation is QAM64.   */
		DRX_CONSTELLATION_QAM128,    /**< Constellation is QAM128.  */
		DRX_CONSTELLATION_QAM256,    /**< Constellation is QAM256.  */
		DRX_CONSTELLATION_QAM512,    /**< Constellation is QAM512.  */
		DRX_CONSTELLATION_QAM1024,   /**< Constellation is QAM1024. */
		DRX_CONSTELLATION_QPSK_NR,   /**< Constellation is QPSK_NR  */
		DRX_CONSTELLATION_UNKNOWN = DRX_UNKNOWN,
					     /**< Constellation unknown.    */
		DRX_CONSTELLATION_AUTO = DRX_AUTO
					     /**< Autodetect constellation. */
	} DRXConstellation_t, *pDRXConstellation_t;

/**
* \enum DRXHierarchy_t
* \brief Hierarchy of the channel.
*/
	typedef enum {
		DRX_HIERARCHY_NONE = 0,	/**< None hierarchical channel.     */
		DRX_HIERARCHY_ALPHA1,	/**< Hierarchical channel, alpha=1. */
		DRX_HIERARCHY_ALPHA2,	/**< Hierarchical channel, alpha=2. */
		DRX_HIERARCHY_ALPHA4,	/**< Hierarchical channel, alpha=4. */
		DRX_HIERARCHY_UNKNOWN = DRX_UNKNOWN,
					/**< Hierarchy unknown.             */
		DRX_HIERARCHY_AUTO = DRX_AUTO
					/**< Autodetect hierarchy.          */
	} DRXHierarchy_t, *pDRXHierarchy_t;

/**
* \enum DRXPriority_t
* \brief Channel priority in case of hierarchical transmission.
*/
	typedef enum {
		DRX_PRIORITY_LOW = 0,  /**< Low priority channel.  */
		DRX_PRIORITY_HIGH,     /**< High priority channel. */
		DRX_PRIORITY_UNKNOWN = DRX_UNKNOWN
				       /**< Priority unknown.      */
	} DRXPriority_t, *pDRXPriority_t;

/**
* \enum DRXCoderate_t
* \brief Channel priority in case of hierarchical transmission.
*/
	typedef enum {
		DRX_CODERATE_1DIV2 = 0,	/**< Code rate 1/2nd.      */
		DRX_CODERATE_2DIV3,	/**< Code rate 2/3nd.      */
		DRX_CODERATE_3DIV4,	/**< Code rate 3/4nd.      */
		DRX_CODERATE_5DIV6,	/**< Code rate 5/6nd.      */
		DRX_CODERATE_7DIV8,	/**< Code rate 7/8nd.      */
		DRX_CODERATE_UNKNOWN = DRX_UNKNOWN,
					/**< Code rate unknown.    */
		DRX_CODERATE_AUTO = DRX_AUTO
					/**< Autodetect code rate. */
	} DRXCoderate_t, *pDRXCoderate_t;

/**
* \enum DRXGuard_t
* \brief Guard interval of a channel.
*/
	typedef enum {
		DRX_GUARD_1DIV32 = 0, /**< Guard interval 1/32nd.     */
		DRX_GUARD_1DIV16,     /**< Guard interval 1/16th.     */
		DRX_GUARD_1DIV8,      /**< Guard interval 1/8th.      */
		DRX_GUARD_1DIV4,      /**< Guard interval 1/4th.      */
		DRX_GUARD_UNKNOWN = DRX_UNKNOWN,
				      /**< Guard interval unknown.    */
		DRX_GUARD_AUTO = DRX_AUTO
				      /**< Autodetect guard interval. */
	} DRXGuard_t, *pDRXGuard_t;

/**
* \enum DRXFftmode_t
* \brief FFT mode.
*/
	typedef enum {
		DRX_FFTMODE_2K = 0,    /**< 2K FFT mode.         */
		DRX_FFTMODE_4K,	       /**< 4K FFT mode.         */
		DRX_FFTMODE_8K,	       /**< 8K FFT mode.         */
		DRX_FFTMODE_UNKNOWN = DRX_UNKNOWN,
				       /**< FFT mode unknown.    */
		DRX_FFTMODE_AUTO = DRX_AUTO
				       /**< Autodetect FFT mode. */
	} DRXFftmode_t, *pDRXFftmode_t;

/**
* \enum DRXClassification_t
* \brief Channel classification.
*/
	typedef enum {
		DRX_CLASSIFICATION_GAUSS = 0, /**< Gaussion noise.            */
		DRX_CLASSIFICATION_HVY_GAUSS, /**< Heavy Gaussion noise.      */
		DRX_CLASSIFICATION_COCHANNEL, /**< Co-channel.                */
		DRX_CLASSIFICATION_STATIC,    /**< Static echo.               */
		DRX_CLASSIFICATION_MOVING,    /**< Moving echo.               */
		DRX_CLASSIFICATION_ZERODB,    /**< Zero dB echo.              */
		DRX_CLASSIFICATION_UNKNOWN = DRX_UNKNOWN,
					      /**< Unknown classification     */
		DRX_CLASSIFICATION_AUTO = DRX_AUTO
					      /**< Autodetect classification. */
	} DRXClassification_t, *pDRXClassification_t;

/**
* /enum DRXInterleaveModes_t
* /brief Interleave modes
*/
	typedef enum {
		DRX_INTERLEAVEMODE_I128_J1 = 0,
		DRX_INTERLEAVEMODE_I128_J1_V2,
		DRX_INTERLEAVEMODE_I128_J2,
		DRX_INTERLEAVEMODE_I64_J2,
		DRX_INTERLEAVEMODE_I128_J3,
		DRX_INTERLEAVEMODE_I32_J4,
		DRX_INTERLEAVEMODE_I128_J4,
		DRX_INTERLEAVEMODE_I16_J8,
		DRX_INTERLEAVEMODE_I128_J5,
		DRX_INTERLEAVEMODE_I8_J16,
		DRX_INTERLEAVEMODE_I128_J6,
		DRX_INTERLEAVEMODE_RESERVED_11,
		DRX_INTERLEAVEMODE_I128_J7,
		DRX_INTERLEAVEMODE_RESERVED_13,
		DRX_INTERLEAVEMODE_I128_J8,
		DRX_INTERLEAVEMODE_RESERVED_15,
		DRX_INTERLEAVEMODE_I12_J17,
		DRX_INTERLEAVEMODE_I5_J4,
		DRX_INTERLEAVEMODE_B52_M240,
		DRX_INTERLEAVEMODE_B52_M720,
		DRX_INTERLEAVEMODE_B52_M48,
		DRX_INTERLEAVEMODE_B52_M0,
		DRX_INTERLEAVEMODE_UNKNOWN = DRX_UNKNOWN,
					      /**< Unknown interleave mode    */
		DRX_INTERLEAVEMODE_AUTO = DRX_AUTO
					      /**< Autodetect interleave mode */
	} DRXInterleaveModes_t, *pDRXInterleaveModes_t;

/**
* \enum DRXCarrier_t
* \brief Channel Carrier Mode.
*/
	typedef enum {
		DRX_CARRIER_MULTI = 0,		/**< Multi carrier mode       */
		DRX_CARRIER_SINGLE,		/**< Single carrier mode      */
		DRX_CARRIER_UNKNOWN = DRX_UNKNOWN,
						/**< Carrier mode unknown.    */
		DRX_CARRIER_AUTO = DRX_AUTO	/**< Autodetect carrier mode  */
	} DRXCarrier_t, *pDRXCarrier_t;

/**
* \enum DRXFramemode_t
* \brief Channel Frame Mode.
*/
	typedef enum {
		DRX_FRAMEMODE_420 = 0,	 /**< 420 with variable PN  */
		DRX_FRAMEMODE_595,	 /**< 595                   */
		DRX_FRAMEMODE_945,	 /**< 945 with variable PN  */
		DRX_FRAMEMODE_420_FIXED_PN,
					 /**< 420 with fixed PN     */
		DRX_FRAMEMODE_945_FIXED_PN,
					 /**< 945 with fixed PN     */
		DRX_FRAMEMODE_UNKNOWN = DRX_UNKNOWN,
					 /**< Frame mode unknown.   */
		DRX_FRAMEMODE_AUTO = DRX_AUTO
					 /**< Autodetect frame mode */
	} DRXFramemode_t, *pDRXFramemode_t;

/**
* \enum DRXTPSFrame_t
* \brief Frame number in current super-frame.
*/
	typedef enum {
		DRX_TPS_FRAME1 = 0,	  /**< TPS frame 1.       */
		DRX_TPS_FRAME2,		  /**< TPS frame 2.       */
		DRX_TPS_FRAME3,		  /**< TPS frame 3.       */
		DRX_TPS_FRAME4,		  /**< TPS frame 4.       */
		DRX_TPS_FRAME_UNKNOWN = DRX_UNKNOWN
					  /**< TPS frame unknown. */
	} DRXTPSFrame_t, *pDRXTPSFrame_t;

/**
* \enum DRXLDPC_t
* \brief TPS LDPC .
*/
	typedef enum {
		DRX_LDPC_0_4 = 0,	  /**< LDPC 0.4           */
		DRX_LDPC_0_6,		  /**< LDPC 0.6           */
		DRX_LDPC_0_8,		  /**< LDPC 0.8           */
		DRX_LDPC_UNKNOWN = DRX_UNKNOWN,
					  /**< LDPC unknown.      */
		DRX_LDPC_AUTO = DRX_AUTO  /**< Autodetect LDPC    */
	} DRXLDPC_t, *pDRXLDPC_t;

/**
* \enum DRXPilotMode_t
* \brief Pilot modes in DTMB.
*/
	typedef enum {
		DRX_PILOT_ON = 0,	  /**< Pilot On             */
		DRX_PILOT_OFF,		  /**< Pilot Off            */
		DRX_PILOT_UNKNOWN = DRX_UNKNOWN,
					  /**< Pilot unknown.       */
		DRX_PILOT_AUTO = DRX_AUTO /**< Autodetect Pilot     */
	} DRXPilotMode_t, *pDRXPilotMode_t;

/**
* \enum DRXCtrlIndex_t
* \brief Indices of the control functions.
*/
	typedef u32_t DRXCtrlIndex_t, *pDRXCtrlIndex_t;

#ifndef DRX_CTRL_BASE
#define DRX_CTRL_BASE          ((DRXCtrlIndex_t)0)
#endif

#define DRX_CTRL_NOP             ( DRX_CTRL_BASE +  0)/**< No Operation       */
#define DRX_CTRL_PROBE_DEVICE    ( DRX_CTRL_BASE +  1)/**< Probe device       */

#define DRX_CTRL_LOAD_UCODE      ( DRX_CTRL_BASE +  2)/**< Load microcode     */
#define DRX_CTRL_VERIFY_UCODE    ( DRX_CTRL_BASE +  3)/**< Verify microcode   */
#define DRX_CTRL_SET_CHANNEL     ( DRX_CTRL_BASE +  4)/**< Set channel        */
#define DRX_CTRL_GET_CHANNEL     ( DRX_CTRL_BASE +  5)/**< Get channel        */
#define DRX_CTRL_LOCK_STATUS     ( DRX_CTRL_BASE +  6)/**< Get lock status    */
#define DRX_CTRL_SIG_QUALITY     ( DRX_CTRL_BASE +  7)/**< Get signal quality */
#define DRX_CTRL_SIG_STRENGTH    ( DRX_CTRL_BASE +  8)/**< Get signal strength*/
#define DRX_CTRL_RF_POWER        ( DRX_CTRL_BASE +  9)/**< Get RF power       */
#define DRX_CTRL_CONSTEL         ( DRX_CTRL_BASE + 10)/**< Get constel point  */
#define DRX_CTRL_SCAN_INIT       ( DRX_CTRL_BASE + 11)/**< Initialize scan    */
#define DRX_CTRL_SCAN_NEXT       ( DRX_CTRL_BASE + 12)/**< Scan for next      */
#define DRX_CTRL_SCAN_STOP       ( DRX_CTRL_BASE + 13)/**< Stop scan          */
#define DRX_CTRL_TPS_INFO        ( DRX_CTRL_BASE + 14)/**< Get TPS info       */
#define DRX_CTRL_SET_CFG         ( DRX_CTRL_BASE + 15)/**< Set configuration  */
#define DRX_CTRL_GET_CFG         ( DRX_CTRL_BASE + 16)/**< Get configuration  */
#define DRX_CTRL_VERSION         ( DRX_CTRL_BASE + 17)/**< Get version info   */
#define DRX_CTRL_I2C_BRIDGE      ( DRX_CTRL_BASE + 18)/**< Open/close  bridge */
#define DRX_CTRL_SET_STANDARD    ( DRX_CTRL_BASE + 19)/**< Set demod std      */
#define DRX_CTRL_GET_STANDARD    ( DRX_CTRL_BASE + 20)/**< Get demod std      */
#define DRX_CTRL_SET_OOB         ( DRX_CTRL_BASE + 21)/**< Set OOB param      */
#define DRX_CTRL_GET_OOB         ( DRX_CTRL_BASE + 22)/**< Get OOB param      */
#define DRX_CTRL_AUD_SET_STANDARD (DRX_CTRL_BASE + 23)/**< Set audio param    */
#define DRX_CTRL_AUD_GET_STANDARD (DRX_CTRL_BASE + 24)/**< Get audio param    */
#define DRX_CTRL_AUD_GET_STATUS  ( DRX_CTRL_BASE + 25)/**< Read RDS           */
#define DRX_CTRL_AUD_BEEP        ( DRX_CTRL_BASE + 26)/**< Read RDS           */
#define DRX_CTRL_I2C_READWRITE   ( DRX_CTRL_BASE + 27)/**< Read/write I2C     */
#define DRX_CTRL_PROGRAM_TUNER   ( DRX_CTRL_BASE + 28)/**< Program tuner      */

	/* Professional */
#define DRX_CTRL_MB_CFG          ( DRX_CTRL_BASE + 29) /**<                   */
#define DRX_CTRL_MB_READ         ( DRX_CTRL_BASE + 30) /**<                   */
#define DRX_CTRL_MB_WRITE        ( DRX_CTRL_BASE + 31) /**<                   */
#define DRX_CTRL_MB_CONSTEL      ( DRX_CTRL_BASE + 32) /**<                   */
#define DRX_CTRL_MB_MER          ( DRX_CTRL_BASE + 33) /**<                   */

	/* Misc */
#define DRX_CTRL_UIO_CFG         DRX_CTRL_SET_UIO_CFG  /**< Configure UIO     */
#define DRX_CTRL_SET_UIO_CFG     ( DRX_CTRL_BASE + 34) /**< Configure UIO     */
#define DRX_CTRL_GET_UIO_CFG     ( DRX_CTRL_BASE + 35) /**< Configure UIO     */
#define DRX_CTRL_UIO_READ        ( DRX_CTRL_BASE + 36) /**< Read from UIO     */
#define DRX_CTRL_UIO_WRITE       ( DRX_CTRL_BASE + 37) /**< Write to UIO      */
#define DRX_CTRL_READ_EVENTS     ( DRX_CTRL_BASE + 38) /**< Read events       */
#define DRX_CTRL_HDL_EVENTS      ( DRX_CTRL_BASE + 39) /**< Handle events     */
#define DRX_CTRL_POWER_MODE      ( DRX_CTRL_BASE + 40) /**< Set power mode    */
#define DRX_CTRL_LOAD_FILTER     ( DRX_CTRL_BASE + 41) /**< Load chan. filter */
#define DRX_CTRL_VALIDATE_UCODE  ( DRX_CTRL_BASE + 42) /**< Validate ucode    */
#define DRX_CTRL_DUMP_REGISTERS  ( DRX_CTRL_BASE + 43) /**< Dump registers    */

#define DRX_CTRL_MAX             ( DRX_CTRL_BASE + 44)	/* never to be used    */

/**
* \enum DRXUCodeAction_t
* \brief Used to indicate if firmware has to be uploaded or verified.
*/

	typedef enum {
		UCODE_UPLOAD,
		  /**< Upload the microcode image to device        */
		UCODE_VERIFY
		  /**< Compare microcode image with code on device */
	} DRXUCodeAction_t, *pDRXUCodeAction_t;

/**
* \enum DRXLockStatus_t
* \brief Used to reflect current lock status of demodulator.
*
* The generic lock states have device dependent semantics.
*/
	typedef enum {
		DRX_NEVER_LOCK = 0,
			      /**< Device will never lock on this signal */
		DRX_NOT_LOCKED,
			      /**< Device has no lock at all             */
		DRX_LOCK_STATE_1,
			      /**< Generic lock state                    */
		DRX_LOCK_STATE_2,
			      /**< Generic lock state                    */
		DRX_LOCK_STATE_3,
			      /**< Generic lock state                    */
		DRX_LOCK_STATE_4,
			      /**< Generic lock state                    */
		DRX_LOCK_STATE_5,
			      /**< Generic lock state                    */
		DRX_LOCK_STATE_6,
			      /**< Generic lock state                    */
		DRX_LOCK_STATE_7,
			      /**< Generic lock state                    */
		DRX_LOCK_STATE_8,
			      /**< Generic lock state                    */
		DRX_LOCK_STATE_9,
			      /**< Generic lock state                    */
		DRX_LOCKED    /**< Device is in lock                     */
	} DRXLockStatus_t, *pDRXLockStatus_t;

/**
* \enum DRXUIO_t
* \brief Used to address a User IO (UIO).
*/
	typedef enum {
		DRX_UIO1,
		DRX_UIO2,
		DRX_UIO3,
		DRX_UIO4,
		DRX_UIO5,
		DRX_UIO6,
		DRX_UIO7,
		DRX_UIO8,
		DRX_UIO9,
		DRX_UIO10,
		DRX_UIO11,
		DRX_UIO12,
		DRX_UIO13,
		DRX_UIO14,
		DRX_UIO15,
		DRX_UIO16,
		DRX_UIO17,
		DRX_UIO18,
		DRX_UIO19,
		DRX_UIO20,
		DRX_UIO21,
		DRX_UIO22,
		DRX_UIO23,
		DRX_UIO24,
		DRX_UIO25,
		DRX_UIO26,
		DRX_UIO27,
		DRX_UIO28,
		DRX_UIO29,
		DRX_UIO30,
		DRX_UIO31,
		DRX_UIO32,
		DRX_UIO_MAX = DRX_UIO32
	} DRXUIO_t, *pDRXUIO_t;

/**
* \enum DRXUIOMode_t
* \brief Used to configure the modus oprandi of a UIO.
*
* DRX_UIO_MODE_FIRMWARE is an old uio mode.
* It is replaced by the modes DRX_UIO_MODE_FIRMWARE0 .. DRX_UIO_MODE_FIRMWARE9.
* To be backward compatible DRX_UIO_MODE_FIRMWARE is equivalent to
* DRX_UIO_MODE_FIRMWARE0.
*/
	typedef enum {
		DRX_UIO_MODE_DISABLE = 0x01,
				    /**< not used, pin is configured as input */
		DRX_UIO_MODE_READWRITE = 0x02,
				    /**< used for read/write by application   */
		DRX_UIO_MODE_FIRMWARE = 0x04,
				    /**< controlled by firmware, function 0   */
		DRX_UIO_MODE_FIRMWARE0 = DRX_UIO_MODE_FIRMWARE,
						    /**< same as above        */
		DRX_UIO_MODE_FIRMWARE1 = 0x08,
				    /**< controlled by firmware, function 1   */
		DRX_UIO_MODE_FIRMWARE2 = 0x10,
				    /**< controlled by firmware, function 2   */
		DRX_UIO_MODE_FIRMWARE3 = 0x20,
				    /**< controlled by firmware, function 3   */
		DRX_UIO_MODE_FIRMWARE4 = 0x40,
				    /**< controlled by firmware, function 4   */
		DRX_UIO_MODE_FIRMWARE5 = 0x80
				    /**< controlled by firmware, function 5   */
	} DRXUIOMode_t, *pDRXUIOMode_t;

/**
* \enum DRXOOBDownstreamStandard_t
* \brief Used to select OOB standard.
*
* Based on ANSI 55-1 and 55-2
*/
	typedef enum {
		DRX_OOB_MODE_A = 0,
			       /**< ANSI 55-1   */
		DRX_OOB_MODE_B_GRADE_A,
			       /**< ANSI 55-2 A */
		DRX_OOB_MODE_B_GRADE_B
			       /**< ANSI 55-2 B */
	} DRXOOBDownstreamStandard_t, *pDRXOOBDownstreamStandard_t;

/*-------------------------------------------------------------------------
STRUCTS
-------------------------------------------------------------------------*/

/*============================================================================*/
/*============================================================================*/
/*== CTRL CFG related data structures ========================================*/
/*============================================================================*/
/*============================================================================*/

/**
* \enum DRXCfgType_t
* \brief Generic configuration function identifiers.
*/
	typedef u32_t DRXCfgType_t, *pDRXCfgType_t;

#ifndef DRX_CFG_BASE
#define DRX_CFG_BASE          ((DRXCfgType_t)0)
#endif

#define DRX_CFG_MPEG_OUTPUT         ( DRX_CFG_BASE +  0)	/* MPEG TS output    */
#define DRX_CFG_PKTERR              ( DRX_CFG_BASE +  1)	/* Packet Error      */
#define DRX_CFG_SYMCLK_OFFS         ( DRX_CFG_BASE +  2)	/* Symbol Clk Offset */
#define DRX_CFG_SMA                 ( DRX_CFG_BASE +  3)	/* Smart Antenna     */
#define DRX_CFG_PINSAFE             ( DRX_CFG_BASE +  4)	/* Pin safe mode     */
#define DRX_CFG_SUBSTANDARD         ( DRX_CFG_BASE +  5)	/* substandard       */
#define DRX_CFG_AUD_VOLUME          ( DRX_CFG_BASE +  6)	/* volume            */
#define DRX_CFG_AUD_RDS             ( DRX_CFG_BASE +  7)	/* rds               */
#define DRX_CFG_AUD_AUTOSOUND       ( DRX_CFG_BASE +  8)	/* ASS & ASC         */
#define DRX_CFG_AUD_ASS_THRES       ( DRX_CFG_BASE +  9)	/* ASS Thresholds    */
#define DRX_CFG_AUD_DEVIATION       ( DRX_CFG_BASE + 10)	/* Deviation         */
#define DRX_CFG_AUD_PRESCALE        ( DRX_CFG_BASE + 11)	/* Prescale          */
#define DRX_CFG_AUD_MIXER           ( DRX_CFG_BASE + 12)	/* Mixer             */
#define DRX_CFG_AUD_AVSYNC          ( DRX_CFG_BASE + 13)	/* AVSync            */
#define DRX_CFG_AUD_CARRIER         ( DRX_CFG_BASE + 14)	/* Audio carriers    */
#define DRX_CFG_I2S_OUTPUT          ( DRX_CFG_BASE + 15)	/* I2S output        */
#define DRX_CFG_ATV_STANDARD        ( DRX_CFG_BASE + 16)	/* ATV standard      */
#define DRX_CFG_SQI_SPEED           ( DRX_CFG_BASE + 17)	/* SQI speed         */
#define DRX_CTRL_CFG_MAX            ( DRX_CFG_BASE + 18)	/* never to be used  */

#define DRX_CFG_PINS_SAFE_MODE      DRX_CFG_PINSAFE
/*============================================================================*/
/*============================================================================*/
/*== CTRL related data structures ============================================*/
/*============================================================================*/
/*============================================================================*/

/**
* \struct DRXUCodeInfo_t
* \brief Parameters for microcode upload and verfiy.
*
* Used by DRX_CTRL_LOAD_UCODE and DRX_CTRL_VERIFY_UCODE
*/
	typedef struct {
		pu8_t mcData;
		     /**< Pointer to microcode image. */
		u16_t mcSize;
		     /**< Microcode image size.       */
	} DRXUCodeInfo_t, *pDRXUCodeInfo_t;

/**
* \struct DRXMcVersionRec_t
* \brief Microcode version record
* Version numbers are stored in BCD format, as usual:
*   o major number = bits 31-20 (first three nibbles of MSW)
*   o minor number = bits 19-16 (fourth nibble of MSW)
*   o patch number = bits 15-0  (remaining nibbles in LSW)
*
* The device type indicates for which the device is meant. It is based on the
* JTAG ID, using everything except the bond ID and the metal fix.
*
* Special values:
* - mcDevType == 0         => any device allowed
* - mcBaseVersion == 0.0.0 => full microcode (mcVersion is the version)
* - mcBaseVersion != 0.0.0 => patch microcode, the base microcode version
*                             (mcVersion is the version)
*/
#define AUX_VER_RECORD 0x8000

	typedef struct {
		u16_t auxType;	/* type of aux data - 0x8000 for version record     */
		u32_t mcDevType;	/* device type, based on JTAG ID                    */
		u32_t mcVersion;	/* version of microcode                             */
		u32_t mcBaseVersion;	/* in case of patch: the original microcode version */
	} DRXMcVersionRec_t, *pDRXMcVersionRec_t;

/*========================================*/

/**
* \struct DRXFilterInfo_t
* \brief Parameters for loading filter coefficients
*
* Used by DRX_CTRL_LOAD_FILTER
*/
	typedef struct {
		pu8_t dataRe;
		      /**< pointer to coefficients for RE */
		pu8_t dataIm;
		      /**< pointer to coefficients for IM */
		u16_t sizeRe;
		      /**< size of coefficients for RE    */
		u16_t sizeIm;
		      /**< size of coefficients for IM    */
	} DRXFilterInfo_t, *pDRXFilterInfo_t;

/*========================================*/

/**
* \struct DRXChannel_t
* \brief The set of parameters describing a single channel.
*
* Used by DRX_CTRL_SET_CHANNEL and DRX_CTRL_GET_CHANNEL.
* Only certain fields need to be used for a specfic standard.
*
*/
	typedef struct {
		DRXFrequency_t frequency;
					/**< frequency in kHz                 */
		DRXBandwidth_t bandwidth;
					/**< bandwidth                        */
		DRXMirror_t mirror;	/**< mirrored or not on RF            */
		DRXConstellation_t constellation;
					/**< constellation                    */
		DRXHierarchy_t hierarchy;
					/**< hierarchy                        */
		DRXPriority_t priority;	/**< priority                         */
		DRXCoderate_t coderate;	/**< coderate                         */
		DRXGuard_t guard;	/**< guard interval                   */
		DRXFftmode_t fftmode;	/**< fftmode                          */
		DRXClassification_t classification;
					/**< classification                   */
		DRXSymbolrate_t symbolrate;
					/**< symbolrate in symbols/sec        */
		DRXInterleaveModes_t interleavemode;
					/**< interleaveMode QAM               */
		DRXLDPC_t ldpc;		/**< ldpc                             */
		DRXCarrier_t carrier;	/**< carrier                          */
		DRXFramemode_t framemode;
					/**< frame mode                       */
		DRXPilotMode_t pilot;	/**< pilot mode                       */
	} DRXChannel_t, *pDRXChannel_t;

/*========================================*/

/**
* \struct DRXSigQuality_t
* Signal quality metrics.
*
* Used by DRX_CTRL_SIG_QUALITY.
*/
	typedef struct {
		u16_t MER;     /**< in steps of 0.1 dB                        */
		u32_t preViterbiBER;
			       /**< in steps of 1/scaleFactorBER              */
		u32_t postViterbiBER;
			       /**< in steps of 1/scaleFactorBER              */
		u32_t scaleFactorBER;
			       /**< scale factor for BER                      */
		u16_t packetError;
			       /**< number of packet errors                   */
		u32_t postReedSolomonBER;
			       /**< in steps of 1/scaleFactorBER              */
		u32_t preLdpcBER;
			       /**< in steps of 1/scaleFactorBER              */
		u32_t averIter;/**< in steps of 0.01                          */
		u16_t indicator;
			       /**< indicative signal quality low=0..100=high */
	} DRXSigQuality_t, *pDRXSigQuality_t;

	typedef enum {
		DRX_SQI_SPEED_FAST = 0,
		DRX_SQI_SPEED_MEDIUM,
		DRX_SQI_SPEED_SLOW,
		DRX_SQI_SPEED_UNKNOWN = DRX_UNKNOWN
	} DRXCfgSqiSpeed_t, *pDRXCfgSqiSpeed_t;

/*========================================*/

/**
* \struct DRXComplex_t
* A complex number.
*
* Used by DRX_CTRL_CONSTEL.
*/
	typedef struct {
		s16_t im;
	     /**< Imaginary part. */
		s16_t re;
	     /**< Real part.      */
	} DRXComplex_t, *pDRXComplex_t;

/*========================================*/

/**
* \struct DRXFrequencyPlan_t
* Array element of a frequency plan.
*
* Used by DRX_CTRL_SCAN_INIT.
*/
	typedef struct {
		DRXFrequency_t first;
			     /**< First centre frequency in this band        */
		DRXFrequency_t last;
			     /**< Last centre frequency in this band         */
		DRXFrequency_t step;
			     /**< Stepping frequency in this band            */
		DRXBandwidth_t bandwidth;
			     /**< Bandwidth within this frequency band       */
		u16_t chNumber;
			     /**< First channel number in this band, or first
				    index in chNames                         */
		char **chNames;
			     /**< Optional list of channel names in this
				    band                                     */
	} DRXFrequencyPlan_t, *pDRXFrequencyPlan_t;

/*========================================*/

/**
* \struct DRXFrequencyPlanInfo_t
* Array element of a list of frequency plans.
*
* Used by frequency_plan.h
*/
	typedef struct {
		pDRXFrequencyPlan_t freqPlan;
		int freqPlanSize;
		char *freqPlanName;
	} DRXFrequencyPlanInfo_t, *pDRXFrequencyPlanInfo_t;

/*========================================*/

/**
* /struct DRXScanDataQam_t
* QAM specific scanning variables
*/
	typedef struct {
		pu32_t symbolrate;	  /**<  list of symbolrates to scan   */
		u16_t symbolrateSize;	  /**<  size of symbolrate array      */
		pDRXConstellation_t constellation;
					  /**<  list of constellations        */
		u16_t constellationSize;    /**<  size of constellation array */
		u16_t ifAgcThreshold;	  /**<  thresholf for IF-AGC based
						scanning filter               */
	} DRXScanDataQam_t, *pDRXScanDataQam_t;

/*========================================*/

/**
* /struct DRXScanDataAtv_t
* ATV specific scanning variables
*/
	typedef struct {
		s16_t svrThreshold;
			/**< threshold of Sound/Video ratio in 0.1dB steps */
	} DRXScanDataAtv_t, *pDRXScanDataAtv_t;

/*========================================*/

/**
* \struct DRXScanParam_t
* Parameters for channel scan.
*
* Used by DRX_CTRL_SCAN_INIT.
*/
	typedef struct {
		pDRXFrequencyPlan_t frequencyPlan;
					  /**< Frequency plan (array)*/
		u16_t frequencyPlanSize;  /**< Number of bands       */
		u32_t numTries;		  /**< Max channels tried    */
		DRXFrequency_t skip;	  /**< Minimum frequency step to take
						after a channel is found */
		void *extParams;	  /**< Standard specific params */
	} DRXScanParam_t, *pDRXScanParam_t;

/*========================================*/

/**
* \brief Scan commands.
* Used by scanning algorithms.
*/
	typedef enum {
		DRX_SCAN_COMMAND_INIT = 0,/**< Initialize scanning */
		DRX_SCAN_COMMAND_NEXT,	  /**< Next scan           */
		DRX_SCAN_COMMAND_STOP	  /**< Stop scanning       */
	} DRXScanCommand_t, *pDRXScanCommand_t;

/*========================================*/

/**
* \brief Inner scan function prototype.
*/
	typedef DRXStatus_t(*DRXScanFunc_t) (void *scanContext,
					     DRXScanCommand_t scanCommand,
					     pDRXChannel_t scanChannel,
					     pBool_t getNextChannel);

/*========================================*/

/**
* \struct DRXTPSInfo_t
* TPS information, DVB-T specific.
*
* Used by DRX_CTRL_TPS_INFO.
*/
	typedef struct {
		DRXFftmode_t fftmode;	/**< Fft mode       */
		DRXGuard_t guard;	/**< Guard interval */
		DRXConstellation_t constellation;
					/**< Constellation  */
		DRXHierarchy_t hierarchy;
					/**< Hierarchy      */
		DRXCoderate_t highCoderate;
					/**< High code rate */
		DRXCoderate_t lowCoderate;
					/**< Low cod rate   */
		DRXTPSFrame_t frame;	/**< Tps frame      */
		u8_t length;		/**< Length         */
		u16_t cellId;		/**< Cell id        */
	} DRXTPSInfo_t, *pDRXTPSInfo_t;

/*========================================*/

/**
* \brief Power mode of device.
*
* Used by DRX_CTRL_SET_POWER_MODE.
*/
	typedef enum {
		DRX_POWER_UP = 0,
			 /**< Generic         , Power Up Mode   */
		DRX_POWER_MODE_1,
			 /**< Device specific , Power Up Mode   */
		DRX_POWER_MODE_2,
			 /**< Device specific , Power Up Mode   */
		DRX_POWER_MODE_3,
			 /**< Device specific , Power Up Mode   */
		DRX_POWER_MODE_4,
			 /**< Device specific , Power Up Mode   */
		DRX_POWER_MODE_5,
			 /**< Device specific , Power Up Mode   */
		DRX_POWER_MODE_6,
			 /**< Device specific , Power Up Mode   */
		DRX_POWER_MODE_7,
			 /**< Device specific , Power Up Mode   */
		DRX_POWER_MODE_8,
			 /**< Device specific , Power Up Mode   */

		DRX_POWER_MODE_9,
			 /**< Device specific , Power Down Mode */
		DRX_POWER_MODE_10,
			 /**< Device specific , Power Down Mode */
		DRX_POWER_MODE_11,
			 /**< Device specific , Power Down Mode */
		DRX_POWER_MODE_12,
			 /**< Device specific , Power Down Mode */
		DRX_POWER_MODE_13,
			 /**< Device specific , Power Down Mode */
		DRX_POWER_MODE_14,
			 /**< Device specific , Power Down Mode */
		DRX_POWER_MODE_15,
			 /**< Device specific , Power Down Mode */
		DRX_POWER_MODE_16,
			 /**< Device specific , Power Down Mode */
		DRX_POWER_DOWN = 255
			 /**< Generic         , Power Down Mode */
	} DRXPowerMode_t, *pDRXPowerMode_t;

/*========================================*/

/**
* \enum DRXModule_t
* \brief Software module identification.
*
* Used by DRX_CTRL_VERSION.
*/
	typedef enum {
		DRX_MODULE_DEVICE,
		DRX_MODULE_MICROCODE,
		DRX_MODULE_DRIVERCORE,
		DRX_MODULE_DEVICEDRIVER,
		DRX_MODULE_DAP,
		DRX_MODULE_BSP_I2C,
		DRX_MODULE_BSP_TUNER,
		DRX_MODULE_BSP_HOST,
		DRX_MODULE_UNKNOWN
	} DRXModule_t, *pDRXModule_t;

/**
* \enum DRXVersion_t
* \brief Version information of one software module.
*
* Used by DRX_CTRL_VERSION.
*/
	typedef struct {
		DRXModule_t moduleType;
			       /**< Type identifier of the module */
		char *moduleName;
			       /**< Name or description of module */
		u16_t vMajor;  /**< Major version number          */
		u16_t vMinor;  /**< Minor version number          */
		u16_t vPatch;  /**< Patch version number          */
		char *vString; /**< Version as text string        */
	} DRXVersion_t, *pDRXVersion_t;

/**
* \enum DRXVersionList_t
* \brief List element of NULL terminated, linked list for version information.
*
* Used by DRX_CTRL_VERSION.
*/
	typedef struct DRXVersionList_s {
		pDRXVersion_t version;/**< Version information */
		struct DRXVersionList_s *next;
				      /**< Next list element   */
	} DRXVersionList_t, *pDRXVersionList_t;

/*========================================*/

/**
* \brief Parameters needed to confiugure a UIO.
*
* Used by DRX_CTRL_UIO_CFG.
*/
	typedef struct {
		DRXUIO_t uio;
		       /**< UIO identifier       */
		DRXUIOMode_t mode;
		       /**< UIO operational mode */
	} DRXUIOCfg_t, *pDRXUIOCfg_t;

/*========================================*/

/**
* \brief Parameters needed to read from or write to a UIO.
*
* Used by DRX_CTRL_UIO_READ and DRX_CTRL_UIO_WRITE.
*/
	typedef struct {
		DRXUIO_t uio;
		   /**< UIO identifier              */
		Bool_t value;
		   /**< UIO value (TRUE=1, FALSE=0) */
	} DRXUIOData_t, *pDRXUIOData_t;

/*========================================*/

/**
* \brief Parameters needed to configure OOB.
*
* Used by DRX_CTRL_SET_OOB.
*/
	typedef struct {
		DRXFrequency_t frequency;	   /**< Frequency in kHz      */
		DRXOOBDownstreamStandard_t standard;
						   /**< OOB standard          */
		Bool_t spectrumInverted;	   /**< If TRUE, then spectrum
							 is inverted          */
	} DRXOOB_t, *pDRXOOB_t;

/*========================================*/

/**
* \brief Metrics from OOB.
*
* Used by DRX_CTRL_GET_OOB.
*/
	typedef struct {
		DRXFrequency_t frequency; /**< Frequency in Khz         */
		DRXLockStatus_t lock;	  /**< Lock status              */
		u32_t mer;		  /**< MER                      */
		s32_t symbolRateOffset;	  /**< Symbolrate offset in ppm */
	} DRXOOBStatus_t, *pDRXOOBStatus_t;

/*========================================*/

/**
* \brief Device dependent configuration data.
*
* Used by DRX_CTRL_SET_CFG and DRX_CTRL_GET_CFG.
* A sort of nested DRX_Ctrl() functionality for device specific controls.
*/
	typedef struct {
		DRXCfgType_t cfgType;
			  /**< Function identifier */
		void *cfgData;
			  /**< Function data */
	} DRXCfg_t, *pDRXCfg_t;

/*========================================*/

/**
* /struct DRXMpegStartWidth_t
* MStart width [nr MCLK cycles] for serial MPEG output.
*/

	typedef enum {
		DRX_MPEG_STR_WIDTH_1,
		DRX_MPEG_STR_WIDTH_8
	} DRXMPEGStrWidth_t, *pDRXMPEGStrWidth_t;

/* CTRL CFG MPEG ouput */
/**
* \struct DRXCfgMPEGOutput_t
* \brief Configuartion parameters for MPEG output control.
*
* Used by DRX_CFG_MPEG_OUTPUT, in combination with DRX_CTRL_SET_CFG and
* DRX_CTRL_GET_CFG.
*/

	typedef struct {
		Bool_t enableMPEGOutput;/**< If TRUE, enable MPEG output      */
		Bool_t insertRSByte;	/**< If TRUE, insert RS byte          */
		Bool_t enableParallel;	/**< If TRUE, parallel out otherwise
								     serial   */
		Bool_t invertDATA;	/**< If TRUE, invert DATA signals     */
		Bool_t invertERR;	/**< If TRUE, invert ERR signal       */
		Bool_t invertSTR;	/**< If TRUE, invert STR signals      */
		Bool_t invertVAL;	/**< If TRUE, invert VAL signals      */
		Bool_t invertCLK;	/**< If TRUE, invert CLK signals      */
		Bool_t staticCLK;	/**< If TRUE, static MPEG clockrate
					     will be used, otherwise clockrate
					     will adapt to the bitrate of the
					     TS                               */
		u32_t bitrate;		/**< Maximum bitrate in b/s in case
					     static clockrate is selected     */
		DRXMPEGStrWidth_t widthSTR;
					/**< MPEG start width                 */
	} DRXCfgMPEGOutput_t, *pDRXCfgMPEGOutput_t;

/* CTRL CFG SMA */
/**
* /struct DRXCfgSMAIO_t
* smart antenna i/o.
*/
	typedef enum DRXCfgSMAIO_t {
		DRX_SMA_OUTPUT = 0,
		DRX_SMA_INPUT
	} DRXCfgSMAIO_t, *pDRXCfgSMAIO_t;

/**
* /struct DRXCfgSMA_t
* Set smart antenna.
*/
	typedef struct {
		DRXCfgSMAIO_t io;
		u16_t ctrlData;
		Bool_t smartAntInverted;
	} DRXCfgSMA_t, *pDRXCfgSMA_t;

/*========================================*/

/**
* \struct DRXI2CData_t
* \brief Data for I2C via 2nd or 3rd or etc I2C port.
*
* Used by DRX_CTRL_I2C_READWRITE.
* If portNr is equal to primairy portNr BSPI2C will be used.
*
*/
	typedef struct {
		u16_t portNr;	/**< I2C port number               */
		struct i2c_device_addr *wDevAddr;
				/**< Write device address          */
		u16_t wCount;	/**< Size of write data in bytes   */
		pu8_t wData;	/**< Pointer to write data         */
		struct i2c_device_addr *rDevAddr;
				/**< Read device address           */
		u16_t rCount;	/**< Size of data to read in bytes */
		pu8_t rData;	/**< Pointer to read buffer        */
	} DRXI2CData_t, *pDRXI2CData_t;

/*========================================*/

/**
* \enum DRXAudStandard_t
* \brief Audio standard identifier.
*
* Used by DRX_CTRL_SET_AUD.
*/
	typedef enum {
		DRX_AUD_STANDARD_BTSC,	   /**< set BTSC standard (USA)       */
		DRX_AUD_STANDARD_A2,	   /**< set A2-Korea FM Stereo        */
		DRX_AUD_STANDARD_EIAJ,	   /**< set to Japanese FM Stereo     */
		DRX_AUD_STANDARD_FM_STEREO,/**< set to FM-Stereo Radio        */
		DRX_AUD_STANDARD_M_MONO,   /**< for 4.5 MHz mono detected     */
		DRX_AUD_STANDARD_D_K_MONO, /**< for 6.5 MHz mono detected     */
		DRX_AUD_STANDARD_BG_FM,	   /**< set BG_FM standard            */
		DRX_AUD_STANDARD_D_K1,	   /**< set D_K1 standard             */
		DRX_AUD_STANDARD_D_K2,	   /**< set D_K2 standard             */
		DRX_AUD_STANDARD_D_K3,	   /**< set D_K3 standard             */
		DRX_AUD_STANDARD_BG_NICAM_FM,
					   /**< set BG_NICAM_FM standard      */
		DRX_AUD_STANDARD_L_NICAM_AM,
					   /**< set L_NICAM_AM standard       */
		DRX_AUD_STANDARD_I_NICAM_FM,
					   /**< set I_NICAM_FM standard       */
		DRX_AUD_STANDARD_D_K_NICAM_FM,
					   /**< set D_K_NICAM_FM standard     */
		DRX_AUD_STANDARD_NOT_READY,/**< used to detect audio standard */
		DRX_AUD_STANDARD_AUTO = DRX_AUTO,
					   /**< Automatic Standard Detection  */
		DRX_AUD_STANDARD_UNKNOWN = DRX_UNKNOWN
					   /**< used as auto and for readback */
	} DRXAudStandard_t, *pDRXAudStandard_t;

/* CTRL_AUD_GET_STATUS    - DRXAudStatus_t */
/**
* \enum DRXAudNICAMStatus_t
* \brief Status of NICAM carrier.
*/
	typedef enum {
		DRX_AUD_NICAM_DETECTED = 0,
					  /**< NICAM carrier detected         */
		DRX_AUD_NICAM_NOT_DETECTED,
					  /**< NICAM carrier not detected     */
		DRX_AUD_NICAM_BAD	  /**< NICAM carrier bad quality      */
	} DRXAudNICAMStatus_t, *pDRXAudNICAMStatus_t;

/**
* \struct DRXAudStatus_t
* \brief Audio status characteristics.
*/
	typedef struct {
		Bool_t stereo;		  /**< stereo detection               */
		Bool_t carrierA;	  /**< carrier A detected             */
		Bool_t carrierB;	  /**< carrier B detected             */
		Bool_t sap;		  /**< sap / bilingual detection      */
		Bool_t rds;		  /**< RDS data array present         */
		DRXAudNICAMStatus_t nicamStatus;
					  /**< status of NICAM carrier        */
		s8_t fmIdent;		  /**< FM Identification value        */
	} DRXAudStatus_t, *pDRXAudStatus_t;

/* CTRL_AUD_READ_RDS       - DRXRDSdata_t */

/**
* \struct DRXRDSdata_t
* \brief Raw RDS data array.
*/
	typedef struct {
		Bool_t valid;		  /**< RDS data validation            */
		u16_t data[18];		  /**< data from one RDS data array   */
	} DRXCfgAudRDS_t, *pDRXCfgAudRDS_t;

/* DRX_CFG_AUD_VOLUME      - DRXCfgAudVolume_t - set/get */
/**
* \enum DRXAudAVCDecayTime_t
* \brief Automatic volume control configuration.
*/
	typedef enum {
		DRX_AUD_AVC_OFF,	  /**< Automatic volume control off   */
		DRX_AUD_AVC_DECAYTIME_8S, /**< level volume in  8 seconds     */
		DRX_AUD_AVC_DECAYTIME_4S, /**< level volume in  4 seconds     */
		DRX_AUD_AVC_DECAYTIME_2S, /**< level volume in  2 seconds     */
		DRX_AUD_AVC_DECAYTIME_20MS/**< level volume in 20 millisec    */
	} DRXAudAVCMode_t, *pDRXAudAVCMode_t;

/**
* /enum DRXAudMaxAVCGain_t
* /brief Automatic volume control max gain in audio baseband.
*/
	typedef enum {
		DRX_AUD_AVC_MAX_GAIN_0DB, /**< maximum AVC gain  0 dB         */
		DRX_AUD_AVC_MAX_GAIN_6DB, /**< maximum AVC gain  6 dB         */
		DRX_AUD_AVC_MAX_GAIN_12DB /**< maximum AVC gain 12 dB         */
	} DRXAudAVCMaxGain_t, *pDRXAudAVCMaxGain_t;

/**
* /enum DRXAudMaxAVCAtten_t
* /brief Automatic volume control max attenuation in audio baseband.
*/
	typedef enum {
		DRX_AUD_AVC_MAX_ATTEN_12DB,
					  /**< maximum AVC attenuation 12 dB  */
		DRX_AUD_AVC_MAX_ATTEN_18DB,
					  /**< maximum AVC attenuation 18 dB  */
		DRX_AUD_AVC_MAX_ATTEN_24DB/**< maximum AVC attenuation 24 dB  */
	} DRXAudAVCMaxAtten_t, *pDRXAudAVCMaxAtten_t;
/**
* \struct DRXCfgAudVolume_t
* \brief Audio volume configuration.
*/
	typedef struct {
		Bool_t mute;		  /**< mute overrides volume setting  */
		s16_t volume;		  /**< volume, range -114 to 12 dB    */
		DRXAudAVCMode_t avcMode;  /**< AVC auto volume control mode   */
		u16_t avcRefLevel;	  /**< AVC reference level            */
		DRXAudAVCMaxGain_t avcMaxGain;
					  /**< AVC max gain selection         */
		DRXAudAVCMaxAtten_t avcMaxAtten;
					  /**< AVC max attenuation selection  */
		s16_t strengthLeft;	  /**< quasi-peak, left speaker       */
		s16_t strengthRight;	  /**< quasi-peak, right speaker      */
	} DRXCfgAudVolume_t, *pDRXCfgAudVolume_t;

/* DRX_CFG_I2S_OUTPUT      - DRXCfgI2SOutput_t - set/get */
/**
* \enum DRXI2SMode_t
* \brief I2S output mode.
*/
	typedef enum {
		DRX_I2S_MODE_MASTER,	  /**< I2S is in master mode          */
		DRX_I2S_MODE_SLAVE	  /**< I2S is in slave mode           */
	} DRXI2SMode_t, *pDRXI2SMode_t;

/**
* \enum DRXI2SWordLength_t
* \brief Width of I2S data.
*/
	typedef enum {
		DRX_I2S_WORDLENGTH_32 = 0,/**< I2S data is 32 bit wide        */
		DRX_I2S_WORDLENGTH_16 = 1 /**< I2S data is 16 bit wide        */
	} DRXI2SWordLength_t, *pDRXI2SWordLength_t;

/**
* \enum DRXI2SFormat_t
* \brief Data wordstrobe alignment for I2S.
*/
	typedef enum {
		DRX_I2S_FORMAT_WS_WITH_DATA,
				    /**< I2S data and wordstrobe are aligned  */
		DRX_I2S_FORMAT_WS_ADVANCED
				    /**< I2S data one cycle after wordstrobe  */
	} DRXI2SFormat_t, *pDRXI2SFormat_t;

/**
* \enum DRXI2SPolarity_t
* \brief Polarity of I2S data.
*/
	typedef enum {
		DRX_I2S_POLARITY_RIGHT,/**< wordstrobe - right high, left low */
		DRX_I2S_POLARITY_LEFT  /**< wordstrobe - right low, left high */
	} DRXI2SPolarity_t, *pDRXI2SPolarity_t;

/**
* \struct DRXCfgI2SOutput_t
* \brief I2S output configuration.
*/
	typedef struct {
		Bool_t outputEnable;	  /**< I2S output enable              */
		u32_t frequency;	  /**< range from 8000-48000 Hz       */
		DRXI2SMode_t mode;	  /**< I2S mode, master or slave      */
		DRXI2SWordLength_t wordLength;
					  /**< I2S wordlength, 16 or 32 bits  */
		DRXI2SPolarity_t polarity;/**< I2S wordstrobe polarity        */
		DRXI2SFormat_t format;	  /**< I2S wordstrobe delay to data   */
	} DRXCfgI2SOutput_t, *pDRXCfgI2SOutput_t;

/* ------------------------------expert interface-----------------------------*/
/**
* /enum DRXAudFMDeemphasis_t
* setting for FM-Deemphasis in audio demodulator.
*
*/
	typedef enum {
		DRX_AUD_FM_DEEMPH_50US,
		DRX_AUD_FM_DEEMPH_75US,
		DRX_AUD_FM_DEEMPH_OFF
	} DRXAudFMDeemphasis_t, *pDRXAudFMDeemphasis_t;

/**
* /enum DRXAudDeviation_t
* setting for deviation mode in audio demodulator.
*
*/
	typedef enum {
		DRX_AUD_DEVIATION_NORMAL,
		DRX_AUD_DEVIATION_HIGH
	} DRXCfgAudDeviation_t, *pDRXCfgAudDeviation_t;

/**
* /enum DRXNoCarrierOption_t
* setting for carrier, mute/noise.
*
*/
	typedef enum {
		DRX_NO_CARRIER_MUTE,
		DRX_NO_CARRIER_NOISE
	} DRXNoCarrierOption_t, *pDRXNoCarrierOption_t;

/**
* \enum DRXAudAutoSound_t
* \brief Automatic Sound
*/
	typedef enum {
		DRX_AUD_AUTO_SOUND_OFF = 0,
		DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_ON,
		DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_OFF
	} DRXCfgAudAutoSound_t, *pDRXCfgAudAutoSound_t;

/**
* \enum DRXAudASSThres_t
* \brief Automatic Sound Select Thresholds
*/
	typedef struct {
		u16_t a2;	/* A2 Threshold for ASS configuration */
		u16_t btsc;	/* BTSC Threshold for ASS configuration */
		u16_t nicam;	/* Nicam Threshold for ASS configuration */
	} DRXCfgAudASSThres_t, *pDRXCfgAudASSThres_t;

/**
* \struct DRXAudCarrier_t
* \brief Carrier detection related parameters
*/
	typedef struct {
		u16_t thres;	/* carrier detetcion threshold for primary carrier (A) */
		DRXNoCarrierOption_t opt;	/* Mute or noise at no carrier detection (A) */
		DRXFrequency_t shift;	/* DC level of incoming signal (A) */
		DRXFrequency_t dco;	/* frequency adjustment (A) */
	} DRXAudCarrier_t, *pDRXCfgAudCarrier_t;

/**
* \struct DRXCfgAudCarriers_t
* \brief combining carrier A & B to one struct
*/
	typedef struct {
		DRXAudCarrier_t a;
		DRXAudCarrier_t b;
	} DRXCfgAudCarriers_t, *pDRXCfgAudCarriers_t;

/**
* /enum DRXAudI2SSrc_t
* Selection of audio source
*/
	typedef enum {
		DRX_AUD_SRC_MONO,
		DRX_AUD_SRC_STEREO_OR_AB,
		DRX_AUD_SRC_STEREO_OR_A,
		DRX_AUD_SRC_STEREO_OR_B
	} DRXAudI2SSrc_t, *pDRXAudI2SSrc_t;

/**
* \enum DRXAudI2SMatrix_t
* \brief Used for selecting I2S output.
*/
	typedef enum {
		DRX_AUD_I2S_MATRIX_A_MONO,
					/**< A sound only, stereo or mono     */
		DRX_AUD_I2S_MATRIX_B_MONO,
					/**< B sound only, stereo or mono     */
		DRX_AUD_I2S_MATRIX_STEREO,
					/**< A+B sound, transparant           */
		DRX_AUD_I2S_MATRIX_MONO	/**< A+B mixed to mono sum, (L+R)/2   */
	} DRXAudI2SMatrix_t, *pDRXAudI2SMatrix_t;

/**
* /enum DRXAudFMMatrix_t
* setting for FM-Matrix in audio demodulator.
*
*/
	typedef enum {
		DRX_AUD_FM_MATRIX_NO_MATRIX,
		DRX_AUD_FM_MATRIX_GERMAN,
		DRX_AUD_FM_MATRIX_KOREAN,
		DRX_AUD_FM_MATRIX_SOUND_A,
		DRX_AUD_FM_MATRIX_SOUND_B
	} DRXAudFMMatrix_t, *pDRXAudFMMatrix_t;

/**
* \struct DRXAudMatrices_t
* \brief Mixer settings
*/
	typedef struct {
		DRXAudI2SSrc_t sourceI2S;
		DRXAudI2SMatrix_t matrixI2S;
		DRXAudFMMatrix_t matrixFm;
	} DRXCfgAudMixer_t, *pDRXCfgAudMixer_t;

/**
* \enum DRXI2SVidSync_t
* \brief Audio/video synchronization, interacts with I2S mode.
* AUTO_1 and AUTO_2 are for automatic video standard detection with preference
* for NTSC or Monochrome, because the frequencies are too close (59.94 & 60 Hz)
*/
	typedef enum {
		DRX_AUD_AVSYNC_OFF,/**< audio/video synchronization is off   */
		DRX_AUD_AVSYNC_NTSC,
				   /**< it is an NTSC system                 */
		DRX_AUD_AVSYNC_MONOCHROME,
				   /**< it is a MONOCHROME system            */
		DRX_AUD_AVSYNC_PAL_SECAM
				   /**< it is a PAL/SECAM system             */
	} DRXCfgAudAVSync_t, *pDRXCfgAudAVSync_t;

/**
* \struct DRXCfgAudPrescale_t
* \brief Prescalers
*/
	typedef struct {
		u16_t fmDeviation;
		s16_t nicamGain;
	} DRXCfgAudPrescale_t, *pDRXCfgAudPrescale_t;

/**
* \struct DRXAudBeep_t
* \brief Beep
*/
	typedef struct {
		s16_t volume;	/* dB */
		u16_t frequency;	/* Hz */
		Bool_t mute;
	} DRXAudBeep_t, *pDRXAudBeep_t;

/**
* \enum DRXAudBtscDetect_t
* \brief BTSC detetcion mode
*/
	typedef enum {
		DRX_BTSC_STEREO,
		DRX_BTSC_MONO_AND_SAP
	} DRXAudBtscDetect_t, *pDRXAudBtscDetect_t;

/**
* \struct DRXAudData_t
* \brief Audio data structure
*/
	typedef struct {
		/* audio storage */
		Bool_t audioIsActive;
		DRXAudStandard_t audioStandard;
		DRXCfgI2SOutput_t i2sdata;
		DRXCfgAudVolume_t volume;
		DRXCfgAudAutoSound_t autoSound;
		DRXCfgAudASSThres_t assThresholds;
		DRXCfgAudCarriers_t carriers;
		DRXCfgAudMixer_t mixer;
		DRXCfgAudDeviation_t deviation;
		DRXCfgAudAVSync_t avSync;
		DRXCfgAudPrescale_t prescale;
		DRXAudFMDeemphasis_t deemph;
		DRXAudBtscDetect_t btscDetect;
		/* rds */
		u16_t rdsDataCounter;
		Bool_t rdsDataPresent;
	} DRXAudData_t, *pDRXAudData_t;

/**
* \enum DRXQamLockRange_t
* \brief QAM lock range mode
*/
	typedef enum {
		DRX_QAM_LOCKRANGE_NORMAL,
		DRX_QAM_LOCKRANGE_EXTENDED
	} DRXQamLockRange_t, *pDRXQamLockRange_t;

/*============================================================================*/
/*============================================================================*/
/*== Data access structures ==================================================*/
/*============================================================================*/
/*============================================================================*/

/* Address on device */
	typedef u32_t DRXaddr_t, *pDRXaddr_t;

/* Protocol specific flags */
	typedef u32_t DRXflags_t, *pDRXflags_t;

/* Write block of data to device */
	typedef DRXStatus_t(*DRXWriteBlockFunc_t) (struct i2c_device_addr *devAddr,	/* address of I2C device        */
						   DRXaddr_t addr,	/* address of register/memory   */
						   u16_t datasize,	/* size of data in bytes        */
						   pu8_t data,	/* data to send                 */
						   DRXflags_t flags);

/* Read block of data from device */
	typedef DRXStatus_t(*DRXReadBlockFunc_t) (struct i2c_device_addr *devAddr,	/* address of I2C device        */
						  DRXaddr_t addr,	/* address of register/memory   */
						  u16_t datasize,	/* size of data in bytes        */
						  pu8_t data,	/* receive buffer               */
						  DRXflags_t flags);

/* Write 8-bits value to device */
	typedef DRXStatus_t(*DRXWriteReg8Func_t) (struct i2c_device_addr *devAddr,	/* address of I2C device        */
						  DRXaddr_t addr,	/* address of register/memory   */
						  u8_t data,	/* data to send                 */
						  DRXflags_t flags);

/* Read 8-bits value to device */
	typedef DRXStatus_t(*DRXReadReg8Func_t) (struct i2c_device_addr *devAddr,	/* address of I2C device        */
						 DRXaddr_t addr,	/* address of register/memory   */
						 pu8_t data,	/* receive buffer               */
						 DRXflags_t flags);

/* Read modify write 8-bits value to device */
	typedef DRXStatus_t(*DRXReadModifyWriteReg8Func_t) (struct i2c_device_addr *devAddr,	/* address of I2C device       */
							    DRXaddr_t waddr,	/* write address of register   */
							    DRXaddr_t raddr,	/* read  address of register   */
							    u8_t wdata,	/* data to write               */
							    pu8_t rdata);	/* data to read                */

/* Write 16-bits value to device */
	typedef DRXStatus_t(*DRXWriteReg16Func_t) (struct i2c_device_addr *devAddr,	/* address of I2C device        */
						   DRXaddr_t addr,	/* address of register/memory   */
						   u16_t data,	/* data to send                 */
						   DRXflags_t flags);

/* Read 16-bits value to device */
	typedef DRXStatus_t(*DRXReadReg16Func_t) (struct i2c_device_addr *devAddr,	/* address of I2C device        */
						  DRXaddr_t addr,	/* address of register/memory   */
						  pu16_t data,	/* receive buffer               */
						  DRXflags_t flags);

/* Read modify write 16-bits value to device */
	typedef DRXStatus_t(*DRXReadModifyWriteReg16Func_t) (struct i2c_device_addr *devAddr,	/* address of I2C device       */
							     DRXaddr_t waddr,	/* write address of register   */
							     DRXaddr_t raddr,	/* read  address of register   */
							     u16_t wdata,	/* data to write               */
							     pu16_t rdata);	/* data to read                */

/* Write 32-bits value to device */
	typedef DRXStatus_t(*DRXWriteReg32Func_t) (struct i2c_device_addr *devAddr,	/* address of I2C device        */
						   DRXaddr_t addr,	/* address of register/memory   */
						   u32_t data,	/* data to send                 */
						   DRXflags_t flags);

/* Read 32-bits value to device */
	typedef DRXStatus_t(*DRXReadReg32Func_t) (struct i2c_device_addr *devAddr,	/* address of I2C device        */
						  DRXaddr_t addr,	/* address of register/memory   */
						  pu32_t data,	/* receive buffer               */
						  DRXflags_t flags);

/* Read modify write 32-bits value to device */
	typedef DRXStatus_t(*DRXReadModifyWriteReg32Func_t) (struct i2c_device_addr *devAddr,	/* address of I2C device       */
							     DRXaddr_t waddr,	/* write address of register   */
							     DRXaddr_t raddr,	/* read  address of register   */
							     u32_t wdata,	/* data to write               */
							     pu32_t rdata);	/* data to read                */

/**
* \struct DRXAccessFunc_t
* \brief Interface to an access protocol.
*/
	typedef struct {
		pDRXVersion_t protocolVersion;
		DRXWriteBlockFunc_t writeBlockFunc;
		DRXReadBlockFunc_t readBlockFunc;
		DRXWriteReg8Func_t writeReg8Func;
		DRXReadReg8Func_t readReg8Func;
		DRXReadModifyWriteReg8Func_t readModifyWriteReg8Func;
		DRXWriteReg16Func_t writeReg16Func;
		DRXReadReg16Func_t readReg16Func;
		DRXReadModifyWriteReg16Func_t readModifyWriteReg16Func;
		DRXWriteReg32Func_t writeReg32Func;
		DRXReadReg32Func_t readReg32Func;
		DRXReadModifyWriteReg32Func_t readModifyWriteReg32Func;
	} DRXAccessFunc_t, *pDRXAccessFunc_t;

/* Register address and data for register dump function */
	typedef struct {

		DRXaddr_t address;
		u32_t data;

	} DRXRegDump_t, *pDRXRegDump_t;

/*============================================================================*/
/*============================================================================*/
/*== Demod instance data structures ==========================================*/
/*============================================================================*/
/*============================================================================*/

/**
* \struct DRXCommonAttr_t
* \brief Set of common attributes, shared by all DRX devices.
*/
	typedef struct {
		/* Microcode (firmware) attributes */
		pu8_t microcode;   /**< Pointer to microcode image.           */
		u16_t microcodeSize;
				   /**< Size of microcode image in bytes.     */
		Bool_t verifyMicrocode;
				   /**< Use microcode verify or not.          */
		DRXMcVersionRec_t mcversion;
				   /**< Version record of microcode from file */

		/* Clocks and tuner attributes */
		DRXFrequency_t intermediateFreq;
				     /**< IF,if tuner instance not used. (kHz)*/
		DRXFrequency_t sysClockFreq;
				     /**< Systemclock frequency.  (kHz)       */
		DRXFrequency_t oscClockFreq;
				     /**< Oscillator clock frequency.  (kHz)  */
		s16_t oscClockDeviation;
				     /**< Oscillator clock deviation.  (ppm)  */
		Bool_t mirrorFreqSpect;
				     /**< Mirror IF frequency spectrum or not.*/

		/* Initial MPEG output attributes */
		DRXCfgMPEGOutput_t mpegCfg;
				     /**< MPEG configuration                  */

		Bool_t isOpened;     /**< if TRUE instance is already opened. */

		/* Channel scan */
		pDRXScanParam_t scanParam;
				      /**< scan parameters                    */
		u16_t scanFreqPlanIndex;
				      /**< next index in freq plan            */
		DRXFrequency_t scanNextFrequency;
				      /**< next freq to scan                  */
		Bool_t scanReady;     /**< scan ready flag                    */
		u32_t scanMaxChannels;/**< number of channels in freqplan     */
		u32_t scanChannelsScanned;
					/**< number of channels scanned       */
		/* Channel scan - inner loop: demod related */
		DRXScanFunc_t scanFunction;
				      /**< function to check channel          */
		/* Channel scan - inner loop: SYSObj related */
		void *scanContext;    /**< Context Pointer of SYSObj          */
		/* Channel scan - parameters for default DTV scan function in core driver  */
		u16_t scanDemodLockTimeout;
					 /**< millisecs to wait for lock      */
		DRXLockStatus_t scanDesiredLock;
				      /**< lock requirement for channel found */
		/* scanActive can be used by SetChannel to decide how to program the tuner,
		   fast or slow (but stable). Usually fast during scan. */
		Bool_t scanActive;    /**< TRUE when scan routines are active */

		/* Power management */
		DRXPowerMode_t currentPowerMode;
				      /**< current power management mode      */

		/* Tuner */
		u8_t tunerPortNr;     /**< nr of I2C port to wich tuner is    */
		DRXFrequency_t tunerMinFreqRF;
				      /**< minimum RF input frequency, in kHz */
		DRXFrequency_t tunerMaxFreqRF;
				      /**< maximum RF input frequency, in kHz */
		Bool_t tunerRfAgcPol; /**< if TRUE invert RF AGC polarity     */
		Bool_t tunerIfAgcPol; /**< if TRUE invert IF AGC polarity     */
		Bool_t tunerSlowMode; /**< if TRUE invert IF AGC polarity     */

		DRXChannel_t currentChannel;
				      /**< current channel parameters         */
		DRXStandard_t currentStandard;
				      /**< current standard selection         */
		DRXStandard_t prevStandard;
				      /**< previous standard selection        */
		DRXStandard_t diCacheStandard;
				      /**< standard in DI cache if available  */
		Bool_t useBootloader; /**< use bootloader in open             */
		u32_t capabilities;   /**< capabilities flags                 */
		u32_t productId;      /**< product ID inc. metal fix number   */

	} DRXCommonAttr_t, *pDRXCommonAttr_t;

/*
* Generic functions for DRX devices.
*/
	typedef struct DRXDemodInstance_s *pDRXDemodInstance_t;

	typedef DRXStatus_t(*DRXOpenFunc_t) (pDRXDemodInstance_t demod);
	typedef DRXStatus_t(*DRXCloseFunc_t) (pDRXDemodInstance_t demod);
	typedef DRXStatus_t(*DRXCtrlFunc_t) (pDRXDemodInstance_t demod,
					     DRXCtrlIndex_t ctrl,
					     void *ctrlData);

/**
* \struct DRXDemodFunc_t
* \brief A stucture containing all functions of a demodulator.
*/
	typedef struct {
		u32_t typeId;		 /**< Device type identifier.      */
		DRXOpenFunc_t openFunc;	 /**< Pointer to Open() function.  */
		DRXCloseFunc_t closeFunc;/**< Pointer to Close() function. */
		DRXCtrlFunc_t ctrlFunc;	 /**< Pointer to Ctrl() function.  */
	} DRXDemodFunc_t, *pDRXDemodFunc_t;

/**
* \struct DRXDemodInstance_t
* \brief Top structure of demodulator instance.
*/
	typedef struct DRXDemodInstance_s {
		/* type specific demodulator data */
		pDRXDemodFunc_t myDemodFunct;
				    /**< demodulator functions                */
		pDRXAccessFunc_t myAccessFunct;
				    /**< data access protocol functions       */
		pTUNERInstance_t myTuner;
				    /**< tuner instance,if NULL then baseband */
		struct i2c_device_addr *myI2CDevAddr;
				    /**< i2c address and device identifier    */
		pDRXCommonAttr_t myCommonAttr;
				    /**< common DRX attributes                */
		void *myExtAttr;    /**< device specific attributes           */
		/* generic demodulator data */
	} DRXDemodInstance_t;

/*-------------------------------------------------------------------------
MACROS
Conversion from enum values to human readable form.
-------------------------------------------------------------------------*/

/* standard */

#define DRX_STR_STANDARD(x) ( \
   ( x == DRX_STANDARD_DVBT             )  ? "DVB-T"            : \
   ( x == DRX_STANDARD_8VSB             )  ? "8VSB"             : \
   ( x == DRX_STANDARD_NTSC             )  ? "NTSC"             : \
   ( x == DRX_STANDARD_PAL_SECAM_BG     )  ? "PAL/SECAM B/G"    : \
   ( x == DRX_STANDARD_PAL_SECAM_DK     )  ? "PAL/SECAM D/K"    : \
   ( x == DRX_STANDARD_PAL_SECAM_I      )  ? "PAL/SECAM I"      : \
   ( x == DRX_STANDARD_PAL_SECAM_L      )  ? "PAL/SECAM L"      : \
   ( x == DRX_STANDARD_PAL_SECAM_LP     )  ? "PAL/SECAM LP"     : \
   ( x == DRX_STANDARD_ITU_A            )  ? "ITU-A"            : \
   ( x == DRX_STANDARD_ITU_B            )  ? "ITU-B"            : \
   ( x == DRX_STANDARD_ITU_C            )  ? "ITU-C"            : \
   ( x == DRX_STANDARD_ITU_D            )  ? "ITU-D"            : \
   ( x == DRX_STANDARD_FM               )  ? "FM"               : \
   ( x == DRX_STANDARD_DTMB             )  ? "DTMB"             : \
   ( x == DRX_STANDARD_AUTO             )  ? "Auto"             : \
   ( x == DRX_STANDARD_UNKNOWN          )  ? "Unknown"          : \
					     "(Invalid)"  )

/* channel */

#define DRX_STR_BANDWIDTH(x) ( \
   ( x == DRX_BANDWIDTH_8MHZ           )  ?  "8 MHz"            : \
   ( x == DRX_BANDWIDTH_7MHZ           )  ?  "7 MHz"            : \
   ( x == DRX_BANDWIDTH_6MHZ           )  ?  "6 MHz"            : \
   ( x == DRX_BANDWIDTH_AUTO           )  ?  "Auto"             : \
   ( x == DRX_BANDWIDTH_UNKNOWN        )  ?  "Unknown"          : \
					     "(Invalid)"  )
#define DRX_STR_FFTMODE(x) ( \
   ( x == DRX_FFTMODE_2K               )  ?  "2k"               : \
   ( x == DRX_FFTMODE_4K               )  ?  "4k"               : \
   ( x == DRX_FFTMODE_8K               )  ?  "8k"               : \
   ( x == DRX_FFTMODE_AUTO             )  ?  "Auto"             : \
   ( x == DRX_FFTMODE_UNKNOWN          )  ?  "Unknown"          : \
					     "(Invalid)"  )
#define DRX_STR_GUARD(x) ( \
   ( x == DRX_GUARD_1DIV32             )  ?  "1/32nd"           : \
   ( x == DRX_GUARD_1DIV16             )  ?  "1/16th"           : \
   ( x == DRX_GUARD_1DIV8              )  ?  "1/8th"            : \
   ( x == DRX_GUARD_1DIV4              )  ?  "1/4th"            : \
   ( x == DRX_GUARD_AUTO               )  ?  "Auto"             : \
   ( x == DRX_GUARD_UNKNOWN            )  ?  "Unknown"          : \
					     "(Invalid)"  )
#define DRX_STR_CONSTELLATION(x) ( \
   ( x == DRX_CONSTELLATION_BPSK       )  ?  "BPSK"            : \
   ( x == DRX_CONSTELLATION_QPSK       )  ?  "QPSK"            : \
   ( x == DRX_CONSTELLATION_PSK8       )  ?  "PSK8"            : \
   ( x == DRX_CONSTELLATION_QAM16      )  ?  "QAM16"           : \
   ( x == DRX_CONSTELLATION_QAM32      )  ?  "QAM32"           : \
   ( x == DRX_CONSTELLATION_QAM64      )  ?  "QAM64"           : \
   ( x == DRX_CONSTELLATION_QAM128     )  ?  "QAM128"          : \
   ( x == DRX_CONSTELLATION_QAM256     )  ?  "QAM256"          : \
   ( x == DRX_CONSTELLATION_QAM512     )  ?  "QAM512"          : \
   ( x == DRX_CONSTELLATION_QAM1024    )  ?  "QAM1024"         : \
   ( x == DRX_CONSTELLATION_QPSK_NR    )  ?  "QPSK_NR"            : \
   ( x == DRX_CONSTELLATION_AUTO       )  ?  "Auto"            : \
   ( x == DRX_CONSTELLATION_UNKNOWN    )  ?  "Unknown"         : \
					     "(Invalid)" )
#define DRX_STR_CODERATE(x) ( \
   ( x == DRX_CODERATE_1DIV2           )  ?  "1/2nd"           : \
   ( x == DRX_CODERATE_2DIV3           )  ?  "2/3rd"           : \
   ( x == DRX_CODERATE_3DIV4           )  ?  "3/4th"           : \
   ( x == DRX_CODERATE_5DIV6           )  ?  "5/6th"           : \
   ( x == DRX_CODERATE_7DIV8           )  ?  "7/8th"           : \
   ( x == DRX_CODERATE_AUTO            )  ?  "Auto"            : \
   ( x == DRX_CODERATE_UNKNOWN         )  ?  "Unknown"         : \
					     "(Invalid)" )
#define DRX_STR_HIERARCHY(x) ( \
   ( x == DRX_HIERARCHY_NONE           )  ?  "None"            : \
   ( x == DRX_HIERARCHY_ALPHA1         )  ?  "Alpha=1"         : \
   ( x == DRX_HIERARCHY_ALPHA2         )  ?  "Alpha=2"         : \
   ( x == DRX_HIERARCHY_ALPHA4         )  ?  "Alpha=4"         : \
   ( x == DRX_HIERARCHY_AUTO           )  ?  "Auto"            : \
   ( x == DRX_HIERARCHY_UNKNOWN        )  ?  "Unknown"         : \
					     "(Invalid)" )
#define DRX_STR_PRIORITY(x) ( \
   ( x == DRX_PRIORITY_LOW             )  ?  "Low"             : \
   ( x == DRX_PRIORITY_HIGH            )  ?  "High"            : \
   ( x == DRX_PRIORITY_UNKNOWN         )  ?  "Unknown"         : \
					     "(Invalid)" )
#define DRX_STR_MIRROR(x) ( \
   ( x == DRX_MIRROR_NO                )  ?  "Normal"          : \
   ( x == DRX_MIRROR_YES               )  ?  "Mirrored"        : \
   ( x == DRX_MIRROR_AUTO              )  ?  "Auto"            : \
   ( x == DRX_MIRROR_UNKNOWN           )  ?  "Unknown"         : \
					     "(Invalid)" )
#define DRX_STR_CLASSIFICATION(x) ( \
   ( x == DRX_CLASSIFICATION_GAUSS     )  ?  "Gaussion"        : \
   ( x == DRX_CLASSIFICATION_HVY_GAUSS )  ?  "Heavy Gaussion"  : \
   ( x == DRX_CLASSIFICATION_COCHANNEL )  ?  "Co-channel"      : \
   ( x == DRX_CLASSIFICATION_STATIC    )  ?  "Static echo"     : \
   ( x == DRX_CLASSIFICATION_MOVING    )  ?  "Moving echo"     : \
   ( x == DRX_CLASSIFICATION_ZERODB    )  ?  "Zero dB echo"    : \
   ( x == DRX_CLASSIFICATION_UNKNOWN   )  ?  "Unknown"         : \
   ( x == DRX_CLASSIFICATION_AUTO      )  ?  "Auto"            : \
					     "(Invalid)" )

#define DRX_STR_INTERLEAVEMODE(x) ( \
   ( x == DRX_INTERLEAVEMODE_I128_J1     ) ? "I128_J1"         : \
   ( x == DRX_INTERLEAVEMODE_I128_J1_V2  ) ? "I128_J1_V2"      : \
   ( x == DRX_INTERLEAVEMODE_I128_J2     ) ? "I128_J2"         : \
   ( x == DRX_INTERLEAVEMODE_I64_J2      ) ? "I64_J2"          : \
   ( x == DRX_INTERLEAVEMODE_I128_J3     ) ? "I128_J3"         : \
   ( x == DRX_INTERLEAVEMODE_I32_J4      ) ? "I32_J4"          : \
   ( x == DRX_INTERLEAVEMODE_I128_J4     ) ? "I128_J4"         : \
   ( x == DRX_INTERLEAVEMODE_I16_J8      ) ? "I16_J8"          : \
   ( x == DRX_INTERLEAVEMODE_I128_J5     ) ? "I128_J5"         : \
   ( x == DRX_INTERLEAVEMODE_I8_J16      ) ? "I8_J16"          : \
   ( x == DRX_INTERLEAVEMODE_I128_J6     ) ? "I128_J6"         : \
   ( x == DRX_INTERLEAVEMODE_RESERVED_11 ) ? "Reserved 11"     : \
   ( x == DRX_INTERLEAVEMODE_I128_J7     ) ? "I128_J7"         : \
   ( x == DRX_INTERLEAVEMODE_RESERVED_13 ) ? "Reserved 13"     : \
   ( x == DRX_INTERLEAVEMODE_I128_J8     ) ? "I128_J8"         : \
   ( x == DRX_INTERLEAVEMODE_RESERVED_15 ) ? "Reserved 15"     : \
   ( x == DRX_INTERLEAVEMODE_I12_J17     ) ? "I12_J17"         : \
   ( x == DRX_INTERLEAVEMODE_I5_J4       ) ? "I5_J4"           : \
   ( x == DRX_INTERLEAVEMODE_B52_M240    ) ? "B52_M240"        : \
   ( x == DRX_INTERLEAVEMODE_B52_M720    ) ? "B52_M720"        : \
   ( x == DRX_INTERLEAVEMODE_B52_M48     ) ? "B52_M48"         : \
   ( x == DRX_INTERLEAVEMODE_B52_M0      ) ? "B52_M0"          : \
   ( x == DRX_INTERLEAVEMODE_UNKNOWN     ) ? "Unknown"         : \
   ( x == DRX_INTERLEAVEMODE_AUTO        ) ? "Auto"            : \
					     "(Invalid)" )

#define DRX_STR_LDPC(x) ( \
   ( x == DRX_LDPC_0_4                   ) ? "0.4"             : \
   ( x == DRX_LDPC_0_6                   ) ? "0.6"             : \
   ( x == DRX_LDPC_0_8                   ) ? "0.8"             : \
   ( x == DRX_LDPC_AUTO                  ) ? "Auto"            : \
   ( x == DRX_LDPC_UNKNOWN               ) ? "Unknown"         : \
					     "(Invalid)" )

#define DRX_STR_CARRIER(x) ( \
   ( x == DRX_CARRIER_MULTI              ) ? "Multi"           : \
   ( x == DRX_CARRIER_SINGLE             ) ? "Single"          : \
   ( x == DRX_CARRIER_AUTO               ) ? "Auto"            : \
   ( x == DRX_CARRIER_UNKNOWN            ) ? "Unknown"         : \
					     "(Invalid)" )

#define DRX_STR_FRAMEMODE(x) ( \
   ( x == DRX_FRAMEMODE_420          )  ? "420"                : \
   ( x == DRX_FRAMEMODE_595          )  ? "595"                : \
   ( x == DRX_FRAMEMODE_945          )  ? "945"                : \
   ( x == DRX_FRAMEMODE_420_FIXED_PN )  ? "420 with fixed PN"  : \
   ( x == DRX_FRAMEMODE_945_FIXED_PN )  ? "945 with fixed PN"  : \
   ( x == DRX_FRAMEMODE_AUTO         )  ? "Auto"               : \
   ( x == DRX_FRAMEMODE_UNKNOWN      )  ? "Unknown"            : \
					  "(Invalid)" )

#define DRX_STR_PILOT(x) ( \
   ( x == DRX_PILOT_ON                 ) ?   "On"              : \
   ( x == DRX_PILOT_OFF                ) ?   "Off"             : \
   ( x == DRX_PILOT_AUTO               ) ?   "Auto"            : \
   ( x == DRX_PILOT_UNKNOWN            ) ?   "Unknown"         : \
					     "(Invalid)" )
/* TPS */

#define DRX_STR_TPS_FRAME(x)  ( \
   ( x == DRX_TPS_FRAME1               )  ?  "Frame1"          : \
   ( x == DRX_TPS_FRAME2               )  ?  "Frame2"          : \
   ( x == DRX_TPS_FRAME3               )  ?  "Frame3"          : \
   ( x == DRX_TPS_FRAME4               )  ?  "Frame4"          : \
   ( x == DRX_TPS_FRAME_UNKNOWN        )  ?  "Unknown"         : \
					     "(Invalid)" )

/* lock status */

#define DRX_STR_LOCKSTATUS(x) ( \
   ( x == DRX_NEVER_LOCK               )  ?  "Never"           : \
   ( x == DRX_NOT_LOCKED               )  ?  "No"              : \
   ( x == DRX_LOCKED                   )  ?  "Locked"          : \
   ( x == DRX_LOCK_STATE_1             )  ?  "Lock state 1"    : \
   ( x == DRX_LOCK_STATE_2             )  ?  "Lock state 2"    : \
   ( x == DRX_LOCK_STATE_3             )  ?  "Lock state 3"    : \
   ( x == DRX_LOCK_STATE_4             )  ?  "Lock state 4"    : \
   ( x == DRX_LOCK_STATE_5             )  ?  "Lock state 5"    : \
   ( x == DRX_LOCK_STATE_6             )  ?  "Lock state 6"    : \
   ( x == DRX_LOCK_STATE_7             )  ?  "Lock state 7"    : \
   ( x == DRX_LOCK_STATE_8             )  ?  "Lock state 8"    : \
   ( x == DRX_LOCK_STATE_9             )  ?  "Lock state 9"    : \
					     "(Invalid)" )

/* version information , modules */
#define DRX_STR_MODULE(x) ( \
   ( x == DRX_MODULE_DEVICE            )  ?  "Device"                : \
   ( x == DRX_MODULE_MICROCODE         )  ?  "Microcode"             : \
   ( x == DRX_MODULE_DRIVERCORE        )  ?  "CoreDriver"            : \
   ( x == DRX_MODULE_DEVICEDRIVER      )  ?  "DeviceDriver"          : \
   ( x == DRX_MODULE_BSP_I2C           )  ?  "BSP I2C"               : \
   ( x == DRX_MODULE_BSP_TUNER         )  ?  "BSP Tuner"             : \
   ( x == DRX_MODULE_BSP_HOST          )  ?  "BSP Host"              : \
   ( x == DRX_MODULE_DAP               )  ?  "Data Access Protocol"  : \
   ( x == DRX_MODULE_UNKNOWN           )  ?  "Unknown"               : \
					     "(Invalid)" )

#define DRX_STR_POWER_MODE(x) ( \
   ( x == DRX_POWER_UP                 )  ?  "DRX_POWER_UP    "  : \
   ( x == DRX_POWER_MODE_1             )  ?  "DRX_POWER_MODE_1"  : \
   ( x == DRX_POWER_MODE_2             )  ?  "DRX_POWER_MODE_2"  : \
   ( x == DRX_POWER_MODE_3             )  ?  "DRX_POWER_MODE_3"  : \
   ( x == DRX_POWER_MODE_4             )  ?  "DRX_POWER_MODE_4"  : \
   ( x == DRX_POWER_MODE_5             )  ?  "DRX_POWER_MODE_5"  : \
   ( x == DRX_POWER_MODE_6             )  ?  "DRX_POWER_MODE_6"  : \
   ( x == DRX_POWER_MODE_7             )  ?  "DRX_POWER_MODE_7"  : \
   ( x == DRX_POWER_MODE_8             )  ?  "DRX_POWER_MODE_8"  : \
   ( x == DRX_POWER_MODE_9             )  ?  "DRX_POWER_MODE_9"  : \
   ( x == DRX_POWER_MODE_10            )  ?  "DRX_POWER_MODE_10" : \
   ( x == DRX_POWER_MODE_11            )  ?  "DRX_POWER_MODE_11" : \
   ( x == DRX_POWER_MODE_12            )  ?  "DRX_POWER_MODE_12" : \
   ( x == DRX_POWER_MODE_13            )  ?  "DRX_POWER_MODE_13" : \
   ( x == DRX_POWER_MODE_14            )  ?  "DRX_POWER_MODE_14" : \
   ( x == DRX_POWER_MODE_15            )  ?  "DRX_POWER_MODE_15" : \
   ( x == DRX_POWER_MODE_16            )  ?  "DRX_POWER_MODE_16" : \
   ( x == DRX_POWER_DOWN               )  ?  "DRX_POWER_DOWN  " : \
					     "(Invalid)" )

#define DRX_STR_OOB_STANDARD(x) ( \
   ( x == DRX_OOB_MODE_A            )  ?  "ANSI 55-1  " : \
   ( x == DRX_OOB_MODE_B_GRADE_A    )  ?  "ANSI 55-2 A" : \
   ( x == DRX_OOB_MODE_B_GRADE_B    )  ?  "ANSI 55-2 B" : \
					     "(Invalid)" )

#define DRX_STR_AUD_STANDARD(x) ( \
   ( x == DRX_AUD_STANDARD_BTSC         )  ? "BTSC"                     : \
   ( x == DRX_AUD_STANDARD_A2           )  ? "A2"                       : \
   ( x == DRX_AUD_STANDARD_EIAJ         )  ? "EIAJ"                     : \
   ( x == DRX_AUD_STANDARD_FM_STEREO    )  ? "FM Stereo"                : \
   ( x == DRX_AUD_STANDARD_AUTO         )  ? "Auto"                     : \
   ( x == DRX_AUD_STANDARD_M_MONO       )  ? "M-Standard Mono"          : \
   ( x == DRX_AUD_STANDARD_D_K_MONO     )  ? "D/K Mono FM"              : \
   ( x == DRX_AUD_STANDARD_BG_FM        )  ? "B/G-Dual Carrier FM (A2)" : \
   ( x == DRX_AUD_STANDARD_D_K1         )  ? "D/K1-Dual Carrier FM"     : \
   ( x == DRX_AUD_STANDARD_D_K2         )  ? "D/K2-Dual Carrier FM"     : \
   ( x == DRX_AUD_STANDARD_D_K3         )  ? "D/K3-Dual Carrier FM"     : \
   ( x == DRX_AUD_STANDARD_BG_NICAM_FM  )  ? "B/G-NICAM-FM"             : \
   ( x == DRX_AUD_STANDARD_L_NICAM_AM   )  ? "L-NICAM-AM"               : \
   ( x == DRX_AUD_STANDARD_I_NICAM_FM   )  ? "I-NICAM-FM"               : \
   ( x == DRX_AUD_STANDARD_D_K_NICAM_FM )  ? "D/K-NICAM-FM"             : \
   ( x == DRX_AUD_STANDARD_UNKNOWN      )  ? "Unknown"                  : \
					     "(Invalid)"  )
#define DRX_STR_AUD_STEREO(x) ( \
   ( x == TRUE                          )  ? "Stereo"           : \
   ( x == FALSE                         )  ? "Mono"             : \
					     "(Invalid)"  )

#define DRX_STR_AUD_SAP(x) ( \
   ( x == TRUE                          )  ? "Present"          : \
   ( x == FALSE                         )  ? "Not present"      : \
					     "(Invalid)"  )

#define DRX_STR_AUD_CARRIER(x) ( \
   ( x == TRUE                          )  ? "Present"          : \
   ( x == FALSE                         )  ? "Not present"      : \
					     "(Invalid)"  )

#define DRX_STR_AUD_RDS(x) ( \
   ( x == TRUE                          )  ? "Available"        : \
   ( x == FALSE                         )  ? "Not Available"    : \
					     "(Invalid)"  )

#define DRX_STR_AUD_NICAM_STATUS(x) ( \
   ( x == DRX_AUD_NICAM_DETECTED        )  ? "Detected"         : \
   ( x == DRX_AUD_NICAM_NOT_DETECTED    )  ? "Not detected"     : \
   ( x == DRX_AUD_NICAM_BAD             )  ? "Bad"              : \
					     "(Invalid)"  )

#define DRX_STR_RDS_VALID(x) ( \
   ( x == TRUE                          )  ? "Valid"            : \
   ( x == FALSE                         )  ? "Not Valid"        : \
					     "(Invalid)"  )

/*-------------------------------------------------------------------------
Access macros
-------------------------------------------------------------------------*/

/**
* \brief Create a compilable reference to the microcode attribute
* \param d pointer to demod instance
*
* Used as main reference to an attribute field.
* Used by both macro implementation and function implementation.
* These macros are defined to avoid duplication of code in macro and function
* definitions that handle access of demod common or extended attributes.
*
*/

#define DRX_ATTR_MCRECORD( d )        ((d)->myCommonAttr->mcversion)
#define DRX_ATTR_MIRRORFREQSPECT( d ) ((d)->myCommonAttr->mirrorFreqSpect)
#define DRX_ATTR_CURRENTPOWERMODE( d )((d)->myCommonAttr->currentPowerMode)
#define DRX_ATTR_ISOPENED( d )        ((d)->myCommonAttr->isOpened)
#define DRX_ATTR_USEBOOTLOADER( d )   ((d)->myCommonAttr->useBootloader)
#define DRX_ATTR_CURRENTSTANDARD( d ) ((d)->myCommonAttr->currentStandard)
#define DRX_ATTR_PREVSTANDARD( d )    ((d)->myCommonAttr->prevStandard)
#define DRX_ATTR_CACHESTANDARD( d )   ((d)->myCommonAttr->diCacheStandard)
#define DRX_ATTR_CURRENTCHANNEL( d )  ((d)->myCommonAttr->currentChannel)
#define DRX_ATTR_MICROCODE( d )       ((d)->myCommonAttr->microcode)
#define DRX_ATTR_MICROCODESIZE( d )   ((d)->myCommonAttr->microcodeSize)
#define DRX_ATTR_VERIFYMICROCODE( d ) ((d)->myCommonAttr->verifyMicrocode)
#define DRX_ATTR_CAPABILITIES( d )    ((d)->myCommonAttr->capabilities)
#define DRX_ATTR_PRODUCTID( d )       ((d)->myCommonAttr->productId)
#define DRX_ATTR_INTERMEDIATEFREQ( d) ((d)->myCommonAttr->intermediateFreq)
#define DRX_ATTR_SYSCLOCKFREQ( d)     ((d)->myCommonAttr->sysClockFreq)
#define DRX_ATTR_TUNERRFAGCPOL( d )   ((d)->myCommonAttr->tunerRfAgcPol)
#define DRX_ATTR_TUNERIFAGCPOL( d)    ((d)->myCommonAttr->tunerIfAgcPol)
#define DRX_ATTR_TUNERSLOWMODE( d)    ((d)->myCommonAttr->tunerSlowMode)
#define DRX_ATTR_TUNERSPORTNR( d)     ((d)->myCommonAttr->tunerPortNr)
#define DRX_ATTR_TUNER( d )           ((d)->myTuner)
#define DRX_ATTR_I2CADDR( d )         ((d)->myI2CDevAddr->i2cAddr)
#define DRX_ATTR_I2CDEVID( d )        ((d)->myI2CDevAddr->i2cDevId)

/**
* \brief Actual access macro's
* \param d pointer to demod instance
* \param x value to set ar to get
*
* SET macro's must be used to set the value of an attribute.
* GET macro's must be used to retrieve the value of an attribute.
*
*/

/**************************/

#define DRX_SET_MIRRORFREQSPECT( d, x )                     \
   do {                                                     \
      DRX_ATTR_MIRRORFREQSPECT( d ) = (x);                  \
   } while(0)

#define DRX_GET_MIRRORFREQSPECT( d, x )                     \
   do {                                                     \
      (x)=DRX_ATTR_MIRRORFREQSPECT( d );                    \
   } while(0)

/**************************/

#define DRX_SET_CURRENTPOWERMODE( d, x )                    \
   do {                                                     \
      DRX_ATTR_CURRENTPOWERMODE( d ) = (x);                 \
   } while(0)

#define DRX_GET_CURRENTPOWERMODE( d, x )                    \
   do {                                                     \
      (x)=DRX_ATTR_CURRENTPOWERMODE( d );                   \
   } while(0)

/**************************/

#define DRX_SET_MICROCODE( d, x )                           \
   do {                                                     \
      DRX_ATTR_MICROCODE( d ) = (x);                        \
   } while(0)

#define DRX_GET_MICROCODE( d, x )                           \
   do {                                                     \
      (x)=DRX_ATTR_MICROCODE( d );                          \
   } while(0)

/**************************/

#define DRX_SET_MICROCODESIZE( d, x )                       \
   do {                                                     \
      DRX_ATTR_MICROCODESIZE(d) = (x);                      \
   } while(0)

#define DRX_GET_MICROCODESIZE( d, x )                       \
   do {                                                     \
      (x)=DRX_ATTR_MICROCODESIZE(d);                        \
   } while(0)

/**************************/

#define DRX_SET_VERIFYMICROCODE( d, x )                     \
   do {                                                     \
      DRX_ATTR_VERIFYMICROCODE(d) = (x);                    \
   } while(0)

#define DRX_GET_VERIFYMICROCODE( d, x )                     \
   do {                                                     \
      (x)=DRX_ATTR_VERIFYMICROCODE(d);                      \
   } while(0)

/**************************/

#define DRX_SET_MCVERTYPE( d, x )                           \
   do {                                                     \
      DRX_ATTR_MCRECORD(d).auxType = (x);                   \
   } while (0)

#define DRX_GET_MCVERTYPE( d, x )                           \
   do {                                                     \
      (x) = DRX_ATTR_MCRECORD(d).auxType;                   \
   } while (0)

/**************************/

#define DRX_ISMCVERTYPE(x) ((x) == AUX_VER_RECORD)

/**************************/

#define DRX_SET_MCDEV( d, x )                               \
   do {                                                     \
      DRX_ATTR_MCRECORD(d).mcDevType = (x);                 \
   } while (0)

#define DRX_GET_MCDEV( d, x )                               \
   do {                                                     \
      (x) = DRX_ATTR_MCRECORD(d).mcDevType;                 \
   } while (0)

/**************************/

#define DRX_SET_MCVERSION( d, x )                           \
   do {                                                     \
      DRX_ATTR_MCRECORD(d).mcVersion = (x);                 \
   } while (0)

#define DRX_GET_MCVERSION( d, x )                           \
   do {                                                     \
      (x) = DRX_ATTR_MCRECORD(d).mcVersion;                 \
   } while (0)

/**************************/
#define DRX_SET_MCPATCH( d, x )                             \
   do {                                                     \
      DRX_ATTR_MCRECORD(d).mcBaseVersion = (x);             \
   } while (0)

#define DRX_GET_MCPATCH( d, x )                             \
   do {                                                     \
      (x) = DRX_ATTR_MCRECORD(d).mcBaseVersion;             \
   } while (0)

/**************************/

#define DRX_SET_I2CADDR( d, x )                             \
   do {                                                     \
      DRX_ATTR_I2CADDR(d) = (x);                            \
   } while(0)

#define DRX_GET_I2CADDR( d, x )                             \
   do {                                                     \
      (x)=DRX_ATTR_I2CADDR(d);                              \
   } while(0)

/**************************/

#define DRX_SET_I2CDEVID( d, x )                            \
   do {                                                     \
      DRX_ATTR_I2CDEVID(d) = (x);                           \
   } while(0)

#define DRX_GET_I2CDEVID( d, x )                            \
   do {                                                     \
      (x)=DRX_ATTR_I2CDEVID(d);                             \
   } while(0)

/**************************/

#define DRX_SET_USEBOOTLOADER( d, x )                       \
   do {                                                     \
      DRX_ATTR_USEBOOTLOADER(d) = (x);                      \
   } while(0)

#define DRX_GET_USEBOOTLOADER( d, x)                        \
   do {                                                     \
      (x)=DRX_ATTR_USEBOOTLOADER(d);                        \
   } while(0)

/**************************/

#define DRX_SET_CURRENTSTANDARD( d, x )                     \
   do {                                                     \
      DRX_ATTR_CURRENTSTANDARD(d) = (x);                    \
   } while(0)

#define DRX_GET_CURRENTSTANDARD( d, x)                      \
   do {                                                     \
      (x)=DRX_ATTR_CURRENTSTANDARD(d);                      \
   } while(0)

/**************************/

#define DRX_SET_PREVSTANDARD( d, x )                        \
   do {                                                     \
      DRX_ATTR_PREVSTANDARD(d) = (x);                       \
   } while(0)

#define DRX_GET_PREVSTANDARD( d, x)                         \
   do {                                                     \
      (x)=DRX_ATTR_PREVSTANDARD(d);                         \
   } while(0)

/**************************/

#define DRX_SET_CACHESTANDARD( d, x )                       \
   do {                                                     \
      DRX_ATTR_CACHESTANDARD(d) = (x);                      \
   } while(0)

#define DRX_GET_CACHESTANDARD( d, x)                        \
   do {                                                     \
      (x)=DRX_ATTR_CACHESTANDARD(d);                        \
   } while(0)

/**************************/

#define DRX_SET_CURRENTCHANNEL( d, x )                      \
   do {                                                     \
      DRX_ATTR_CURRENTCHANNEL(d) = (x);                     \
   } while(0)

#define DRX_GET_CURRENTCHANNEL( d, x)                       \
   do {                                                     \
      (x)=DRX_ATTR_CURRENTCHANNEL(d);                       \
   } while(0)

/**************************/

#define DRX_SET_ISOPENED( d, x )                            \
   do {                                                     \
      DRX_ATTR_ISOPENED(d) = (x);                           \
   } while(0)

#define DRX_GET_ISOPENED( d, x)                             \
   do {                                                     \
      (x) = DRX_ATTR_ISOPENED(d);                           \
   } while(0)

/**************************/

#define DRX_SET_TUNER( d, x )                               \
   do {                                                     \
      DRX_ATTR_TUNER(d) = (x);                              \
   } while(0)

#define DRX_GET_TUNER( d, x)                                \
   do {                                                     \
      (x) = DRX_ATTR_TUNER(d);                              \
   } while(0)

/**************************/

#define DRX_SET_CAPABILITIES( d, x )                        \
   do {                                                     \
      DRX_ATTR_CAPABILITIES(d) = (x);                       \
   } while(0)

#define DRX_GET_CAPABILITIES( d, x)                         \
   do {                                                     \
      (x) = DRX_ATTR_CAPABILITIES(d);                       \
   } while(0)

/**************************/

#define DRX_SET_PRODUCTID( d, x )                           \
   do {                                                     \
      DRX_ATTR_PRODUCTID(d) |= (x << 4);                    \
   } while(0)

#define DRX_GET_PRODUCTID( d, x)                            \
   do {                                                     \
      (x) = (DRX_ATTR_PRODUCTID(d) >> 4);                   \
   } while(0)

/**************************/

#define DRX_SET_MFX( d, x )                                 \
   do {                                                     \
      DRX_ATTR_PRODUCTID(d) |= (x);                         \
   } while(0)

#define DRX_GET_MFX( d, x)                                  \
   do {                                                     \
      (x) = (DRX_ATTR_PRODUCTID(d) & 0xF);                  \
   } while(0)

/**************************/

#define DRX_SET_INTERMEDIATEFREQ( d, x )                    \
   do {                                                     \
      DRX_ATTR_INTERMEDIATEFREQ(d) = (x);                   \
   } while(0)

#define DRX_GET_INTERMEDIATEFREQ( d, x)                     \
   do {                                                     \
      (x) = DRX_ATTR_INTERMEDIATEFREQ(d);                   \
   } while(0)

/**************************/

#define DRX_SET_SYSCLOCKFREQ( d, x )                        \
   do {                                                     \
      DRX_ATTR_SYSCLOCKFREQ(d) = (x);                       \
   } while(0)

#define DRX_GET_SYSCLOCKFREQ( d, x)                         \
   do {                                                     \
      (x) = DRX_ATTR_SYSCLOCKFREQ(d);                       \
   } while(0)

/**************************/

#define DRX_SET_TUNERRFAGCPOL( d, x )                       \
   do {                                                     \
      DRX_ATTR_TUNERRFAGCPOL(d) = (x);                      \
   } while(0)

#define DRX_GET_TUNERRFAGCPOL( d, x)                        \
   do {                                                     \
      (x) = DRX_ATTR_TUNERRFAGCPOL(d);                      \
   } while(0)

/**************************/

#define DRX_SET_TUNERIFAGCPOL( d, x )                       \
   do {                                                     \
      DRX_ATTR_TUNERIFAGCPOL(d) = (x);                      \
   } while(0)

#define DRX_GET_TUNERIFAGCPOL( d, x)                        \
   do {                                                     \
      (x) = DRX_ATTR_TUNERIFAGCPOL(d);                      \
   } while(0)

/**************************/

#define DRX_SET_TUNERSLOWMODE( d, x )                       \
   do {                                                     \
      DRX_ATTR_TUNERSLOWMODE(d) = (x);                      \
   } while(0)

#define DRX_GET_TUNERSLOWMODE( d, x)                        \
   do {                                                     \
      (x) = DRX_ATTR_TUNERSLOWMODE(d);                      \
   } while(0)

/**************************/

#define DRX_SET_TUNERPORTNR( d, x )                         \
   do {                                                     \
      DRX_ATTR_TUNERSPORTNR(d) = (x);                       \
   } while(0)

/**************************/

/* Macros with device-specific handling are converted to CFG functions */

#define DRX_ACCESSMACRO_SET( demod, value, cfgName, dataType )             \
   do {                                                                    \
      DRXCfg_t config;                                                     \
      dataType cfgData;                                                    \
      config.cfgType = cfgName;                                            \
      config.cfgData = &cfgData;                                           \
      cfgData = value;                                                     \
      DRX_Ctrl( demod, DRX_CTRL_SET_CFG, &config );                        \
   } while ( 0 )

#define DRX_ACCESSMACRO_GET( demod, value, cfgName, dataType, errorValue ) \
   do {                                                                    \
      DRXStatus_t cfgStatus;                                               \
      DRXCfg_t    config;                                                  \
      dataType    cfgData;                                                 \
      config.cfgType = cfgName;                                            \
      config.cfgData = &cfgData;                                           \
      cfgStatus = DRX_Ctrl( demod, DRX_CTRL_GET_CFG, &config );            \
      if ( cfgStatus == DRX_STS_OK ) {                                     \
	 value = cfgData;                                                  \
      } else {                                                             \
	 value = (dataType)errorValue;                                     \
      }                                                                    \
   } while ( 0 )

/* Configuration functions for usage by Access (XS) Macros */

#ifndef DRX_XS_CFG_BASE
#define DRX_XS_CFG_BASE (500)
#endif

#define DRX_XS_CFG_PRESET          ( DRX_XS_CFG_BASE + 0 )
#define DRX_XS_CFG_AUD_BTSC_DETECT ( DRX_XS_CFG_BASE + 1 )
#define DRX_XS_CFG_QAM_LOCKRANGE   ( DRX_XS_CFG_BASE + 2 )

/* Access Macros with device-specific handling */

#define DRX_SET_PRESET( d, x ) \
   DRX_ACCESSMACRO_SET( (d), (x), DRX_XS_CFG_PRESET, char* )
#define DRX_GET_PRESET( d, x ) \
   DRX_ACCESSMACRO_GET( (d), (x), DRX_XS_CFG_PRESET, char*, "ERROR" )

#define DRX_SET_AUD_BTSC_DETECT( d, x ) DRX_ACCESSMACRO_SET( (d), (x), \
	 DRX_XS_CFG_AUD_BTSC_DETECT, DRXAudBtscDetect_t )
#define DRX_GET_AUD_BTSC_DETECT( d, x ) DRX_ACCESSMACRO_GET( (d), (x), \
	 DRX_XS_CFG_AUD_BTSC_DETECT, DRXAudBtscDetect_t, DRX_UNKNOWN )

#define DRX_SET_QAM_LOCKRANGE( d, x ) DRX_ACCESSMACRO_SET( (d), (x), \
	 DRX_XS_CFG_QAM_LOCKRANGE, DRXQamLockRange_t )
#define DRX_GET_QAM_LOCKRANGE( d, x ) DRX_ACCESSMACRO_GET( (d), (x), \
	 DRX_XS_CFG_QAM_LOCKRANGE, DRXQamLockRange_t, DRX_UNKNOWN )

/**
* \brief Macro to check if std is an ATV standard
* \retval TRUE std is an ATV standard
* \retval FALSE std is an ATV standard
*/
#define DRX_ISATVSTD( std ) ( ( (std) == DRX_STANDARD_PAL_SECAM_BG ) || \
			      ( (std) == DRX_STANDARD_PAL_SECAM_DK ) || \
			      ( (std) == DRX_STANDARD_PAL_SECAM_I  ) || \
			      ( (std) == DRX_STANDARD_PAL_SECAM_L  ) || \
			      ( (std) == DRX_STANDARD_PAL_SECAM_LP ) || \
			      ( (std) == DRX_STANDARD_NTSC ) || \
			      ( (std) == DRX_STANDARD_FM ) )

/**
* \brief Macro to check if std is an QAM standard
* \retval TRUE std is an QAM standards
* \retval FALSE std is an QAM standards
*/
#define DRX_ISQAMSTD( std ) ( ( (std) == DRX_STANDARD_ITU_A ) || \
			      ( (std) == DRX_STANDARD_ITU_B ) || \
			      ( (std) == DRX_STANDARD_ITU_C ) || \
			      ( (std) == DRX_STANDARD_ITU_D ))

/**
* \brief Macro to check if std is VSB standard
* \retval TRUE std is VSB standard
* \retval FALSE std is not VSB standard
*/
#define DRX_ISVSBSTD( std ) ( (std) == DRX_STANDARD_8VSB )

/**
* \brief Macro to check if std is DVBT standard
* \retval TRUE std is DVBT standard
* \retval FALSE std is not DVBT standard
*/
#define DRX_ISDVBTSTD( std ) ( (std) == DRX_STANDARD_DVBT )

/*-------------------------------------------------------------------------
Exported FUNCTIONS
-------------------------------------------------------------------------*/

	DRXStatus_t DRX_Init(pDRXDemodInstance_t demods[]);

	DRXStatus_t DRX_Term(void);

	DRXStatus_t DRX_Open(pDRXDemodInstance_t demod);

	DRXStatus_t DRX_Close(pDRXDemodInstance_t demod);

	DRXStatus_t DRX_Ctrl(pDRXDemodInstance_t demod,
			     DRXCtrlIndex_t ctrl, void *ctrlData);

/*-------------------------------------------------------------------------
THE END
-------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif				/* __DRXDRIVER_H__ */
