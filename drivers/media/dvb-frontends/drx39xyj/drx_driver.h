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

#ifndef __DRXDRIVER_H__
#define __DRXDRIVER_H__

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/i2c.h>

/*
 * This structure contains the I2C address, the device ID and a user_data pointer.
 * The user_data pointer can be used for application specific purposes.
 */
struct i2c_device_addr {
	u16 i2c_addr;		/* The I2C address of the device. */
	u16 i2c_dev_id;		/* The device identifier. */
	void *user_data;		/* User data pointer */
};

/*
* \def IS_I2C_10BIT( addr )
* \brief Determine if I2C address 'addr' is a 10 bits address or not.
* \param addr The I2C address.
* \return int.
* \retval 0 if address is not a 10 bits I2C address.
* \retval 1 if address is a 10 bits I2C address.
*/
#define IS_I2C_10BIT(addr) \
	 (((addr) & 0xF8) == 0xF0)

/*------------------------------------------------------------------------------
Exported FUNCTIONS
------------------------------------------------------------------------------*/

/*
* \fn drxbsp_i2c_init()
* \brief Initialize I2C communication module.
* \return int Return status.
* \retval 0 Initialization successful.
* \retval -EIO Initialization failed.
*/
int drxbsp_i2c_init(void);

/*
* \fn drxbsp_i2c_term()
* \brief Terminate I2C communication module.
* \return int Return status.
* \retval 0 Termination successful.
* \retval -EIO Termination failed.
*/
int drxbsp_i2c_term(void);

/*
* \fn int drxbsp_i2c_write_read( struct i2c_device_addr *w_dev_addr,
*                                       u16 w_count,
*                                       u8 * wData,
*                                       struct i2c_device_addr *r_dev_addr,
*                                       u16 r_count,
*                                       u8 * r_data)
* \brief Read and/or write count bytes from I2C bus, store them in data[].
* \param w_dev_addr The device i2c address and the device ID to write to
* \param w_count   The number of bytes to write
* \param wData    The array to write the data to
* \param r_dev_addr The device i2c address and the device ID to read from
* \param r_count   The number of bytes to read
* \param r_data    The array to read the data from
* \return int Return status.
* \retval 0 Succes.
* \retval -EIO Failure.
* \retval -EINVAL Parameter 'wcount' is not zero but parameter
*                                       'wdata' contains NULL.
*                                       Idem for 'rcount' and 'rdata'.
*                                       Both w_dev_addr and r_dev_addr are NULL.
*
* This function must implement an atomic write and/or read action on the I2C bus
* No other process may use the I2C bus when this function is executing.
* The critical section of this function runs from and including the I2C
* write, up to and including the I2C read action.
*
* The device ID can be useful if several devices share an I2C address.
* It can be used to control a "switch" on the I2C bus to the correct device.
*/
int drxbsp_i2c_write_read(struct i2c_device_addr *w_dev_addr,
					u16 w_count,
					u8 *wData,
					struct i2c_device_addr *r_dev_addr,
					u16 r_count, u8 *r_data);

/*
* \fn drxbsp_i2c_error_text()
* \brief Returns a human readable error.
* Counter part of numerical drx_i2c_error_g.
*
* \return char* Pointer to human readable error text.
*/
char *drxbsp_i2c_error_text(void);

/*
* \var drx_i2c_error_g;
* \brief I2C specific error codes, platform dependent.
*/
extern int drx_i2c_error_g;

#define TUNER_MODE_SUB0    0x0001	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB1    0x0002	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB2    0x0004	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB3    0x0008	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB4    0x0010	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB5    0x0020	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB6    0x0040	/* for sub-mode (e.g. RF-AGC setting) */
#define TUNER_MODE_SUB7    0x0080	/* for sub-mode (e.g. RF-AGC setting) */

#define TUNER_MODE_DIGITAL 0x0100	/* for digital channel (e.g. DVB-T)   */
#define TUNER_MODE_ANALOG  0x0200	/* for analog channel  (e.g. PAL)     */
#define TUNER_MODE_SWITCH  0x0400	/* during channel switch & scanning   */
#define TUNER_MODE_LOCK    0x0800	/* after tuner has locked             */
#define TUNER_MODE_6MHZ    0x1000	/* for 6MHz bandwidth channels        */
#define TUNER_MODE_7MHZ    0x2000	/* for 7MHz bandwidth channels        */
#define TUNER_MODE_8MHZ    0x4000	/* for 8MHz bandwidth channels        */

#define TUNER_MODE_SUB_MAX 8
#define TUNER_MODE_SUBALL  (TUNER_MODE_SUB0 | TUNER_MODE_SUB1 | \
			      TUNER_MODE_SUB2 | TUNER_MODE_SUB3 | \
			      TUNER_MODE_SUB4 | TUNER_MODE_SUB5 | \
			      TUNER_MODE_SUB6 | TUNER_MODE_SUB7)


enum tuner_lock_status {
	TUNER_LOCKED,
	TUNER_NOT_LOCKED
};

struct tuner_common {
	char *name;	/* Tuner brand & type name */
	s32 min_freq_rf;	/* Lowest  RF input frequency, in kHz */
	s32 max_freq_rf;	/* Highest RF input frequency, in kHz */

	u8 sub_mode;	/* Index to sub-mode in use */
	char ***sub_mode_descriptions;	/* Pointer to description of sub-modes */
	u8 sub_modes;	/* Number of available sub-modes      */

	/* The following fields will be either 0, NULL or false and do not need
		initialisation */
	void *self_check;	/* gives proof of initialization  */
	bool programmed;	/* only valid if self_check is OK  */
	s32 r_ffrequency;	/* only valid if programmed       */
	s32 i_ffrequency;	/* only valid if programmed       */

	void *my_user_data;	/* pointer to associated demod instance */
	u16 my_capabilities;	/* value for storing application flags  */
};

struct tuner_instance;

typedef int(*tuner_open_func_t) (struct tuner_instance *tuner);
typedef int(*tuner_close_func_t) (struct tuner_instance *tuner);

typedef int(*tuner_set_frequency_func_t) (struct tuner_instance *tuner,
						u32 mode,
						s32
						frequency);

typedef int(*tuner_get_frequency_func_t) (struct tuner_instance *tuner,
						u32 mode,
						s32 *
						r_ffrequency,
						s32 *
						i_ffrequency);

typedef int(*tuner_lock_status_func_t) (struct tuner_instance *tuner,
						enum tuner_lock_status *
						lock_stat);

typedef int(*tune_ri2c_write_read_func_t) (struct tuner_instance *tuner,
						struct i2c_device_addr *
						w_dev_addr, u16 w_count,
						u8 *wData,
						struct i2c_device_addr *
						r_dev_addr, u16 r_count,
						u8 *r_data);

struct tuner_ops {
	tuner_open_func_t open_func;
	tuner_close_func_t close_func;
	tuner_set_frequency_func_t set_frequency_func;
	tuner_get_frequency_func_t get_frequency_func;
	tuner_lock_status_func_t lock_status_func;
	tune_ri2c_write_read_func_t i2c_write_read_func;

};

struct tuner_instance {
	struct i2c_device_addr my_i2c_dev_addr;
	struct tuner_common *my_common_attr;
	void *my_ext_attr;
	struct tuner_ops *my_funct;
};

int drxbsp_tuner_set_frequency(struct tuner_instance *tuner,
					u32 mode,
					s32 frequency);

int drxbsp_tuner_get_frequency(struct tuner_instance *tuner,
					u32 mode,
					s32 *r_ffrequency,
					s32 *i_ffrequency);

int drxbsp_tuner_default_i2c_write_read(struct tuner_instance *tuner,
						struct i2c_device_addr *w_dev_addr,
						u16 w_count,
						u8 *wData,
						struct i2c_device_addr *r_dev_addr,
						u16 r_count, u8 *r_data);

/*************
*
* This section configures the DRX Data Access Protocols (DAPs).
*
**************/

/*
* \def DRXDAP_SINGLE_MASTER
* \brief Enable I2C single or I2C multimaster mode on host.
*
* Set to 1 to enable single master mode
* Set to 0 to enable multi master mode
*
* The actual DAP implementation may be restricted to only one of the modes.
* A compiler warning or error will be generated if the DAP implementation
* overrides or cannot handle the mode defined below.
*/
#ifndef DRXDAP_SINGLE_MASTER
#define DRXDAP_SINGLE_MASTER 1
#endif

/*
* \def DRXDAP_MAX_WCHUNKSIZE
* \brief Defines maximum chunksize of an i2c write action by host.
*
* This indicates the maximum size of data the I2C device driver is able to
* write at a time. This includes I2C device address and register addressing.
*
* This maximum size may be restricted by the actual DAP implementation.
* A compiler warning or error will be generated if the DAP implementation
* overrides or cannot handle the chunksize defined below.
*
* Beware that the DAP uses  DRXDAP_MAX_WCHUNKSIZE to create a temporary data
* buffer. Do not undefine or choose too large, unless your system is able to
* handle a stack buffer of that size.
*
*/
#ifndef DRXDAP_MAX_WCHUNKSIZE
#define  DRXDAP_MAX_WCHUNKSIZE 60
#endif

/*
* \def DRXDAP_MAX_RCHUNKSIZE
* \brief Defines maximum chunksize of an i2c read action by host.
*
* This indicates the maximum size of data the I2C device driver is able to read
* at a time. Minimum value is 2. Also, the read chunk size must be even.
*
* This maximum size may be restricted by the actual DAP implementation.
* A compiler warning or error will be generated if the DAP implementation
* overrides or cannot handle the chunksize defined below.
*/
#ifndef DRXDAP_MAX_RCHUNKSIZE
#define  DRXDAP_MAX_RCHUNKSIZE 60
#endif

/*************
*
* This section describes drxdriver defines.
*
**************/

/*
* \def DRX_UNKNOWN
* \brief Generic UNKNOWN value for DRX enumerated types.
*
* Used to indicate that the parameter value is unknown or not yet initialized.
*/
#ifndef DRX_UNKNOWN
#define DRX_UNKNOWN (254)
#endif

/*
* \def DRX_AUTO
* \brief Generic AUTO value for DRX enumerated types.
*
* Used to instruct the driver to automatically determine the value of the
* parameter.
*/
#ifndef DRX_AUTO
#define DRX_AUTO    (255)
#endif

/*************
*
* This section describes flag definitions for the device capbilities.
*
**************/

/*
* \brief LNA capability flag
*
* Device has a Low Noise Amplifier
*
*/
#define DRX_CAPABILITY_HAS_LNA           (1UL <<  0)
/*
* \brief OOB-RX capability flag
*
* Device has OOB-RX
*
*/
#define DRX_CAPABILITY_HAS_OOBRX         (1UL <<  1)
/*
* \brief ATV capability flag
*
* Device has ATV
*
*/
#define DRX_CAPABILITY_HAS_ATV           (1UL <<  2)
/*
* \brief DVB-T capability flag
*
* Device has DVB-T
*
*/
#define DRX_CAPABILITY_HAS_DVBT          (1UL <<  3)
/*
* \brief  ITU-B capability flag
*
* Device has ITU-B
*
*/
#define DRX_CAPABILITY_HAS_ITUB          (1UL <<  4)
/*
* \brief  Audio capability flag
*
* Device has Audio
*
*/
#define DRX_CAPABILITY_HAS_AUD           (1UL <<  5)
/*
* \brief  SAW switch capability flag
*
* Device has SAW switch
*
*/
#define DRX_CAPABILITY_HAS_SAWSW         (1UL <<  6)
/*
* \brief  GPIO1 capability flag
*
* Device has GPIO1
*
*/
#define DRX_CAPABILITY_HAS_GPIO1         (1UL <<  7)
/*
* \brief  GPIO2 capability flag
*
* Device has GPIO2
*
*/
#define DRX_CAPABILITY_HAS_GPIO2         (1UL <<  8)
/*
* \brief  IRQN capability flag
*
* Device has IRQN
*
*/
#define DRX_CAPABILITY_HAS_IRQN          (1UL <<  9)
/*
* \brief  8VSB capability flag
*
* Device has 8VSB
*
*/
#define DRX_CAPABILITY_HAS_8VSB          (1UL << 10)
/*
* \brief  SMA-TX capability flag
*
* Device has SMATX
*
*/
#define DRX_CAPABILITY_HAS_SMATX         (1UL << 11)
/*
* \brief  SMA-RX capability flag
*
* Device has SMARX
*
*/
#define DRX_CAPABILITY_HAS_SMARX         (1UL << 12)
/*
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
#define DRX_VERSIONSTRING(MAJOR, MINOR, PATCH) \
	 DRX_VERSIONSTRING_HELP(MAJOR)"." \
	 DRX_VERSIONSTRING_HELP(MINOR)"." \
	 DRX_VERSIONSTRING_HELP(PATCH)
#define DRX_VERSIONSTRING_HELP(NUM) #NUM

/*
* \brief Macro to create byte array elements from 16 bit integers.
* This macro is used to create byte arrays for block writes.
* Block writes speed up I2C traffic between host and demod.
* The macro takes care of the required byte order in a 16 bits word.
* x->lowbyte(x), highbyte(x)
*/
#define DRX_16TO8(x) ((u8) (((u16)x) & 0xFF)), \
			((u8)((((u16)x)>>8)&0xFF))

/*
* \brief Macro to convert 16 bit register value to a s32
*/
#define DRX_U16TODRXFREQ(x)   ((x & 0x8000) ? \
				 ((s32) \
				    (((u32) x) | 0xFFFF0000)) : \
				 ((s32) x))

/*-------------------------------------------------------------------------
ENUM
-------------------------------------------------------------------------*/

/*
* \enum enum drx_standard
* \brief Modulation standards.
*/
enum drx_standard {
	DRX_STANDARD_DVBT = 0, /*< Terrestrial DVB-T.               */
	DRX_STANDARD_8VSB,     /*< Terrestrial 8VSB.                */
	DRX_STANDARD_NTSC,     /*< Terrestrial\Cable analog NTSC.   */
	DRX_STANDARD_PAL_SECAM_BG,
				/*< Terrestrial analog PAL/SECAM B/G */
	DRX_STANDARD_PAL_SECAM_DK,
				/*< Terrestrial analog PAL/SECAM D/K */
	DRX_STANDARD_PAL_SECAM_I,
				/*< Terrestrial analog PAL/SECAM I   */
	DRX_STANDARD_PAL_SECAM_L,
				/*< Terrestrial analog PAL/SECAM L
					with negative modulation        */
	DRX_STANDARD_PAL_SECAM_LP,
				/*< Terrestrial analog PAL/SECAM L
					with positive modulation        */
	DRX_STANDARD_ITU_A,    /*< Cable ITU ANNEX A.               */
	DRX_STANDARD_ITU_B,    /*< Cable ITU ANNEX B.               */
	DRX_STANDARD_ITU_C,    /*< Cable ITU ANNEX C.               */
	DRX_STANDARD_ITU_D,    /*< Cable ITU ANNEX D.               */
	DRX_STANDARD_FM,       /*< Terrestrial\Cable FM radio       */
	DRX_STANDARD_DTMB,     /*< Terrestrial DTMB standard (China)*/
	DRX_STANDARD_UNKNOWN = DRX_UNKNOWN,
				/*< Standard unknown.                */
	DRX_STANDARD_AUTO = DRX_AUTO
				/*< Autodetect standard.             */
};

/*
* \enum enum drx_standard
* \brief Modulation sub-standards.
*/
enum drx_substandard {
	DRX_SUBSTANDARD_MAIN = 0, /*< Main subvariant of standard   */
	DRX_SUBSTANDARD_ATV_BG_SCANDINAVIA,
	DRX_SUBSTANDARD_ATV_DK_POLAND,
	DRX_SUBSTANDARD_ATV_DK_CHINA,
	DRX_SUBSTANDARD_UNKNOWN = DRX_UNKNOWN,
					/*< Sub-standard unknown.         */
	DRX_SUBSTANDARD_AUTO = DRX_AUTO
					/*< Auto (default) sub-standard   */
};

/*
* \enum enum drx_bandwidth
* \brief Channel bandwidth or channel spacing.
*/
enum drx_bandwidth {
	DRX_BANDWIDTH_8MHZ = 0,	 /*< Bandwidth 8 MHz.   */
	DRX_BANDWIDTH_7MHZ,	 /*< Bandwidth 7 MHz.   */
	DRX_BANDWIDTH_6MHZ,	 /*< Bandwidth 6 MHz.   */
	DRX_BANDWIDTH_UNKNOWN = DRX_UNKNOWN,
					/*< Bandwidth unknown. */
	DRX_BANDWIDTH_AUTO = DRX_AUTO
					/*< Auto Set Bandwidth */
};

/*
* \enum enum drx_mirror
* \brief Indicate if channel spectrum is mirrored or not.
*/
enum drx_mirror {
	DRX_MIRROR_NO = 0,   /*< Spectrum is not mirrored.           */
	DRX_MIRROR_YES,	     /*< Spectrum is mirrored.               */
	DRX_MIRROR_UNKNOWN = DRX_UNKNOWN,
				/*< Unknown if spectrum is mirrored.    */
	DRX_MIRROR_AUTO = DRX_AUTO
				/*< Autodetect if spectrum is mirrored. */
};

/*
* \enum enum drx_modulation
* \brief Constellation type of the channel.
*/
enum drx_modulation {
	DRX_CONSTELLATION_BPSK = 0,  /*< Modulation is BPSK.       */
	DRX_CONSTELLATION_QPSK,	     /*< Constellation is QPSK.    */
	DRX_CONSTELLATION_PSK8,	     /*< Constellation is PSK8.    */
	DRX_CONSTELLATION_QAM16,     /*< Constellation is QAM16.   */
	DRX_CONSTELLATION_QAM32,     /*< Constellation is QAM32.   */
	DRX_CONSTELLATION_QAM64,     /*< Constellation is QAM64.   */
	DRX_CONSTELLATION_QAM128,    /*< Constellation is QAM128.  */
	DRX_CONSTELLATION_QAM256,    /*< Constellation is QAM256.  */
	DRX_CONSTELLATION_QAM512,    /*< Constellation is QAM512.  */
	DRX_CONSTELLATION_QAM1024,   /*< Constellation is QAM1024. */
	DRX_CONSTELLATION_QPSK_NR,   /*< Constellation is QPSK_NR  */
	DRX_CONSTELLATION_UNKNOWN = DRX_UNKNOWN,
					/*< Constellation unknown.    */
	DRX_CONSTELLATION_AUTO = DRX_AUTO
					/*< Autodetect constellation. */
};

/*
* \enum enum drx_hierarchy
* \brief Hierarchy of the channel.
*/
enum drx_hierarchy {
	DRX_HIERARCHY_NONE = 0,	/*< None hierarchical channel.     */
	DRX_HIERARCHY_ALPHA1,	/*< Hierarchical channel, alpha=1. */
	DRX_HIERARCHY_ALPHA2,	/*< Hierarchical channel, alpha=2. */
	DRX_HIERARCHY_ALPHA4,	/*< Hierarchical channel, alpha=4. */
	DRX_HIERARCHY_UNKNOWN = DRX_UNKNOWN,
				/*< Hierarchy unknown.             */
	DRX_HIERARCHY_AUTO = DRX_AUTO
				/*< Autodetect hierarchy.          */
};

/*
* \enum enum drx_priority
* \brief Channel priority in case of hierarchical transmission.
*/
enum drx_priority {
	DRX_PRIORITY_LOW = 0,  /*< Low priority channel.  */
	DRX_PRIORITY_HIGH,     /*< High priority channel. */
	DRX_PRIORITY_UNKNOWN = DRX_UNKNOWN
				/*< Priority unknown.      */
};

/*
* \enum enum drx_coderate
* \brief Channel priority in case of hierarchical transmission.
*/
enum drx_coderate {
		DRX_CODERATE_1DIV2 = 0,	/*< Code rate 1/2nd.      */
		DRX_CODERATE_2DIV3,	/*< Code rate 2/3nd.      */
		DRX_CODERATE_3DIV4,	/*< Code rate 3/4nd.      */
		DRX_CODERATE_5DIV6,	/*< Code rate 5/6nd.      */
		DRX_CODERATE_7DIV8,	/*< Code rate 7/8nd.      */
		DRX_CODERATE_UNKNOWN = DRX_UNKNOWN,
					/*< Code rate unknown.    */
		DRX_CODERATE_AUTO = DRX_AUTO
					/*< Autodetect code rate. */
};

/*
* \enum enum drx_guard
* \brief Guard interval of a channel.
*/
enum drx_guard {
	DRX_GUARD_1DIV32 = 0, /*< Guard interval 1/32nd.     */
	DRX_GUARD_1DIV16,     /*< Guard interval 1/16th.     */
	DRX_GUARD_1DIV8,      /*< Guard interval 1/8th.      */
	DRX_GUARD_1DIV4,      /*< Guard interval 1/4th.      */
	DRX_GUARD_UNKNOWN = DRX_UNKNOWN,
				/*< Guard interval unknown.    */
	DRX_GUARD_AUTO = DRX_AUTO
				/*< Autodetect guard interval. */
};

/*
* \enum enum drx_fft_mode
* \brief FFT mode.
*/
enum drx_fft_mode {
	DRX_FFTMODE_2K = 0,    /*< 2K FFT mode.         */
	DRX_FFTMODE_4K,	       /*< 4K FFT mode.         */
	DRX_FFTMODE_8K,	       /*< 8K FFT mode.         */
	DRX_FFTMODE_UNKNOWN = DRX_UNKNOWN,
				/*< FFT mode unknown.    */
	DRX_FFTMODE_AUTO = DRX_AUTO
				/*< Autodetect FFT mode. */
};

/*
* \enum enum drx_classification
* \brief Channel classification.
*/
enum drx_classification {
	DRX_CLASSIFICATION_GAUSS = 0, /*< Gaussion noise.            */
	DRX_CLASSIFICATION_HVY_GAUSS, /*< Heavy Gaussion noise.      */
	DRX_CLASSIFICATION_COCHANNEL, /*< Co-channel.                */
	DRX_CLASSIFICATION_STATIC,    /*< Static echo.               */
	DRX_CLASSIFICATION_MOVING,    /*< Moving echo.               */
	DRX_CLASSIFICATION_ZERODB,    /*< Zero dB echo.              */
	DRX_CLASSIFICATION_UNKNOWN = DRX_UNKNOWN,
					/*< Unknown classification     */
	DRX_CLASSIFICATION_AUTO = DRX_AUTO
					/*< Autodetect classification. */
};

/*
* /enum enum drx_interleave_mode
* /brief Interleave modes
*/
enum drx_interleave_mode {
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
					/*< Unknown interleave mode    */
	DRX_INTERLEAVEMODE_AUTO = DRX_AUTO
					/*< Autodetect interleave mode */
};

/*
* \enum enum drx_carrier_mode
* \brief Channel Carrier Mode.
*/
enum drx_carrier_mode {
	DRX_CARRIER_MULTI = 0,		/*< Multi carrier mode       */
	DRX_CARRIER_SINGLE,		/*< Single carrier mode      */
	DRX_CARRIER_UNKNOWN = DRX_UNKNOWN,
					/*< Carrier mode unknown.    */
	DRX_CARRIER_AUTO = DRX_AUTO	/*< Autodetect carrier mode  */
};

/*
* \enum enum drx_frame_mode
* \brief Channel Frame Mode.
*/
enum drx_frame_mode {
	DRX_FRAMEMODE_420 = 0,	 /*< 420 with variable PN  */
	DRX_FRAMEMODE_595,	 /*< 595                   */
	DRX_FRAMEMODE_945,	 /*< 945 with variable PN  */
	DRX_FRAMEMODE_420_FIXED_PN,
					/*< 420 with fixed PN     */
	DRX_FRAMEMODE_945_FIXED_PN,
					/*< 945 with fixed PN     */
	DRX_FRAMEMODE_UNKNOWN = DRX_UNKNOWN,
					/*< Frame mode unknown.   */
	DRX_FRAMEMODE_AUTO = DRX_AUTO
					/*< Autodetect frame mode */
};

/*
* \enum enum drx_tps_frame
* \brief Frame number in current super-frame.
*/
enum drx_tps_frame {
	DRX_TPS_FRAME1 = 0,	  /*< TPS frame 1.       */
	DRX_TPS_FRAME2,		  /*< TPS frame 2.       */
	DRX_TPS_FRAME3,		  /*< TPS frame 3.       */
	DRX_TPS_FRAME4,		  /*< TPS frame 4.       */
	DRX_TPS_FRAME_UNKNOWN = DRX_UNKNOWN
					/*< TPS frame unknown. */
};

/*
* \enum enum drx_ldpc
* \brief TPS LDPC .
*/
enum drx_ldpc {
	DRX_LDPC_0_4 = 0,	  /*< LDPC 0.4           */
	DRX_LDPC_0_6,		  /*< LDPC 0.6           */
	DRX_LDPC_0_8,		  /*< LDPC 0.8           */
	DRX_LDPC_UNKNOWN = DRX_UNKNOWN,
					/*< LDPC unknown.      */
	DRX_LDPC_AUTO = DRX_AUTO  /*< Autodetect LDPC    */
};

/*
* \enum enum drx_pilot_mode
* \brief Pilot modes in DTMB.
*/
enum drx_pilot_mode {
	DRX_PILOT_ON = 0,	  /*< Pilot On             */
	DRX_PILOT_OFF,		  /*< Pilot Off            */
	DRX_PILOT_UNKNOWN = DRX_UNKNOWN,
					/*< Pilot unknown.       */
	DRX_PILOT_AUTO = DRX_AUTO /*< Autodetect Pilot     */
};

/*
 * enum drxu_code_action - indicate if firmware has to be uploaded or verified.
 * @UCODE_UPLOAD:	Upload the microcode image to device
 * @UCODE_VERIFY:	Compare microcode image with code on device
 */
enum drxu_code_action {
	UCODE_UPLOAD,
	UCODE_VERIFY
};

/*
* \enum enum drx_lock_status * \brief Used to reflect current lock status of demodulator.
*
* The generic lock states have device dependent semantics.

		DRX_NEVER_LOCK = 0,
			      **< Device will never lock on this signal *
		DRX_NOT_LOCKED,
			      **< Device has no lock at all             *
		DRX_LOCK_STATE_1,
			      **< Generic lock state                    *
		DRX_LOCK_STATE_2,
			      **< Generic lock state                    *
		DRX_LOCK_STATE_3,
			      **< Generic lock state                    *
		DRX_LOCK_STATE_4,
			      **< Generic lock state                    *
		DRX_LOCK_STATE_5,
			      **< Generic lock state                    *
		DRX_LOCK_STATE_6,
			      **< Generic lock state                    *
		DRX_LOCK_STATE_7,
			      **< Generic lock state                    *
		DRX_LOCK_STATE_8,
			      **< Generic lock state                    *
		DRX_LOCK_STATE_9,
			      **< Generic lock state                    *
		DRX_LOCKED    **< Device is in lock                     *
*/

enum drx_lock_status {
	DRX_NEVER_LOCK = 0,
	DRX_NOT_LOCKED,
	DRX_LOCK_STATE_1,
	DRX_LOCK_STATE_2,
	DRX_LOCK_STATE_3,
	DRX_LOCK_STATE_4,
	DRX_LOCK_STATE_5,
	DRX_LOCK_STATE_6,
	DRX_LOCK_STATE_7,
	DRX_LOCK_STATE_8,
	DRX_LOCK_STATE_9,
	DRX_LOCKED
};

/*
* \enum enum drx_uio* \brief Used to address a User IO (UIO).
*/
enum drx_uio {
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
};

/*
* \enum enum drxuio_mode * \brief Used to configure the modus oprandi of a UIO.
*
* DRX_UIO_MODE_FIRMWARE is an old uio mode.
* It is replaced by the modes DRX_UIO_MODE_FIRMWARE0 .. DRX_UIO_MODE_FIRMWARE9.
* To be backward compatible DRX_UIO_MODE_FIRMWARE is equivalent to
* DRX_UIO_MODE_FIRMWARE0.
*/
enum drxuio_mode {
	DRX_UIO_MODE_DISABLE = 0x01,
			    /*< not used, pin is configured as input */
	DRX_UIO_MODE_READWRITE = 0x02,
			    /*< used for read/write by application   */
	DRX_UIO_MODE_FIRMWARE = 0x04,
			    /*< controlled by firmware, function 0   */
	DRX_UIO_MODE_FIRMWARE0 = DRX_UIO_MODE_FIRMWARE,
					    /*< same as above        */
	DRX_UIO_MODE_FIRMWARE1 = 0x08,
			    /*< controlled by firmware, function 1   */
	DRX_UIO_MODE_FIRMWARE2 = 0x10,
			    /*< controlled by firmware, function 2   */
	DRX_UIO_MODE_FIRMWARE3 = 0x20,
			    /*< controlled by firmware, function 3   */
	DRX_UIO_MODE_FIRMWARE4 = 0x40,
			    /*< controlled by firmware, function 4   */
	DRX_UIO_MODE_FIRMWARE5 = 0x80
			    /*< controlled by firmware, function 5   */
};

/*
* \enum enum drxoob_downstream_standard * \brief Used to select OOB standard.
*
* Based on ANSI 55-1 and 55-2
*/
enum drxoob_downstream_standard {
	DRX_OOB_MODE_A = 0,
		       /*< ANSI 55-1   */
	DRX_OOB_MODE_B_GRADE_A,
		       /*< ANSI 55-2 A */
	DRX_OOB_MODE_B_GRADE_B
		       /*< ANSI 55-2 B */
};

/*-------------------------------------------------------------------------
STRUCTS
-------------------------------------------------------------------------*/

/*============================================================================*/
/*============================================================================*/
/*== CTRL CFG related data structures ========================================*/
/*============================================================================*/
/*============================================================================*/

#ifndef DRX_CFG_BASE
#define DRX_CFG_BASE          0
#endif

#define DRX_CFG_MPEG_OUTPUT         (DRX_CFG_BASE +  0)	/* MPEG TS output    */
#define DRX_CFG_PKTERR              (DRX_CFG_BASE +  1)	/* Packet Error      */
#define DRX_CFG_SYMCLK_OFFS         (DRX_CFG_BASE +  2)	/* Symbol Clk Offset */
#define DRX_CFG_SMA                 (DRX_CFG_BASE +  3)	/* Smart Antenna     */
#define DRX_CFG_PINSAFE             (DRX_CFG_BASE +  4)	/* Pin safe mode     */
#define DRX_CFG_SUBSTANDARD         (DRX_CFG_BASE +  5)	/* substandard       */
#define DRX_CFG_AUD_VOLUME          (DRX_CFG_BASE +  6)	/* volume            */
#define DRX_CFG_AUD_RDS             (DRX_CFG_BASE +  7)	/* rds               */
#define DRX_CFG_AUD_AUTOSOUND       (DRX_CFG_BASE +  8)	/* ASS & ASC         */
#define DRX_CFG_AUD_ASS_THRES       (DRX_CFG_BASE +  9)	/* ASS Thresholds    */
#define DRX_CFG_AUD_DEVIATION       (DRX_CFG_BASE + 10)	/* Deviation         */
#define DRX_CFG_AUD_PRESCALE        (DRX_CFG_BASE + 11)	/* Prescale          */
#define DRX_CFG_AUD_MIXER           (DRX_CFG_BASE + 12)	/* Mixer             */
#define DRX_CFG_AUD_AVSYNC          (DRX_CFG_BASE + 13)	/* AVSync            */
#define DRX_CFG_AUD_CARRIER         (DRX_CFG_BASE + 14)	/* Audio carriers    */
#define DRX_CFG_I2S_OUTPUT          (DRX_CFG_BASE + 15)	/* I2S output        */
#define DRX_CFG_ATV_STANDARD        (DRX_CFG_BASE + 16)	/* ATV standard      */
#define DRX_CFG_SQI_SPEED           (DRX_CFG_BASE + 17)	/* SQI speed         */
#define DRX_CTRL_CFG_MAX            (DRX_CFG_BASE + 18)	/* never to be used  */

#define DRX_CFG_PINS_SAFE_MODE      DRX_CFG_PINSAFE
/*============================================================================*/
/*============================================================================*/
/*== CTRL related data structures ============================================*/
/*============================================================================*/
/*============================================================================*/

/*
 * struct drxu_code_info	Parameters for microcode upload and verfiy.
 *
 * @mc_file:	microcode file name
 *
 * Used by DRX_CTRL_LOAD_UCODE and DRX_CTRL_VERIFY_UCODE
 */
struct drxu_code_info {
	char 			*mc_file;
};

/*
* \struct drx_mc_version_rec_t
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
* - mc_dev_type == 0         => any device allowed
* - mc_base_version == 0.0.0 => full microcode (mc_version is the version)
* - mc_base_version != 0.0.0 => patch microcode, the base microcode version
*                             (mc_version is the version)
*/
#define AUX_VER_RECORD 0x8000

struct drx_mc_version_rec {
	u16 aux_type;	/* type of aux data - 0x8000 for version record     */
	u32 mc_dev_type;	/* device type, based on JTAG ID                    */
	u32 mc_version;	/* version of microcode                             */
	u32 mc_base_version;	/* in case of patch: the original microcode version */
};

/*========================================*/

/*
* \struct drx_filter_info_t
* \brief Parameters for loading filter coefficients
*
* Used by DRX_CTRL_LOAD_FILTER
*/
struct drx_filter_info {
	u8 *data_re;
	      /*< pointer to coefficients for RE */
	u8 *data_im;
	      /*< pointer to coefficients for IM */
	u16 size_re;
	      /*< size of coefficients for RE    */
	u16 size_im;
	      /*< size of coefficients for IM    */
};

/*========================================*/

/*
* \struct struct drx_channel * \brief The set of parameters describing a single channel.
*
* Used by DRX_CTRL_SET_CHANNEL and DRX_CTRL_GET_CHANNEL.
* Only certain fields need to be used for a specfic standard.
*
*/
struct drx_channel {
	s32 frequency;
				/*< frequency in kHz                 */
	enum drx_bandwidth bandwidth;
				/*< bandwidth                        */
	enum drx_mirror mirror;	/*< mirrored or not on RF            */
	enum drx_modulation constellation;
				/*< constellation                    */
	enum drx_hierarchy hierarchy;
				/*< hierarchy                        */
	enum drx_priority priority;	/*< priority                         */
	enum drx_coderate coderate;	/*< coderate                         */
	enum drx_guard guard;	/*< guard interval                   */
	enum drx_fft_mode fftmode;	/*< fftmode                          */
	enum drx_classification classification;
				/*< classification                   */
	u32 symbolrate;
				/*< symbolrate in symbols/sec        */
	enum drx_interleave_mode interleavemode;
				/*< interleaveMode QAM               */
	enum drx_ldpc ldpc;		/*< ldpc                             */
	enum drx_carrier_mode carrier;	/*< carrier                          */
	enum drx_frame_mode framemode;
				/*< frame mode                       */
	enum drx_pilot_mode pilot;	/*< pilot mode                       */
};

/*========================================*/

enum drx_cfg_sqi_speed {
	DRX_SQI_SPEED_FAST = 0,
	DRX_SQI_SPEED_MEDIUM,
	DRX_SQI_SPEED_SLOW,
	DRX_SQI_SPEED_UNKNOWN = DRX_UNKNOWN
};

/*========================================*/

/*
* \struct struct drx_complex * A complex number.
*
* Used by DRX_CTRL_CONSTEL.
*/
struct drx_complex {
	s16 im;
     /*< Imaginary part. */
	s16 re;
     /*< Real part.      */
};

/*========================================*/

/*
* \struct struct drx_frequency_plan * Array element of a frequency plan.
*
* Used by DRX_CTRL_SCAN_INIT.
*/
struct drx_frequency_plan {
	s32 first;
		     /*< First centre frequency in this band        */
	s32 last;
		     /*< Last centre frequency in this band         */
	s32 step;
		     /*< Stepping frequency in this band            */
	enum drx_bandwidth bandwidth;
		     /*< Bandwidth within this frequency band       */
	u16 ch_number;
		     /*< First channel number in this band, or first
			    index in ch_names                         */
	char **ch_names;
		     /*< Optional list of channel names in this
			    band                                     */
};

/*========================================*/

/*
* \struct struct drx_scan_param * Parameters for channel scan.
*
* Used by DRX_CTRL_SCAN_INIT.
*/
struct drx_scan_param {
	struct drx_frequency_plan *frequency_plan;
				  /*< Frequency plan (array)*/
	u16 frequency_plan_size;  /*< Number of bands       */
	u32 num_tries;		  /*< Max channels tried    */
	s32 skip;	  /*< Minimum frequency step to take
					after a channel is found */
	void *ext_params;	  /*< Standard specific params */
};

/*========================================*/

/*
* \brief Scan commands.
* Used by scanning algorithms.
*/
enum drx_scan_command {
		DRX_SCAN_COMMAND_INIT = 0,/*< Initialize scanning */
		DRX_SCAN_COMMAND_NEXT,	  /*< Next scan           */
		DRX_SCAN_COMMAND_STOP	  /*< Stop scanning       */
};

/*========================================*/

/*
* \brief Inner scan function prototype.
*/
typedef int(*drx_scan_func_t) (void *scan_context,
				     enum drx_scan_command scan_command,
				     struct drx_channel *scan_channel,
				     bool *get_next_channel);

/*========================================*/

/*
* \struct struct drxtps_info * TPS information, DVB-T specific.
*
* Used by DRX_CTRL_TPS_INFO.
*/
	struct drxtps_info {
		enum drx_fft_mode fftmode;	/*< Fft mode       */
		enum drx_guard guard;	/*< Guard interval */
		enum drx_modulation constellation;
					/*< Constellation  */
		enum drx_hierarchy hierarchy;
					/*< Hierarchy      */
		enum drx_coderate high_coderate;
					/*< High code rate */
		enum drx_coderate low_coderate;
					/*< Low cod rate   */
		enum drx_tps_frame frame;	/*< Tps frame      */
		u8 length;		/*< Length         */
		u16 cell_id;		/*< Cell id        */
	};

/*========================================*/

/*
* \brief Power mode of device.
*
* Used by DRX_CTRL_SET_POWER_MODE.
*/
	enum drx_power_mode {
		DRX_POWER_UP = 0,
			 /*< Generic         , Power Up Mode   */
		DRX_POWER_MODE_1,
			 /*< Device specific , Power Up Mode   */
		DRX_POWER_MODE_2,
			 /*< Device specific , Power Up Mode   */
		DRX_POWER_MODE_3,
			 /*< Device specific , Power Up Mode   */
		DRX_POWER_MODE_4,
			 /*< Device specific , Power Up Mode   */
		DRX_POWER_MODE_5,
			 /*< Device specific , Power Up Mode   */
		DRX_POWER_MODE_6,
			 /*< Device specific , Power Up Mode   */
		DRX_POWER_MODE_7,
			 /*< Device specific , Power Up Mode   */
		DRX_POWER_MODE_8,
			 /*< Device specific , Power Up Mode   */

		DRX_POWER_MODE_9,
			 /*< Device specific , Power Down Mode */
		DRX_POWER_MODE_10,
			 /*< Device specific , Power Down Mode */
		DRX_POWER_MODE_11,
			 /*< Device specific , Power Down Mode */
		DRX_POWER_MODE_12,
			 /*< Device specific , Power Down Mode */
		DRX_POWER_MODE_13,
			 /*< Device specific , Power Down Mode */
		DRX_POWER_MODE_14,
			 /*< Device specific , Power Down Mode */
		DRX_POWER_MODE_15,
			 /*< Device specific , Power Down Mode */
		DRX_POWER_MODE_16,
			 /*< Device specific , Power Down Mode */
		DRX_POWER_DOWN = 255
			 /*< Generic         , Power Down Mode */
	};

/*========================================*/

/*
* \enum enum drx_module * \brief Software module identification.
*
* Used by DRX_CTRL_VERSION.
*/
	enum drx_module {
		DRX_MODULE_DEVICE,
		DRX_MODULE_MICROCODE,
		DRX_MODULE_DRIVERCORE,
		DRX_MODULE_DEVICEDRIVER,
		DRX_MODULE_DAP,
		DRX_MODULE_BSP_I2C,
		DRX_MODULE_BSP_TUNER,
		DRX_MODULE_BSP_HOST,
		DRX_MODULE_UNKNOWN
	};

/*
* \enum struct drx_version * \brief Version information of one software module.
*
* Used by DRX_CTRL_VERSION.
*/
	struct drx_version {
		enum drx_module module_type;
			       /*< Type identifier of the module */
		char *module_name;
			       /*< Name or description of module */
		u16 v_major;  /*< Major version number          */
		u16 v_minor;  /*< Minor version number          */
		u16 v_patch;  /*< Patch version number          */
		char *v_string; /*< Version as text string        */
	};

/*
* \enum struct drx_version_list * \brief List element of NULL terminated, linked list for version information.
*
* Used by DRX_CTRL_VERSION.
*/
struct drx_version_list {
	struct drx_version *version;/*< Version information */
	struct drx_version_list *next;
			      /*< Next list element   */
};

/*========================================*/

/*
* \brief Parameters needed to confiugure a UIO.
*
* Used by DRX_CTRL_UIO_CFG.
*/
	struct drxuio_cfg {
		enum drx_uio uio;
		       /*< UIO identifier       */
		enum drxuio_mode mode;
		       /*< UIO operational mode */
	};

/*========================================*/

/*
* \brief Parameters needed to read from or write to a UIO.
*
* Used by DRX_CTRL_UIO_READ and DRX_CTRL_UIO_WRITE.
*/
	struct drxuio_data {
		enum drx_uio uio;
		   /*< UIO identifier              */
		bool value;
		   /*< UIO value (true=1, false=0) */
	};

/*========================================*/

/*
* \brief Parameters needed to configure OOB.
*
* Used by DRX_CTRL_SET_OOB.
*/
	struct drxoob {
		s32 frequency;	   /*< Frequency in kHz      */
		enum drxoob_downstream_standard standard;
						   /*< OOB standard          */
		bool spectrum_inverted;	   /*< If true, then spectrum
							 is inverted          */
	};

/*========================================*/

/*
* \brief Metrics from OOB.
*
* Used by DRX_CTRL_GET_OOB.
*/
	struct drxoob_status {
		s32 frequency; /*< Frequency in Khz         */
		enum drx_lock_status lock;	  /*< Lock status              */
		u32 mer;		  /*< MER                      */
		s32 symbol_rate_offset;	  /*< Symbolrate offset in ppm */
	};

/*========================================*/

/*
* \brief Device dependent configuration data.
*
* Used by DRX_CTRL_SET_CFG and DRX_CTRL_GET_CFG.
* A sort of nested drx_ctrl() functionality for device specific controls.
*/
	struct drx_cfg {
		u32 cfg_type;
			  /*< Function identifier */
		void *cfg_data;
			  /*< Function data */
	};

/*========================================*/

/*
* /struct DRXMpegStartWidth_t
* MStart width [nr MCLK cycles] for serial MPEG output.
*/

	enum drxmpeg_str_width {
		DRX_MPEG_STR_WIDTH_1,
		DRX_MPEG_STR_WIDTH_8
	};

/* CTRL CFG MPEG output */
/*
* \struct struct drx_cfg_mpeg_output * \brief Configuration parameters for MPEG output control.
*
* Used by DRX_CFG_MPEG_OUTPUT, in combination with DRX_CTRL_SET_CFG and
* DRX_CTRL_GET_CFG.
*/

	struct drx_cfg_mpeg_output {
		bool enable_mpeg_output;/*< If true, enable MPEG output      */
		bool insert_rs_byte;	/*< If true, insert RS byte          */
		bool enable_parallel;	/*< If true, parallel out otherwise
								     serial   */
		bool invert_data;	/*< If true, invert DATA signals     */
		bool invert_err;	/*< If true, invert ERR signal       */
		bool invert_str;	/*< If true, invert STR signals      */
		bool invert_val;	/*< If true, invert VAL signals      */
		bool invert_clk;	/*< If true, invert CLK signals      */
		bool static_clk;	/*< If true, static MPEG clockrate
					     will be used, otherwise clockrate
					     will adapt to the bitrate of the
					     TS                               */
		u32 bitrate;		/*< Maximum bitrate in b/s in case
					     static clockrate is selected     */
		enum drxmpeg_str_width width_str;
					/*< MPEG start width                 */
	};


/*========================================*/

/*
* \struct struct drxi2c_data * \brief Data for I2C via 2nd or 3rd or etc I2C port.
*
* Used by DRX_CTRL_I2C_READWRITE.
* If port_nr is equal to primairy port_nr BSPI2C will be used.
*
*/
	struct drxi2c_data {
		u16 port_nr;	/*< I2C port number               */
		struct i2c_device_addr *w_dev_addr;
				/*< Write device address          */
		u16 w_count;	/*< Size of write data in bytes   */
		u8 *wData;	/*< Pointer to write data         */
		struct i2c_device_addr *r_dev_addr;
				/*< Read device address           */
		u16 r_count;	/*< Size of data to read in bytes */
		u8 *r_data;	/*< Pointer to read buffer        */
	};

/*========================================*/

/*
* \enum enum drx_aud_standard * \brief Audio standard identifier.
*
* Used by DRX_CTRL_SET_AUD.
*/
	enum drx_aud_standard {
		DRX_AUD_STANDARD_BTSC,	   /*< set BTSC standard (USA)       */
		DRX_AUD_STANDARD_A2,	   /*< set A2-Korea FM Stereo        */
		DRX_AUD_STANDARD_EIAJ,	   /*< set to Japanese FM Stereo     */
		DRX_AUD_STANDARD_FM_STEREO,/*< set to FM-Stereo Radio        */
		DRX_AUD_STANDARD_M_MONO,   /*< for 4.5 MHz mono detected     */
		DRX_AUD_STANDARD_D_K_MONO, /*< for 6.5 MHz mono detected     */
		DRX_AUD_STANDARD_BG_FM,	   /*< set BG_FM standard            */
		DRX_AUD_STANDARD_D_K1,	   /*< set D_K1 standard             */
		DRX_AUD_STANDARD_D_K2,	   /*< set D_K2 standard             */
		DRX_AUD_STANDARD_D_K3,	   /*< set D_K3 standard             */
		DRX_AUD_STANDARD_BG_NICAM_FM,
					   /*< set BG_NICAM_FM standard      */
		DRX_AUD_STANDARD_L_NICAM_AM,
					   /*< set L_NICAM_AM standard       */
		DRX_AUD_STANDARD_I_NICAM_FM,
					   /*< set I_NICAM_FM standard       */
		DRX_AUD_STANDARD_D_K_NICAM_FM,
					   /*< set D_K_NICAM_FM standard     */
		DRX_AUD_STANDARD_NOT_READY,/*< used to detect audio standard */
		DRX_AUD_STANDARD_AUTO = DRX_AUTO,
					   /*< Automatic Standard Detection  */
		DRX_AUD_STANDARD_UNKNOWN = DRX_UNKNOWN
					   /*< used as auto and for readback */
	};

/* CTRL_AUD_GET_STATUS    - struct drx_aud_status */
/*
* \enum enum drx_aud_nicam_status * \brief Status of NICAM carrier.
*/
	enum drx_aud_nicam_status {
		DRX_AUD_NICAM_DETECTED = 0,
					  /*< NICAM carrier detected         */
		DRX_AUD_NICAM_NOT_DETECTED,
					  /*< NICAM carrier not detected     */
		DRX_AUD_NICAM_BAD	  /*< NICAM carrier bad quality      */
	};

/*
* \struct struct drx_aud_status * \brief Audio status characteristics.
*/
	struct drx_aud_status {
		bool stereo;		  /*< stereo detection               */
		bool carrier_a;	  /*< carrier A detected             */
		bool carrier_b;	  /*< carrier B detected             */
		bool sap;		  /*< sap / bilingual detection      */
		bool rds;		  /*< RDS data array present         */
		enum drx_aud_nicam_status nicam_status;
					  /*< status of NICAM carrier        */
		s8 fm_ident;		  /*< FM Identification value        */
	};

/* CTRL_AUD_READ_RDS       - DRXRDSdata_t */

/*
* \struct DRXRDSdata_t
* \brief Raw RDS data array.
*/
	struct drx_cfg_aud_rds {
		bool valid;		  /*< RDS data validation            */
		u16 data[18];		  /*< data from one RDS data array   */
	};

/* DRX_CFG_AUD_VOLUME      - struct drx_cfg_aud_volume - set/get */
/*
* \enum DRXAudAVCDecayTime_t
* \brief Automatic volume control configuration.
*/
	enum drx_aud_avc_mode {
		DRX_AUD_AVC_OFF,	  /*< Automatic volume control off   */
		DRX_AUD_AVC_DECAYTIME_8S, /*< level volume in  8 seconds     */
		DRX_AUD_AVC_DECAYTIME_4S, /*< level volume in  4 seconds     */
		DRX_AUD_AVC_DECAYTIME_2S, /*< level volume in  2 seconds     */
		DRX_AUD_AVC_DECAYTIME_20MS/*< level volume in 20 millisec    */
	};

/*
* /enum DRXAudMaxAVCGain_t
* /brief Automatic volume control max gain in audio baseband.
*/
	enum drx_aud_avc_max_gain {
		DRX_AUD_AVC_MAX_GAIN_0DB, /*< maximum AVC gain  0 dB         */
		DRX_AUD_AVC_MAX_GAIN_6DB, /*< maximum AVC gain  6 dB         */
		DRX_AUD_AVC_MAX_GAIN_12DB /*< maximum AVC gain 12 dB         */
	};

/*
* /enum DRXAudMaxAVCAtten_t
* /brief Automatic volume control max attenuation in audio baseband.
*/
	enum drx_aud_avc_max_atten {
		DRX_AUD_AVC_MAX_ATTEN_12DB,
					  /*< maximum AVC attenuation 12 dB  */
		DRX_AUD_AVC_MAX_ATTEN_18DB,
					  /*< maximum AVC attenuation 18 dB  */
		DRX_AUD_AVC_MAX_ATTEN_24DB/*< maximum AVC attenuation 24 dB  */
	};
/*
* \struct struct drx_cfg_aud_volume * \brief Audio volume configuration.
*/
	struct drx_cfg_aud_volume {
		bool mute;		  /*< mute overrides volume setting  */
		s16 volume;		  /*< volume, range -114 to 12 dB    */
		enum drx_aud_avc_mode avc_mode;  /*< AVC auto volume control mode   */
		u16 avc_ref_level;	  /*< AVC reference level            */
		enum drx_aud_avc_max_gain avc_max_gain;
					  /*< AVC max gain selection         */
		enum drx_aud_avc_max_atten avc_max_atten;
					  /*< AVC max attenuation selection  */
		s16 strength_left;	  /*< quasi-peak, left speaker       */
		s16 strength_right;	  /*< quasi-peak, right speaker      */
	};

/* DRX_CFG_I2S_OUTPUT      - struct drx_cfg_i2s_output - set/get */
/*
* \enum enum drxi2s_mode * \brief I2S output mode.
*/
	enum drxi2s_mode {
		DRX_I2S_MODE_MASTER,	  /*< I2S is in master mode          */
		DRX_I2S_MODE_SLAVE	  /*< I2S is in slave mode           */
	};

/*
* \enum enum drxi2s_word_length * \brief Width of I2S data.
*/
	enum drxi2s_word_length {
		DRX_I2S_WORDLENGTH_32 = 0,/*< I2S data is 32 bit wide        */
		DRX_I2S_WORDLENGTH_16 = 1 /*< I2S data is 16 bit wide        */
	};

/*
* \enum enum drxi2s_format * \brief Data wordstrobe alignment for I2S.
*/
	enum drxi2s_format {
		DRX_I2S_FORMAT_WS_WITH_DATA,
				    /*< I2S data and wordstrobe are aligned  */
		DRX_I2S_FORMAT_WS_ADVANCED
				    /*< I2S data one cycle after wordstrobe  */
	};

/*
* \enum enum drxi2s_polarity * \brief Polarity of I2S data.
*/
	enum drxi2s_polarity {
		DRX_I2S_POLARITY_RIGHT,/*< wordstrobe - right high, left low */
		DRX_I2S_POLARITY_LEFT  /*< wordstrobe - right low, left high */
	};

/*
* \struct struct drx_cfg_i2s_output * \brief I2S output configuration.
*/
	struct drx_cfg_i2s_output {
		bool output_enable;	  /*< I2S output enable              */
		u32 frequency;	  /*< range from 8000-48000 Hz       */
		enum drxi2s_mode mode;	  /*< I2S mode, master or slave      */
		enum drxi2s_word_length word_length;
					  /*< I2S wordlength, 16 or 32 bits  */
		enum drxi2s_polarity polarity;/*< I2S wordstrobe polarity        */
		enum drxi2s_format format;	  /*< I2S wordstrobe delay to data   */
	};

/* ------------------------------expert interface-----------------------------*/
/*
* /enum enum drx_aud_fm_deemphasis * setting for FM-Deemphasis in audio demodulator.
*
*/
	enum drx_aud_fm_deemphasis {
		DRX_AUD_FM_DEEMPH_50US,
		DRX_AUD_FM_DEEMPH_75US,
		DRX_AUD_FM_DEEMPH_OFF
	};

/*
* /enum DRXAudDeviation_t
* setting for deviation mode in audio demodulator.
*
*/
	enum drx_cfg_aud_deviation {
		DRX_AUD_DEVIATION_NORMAL,
		DRX_AUD_DEVIATION_HIGH
	};

/*
* /enum enum drx_no_carrier_option * setting for carrier, mute/noise.
*
*/
	enum drx_no_carrier_option {
		DRX_NO_CARRIER_MUTE,
		DRX_NO_CARRIER_NOISE
	};

/*
* \enum DRXAudAutoSound_t
* \brief Automatic Sound
*/
	enum drx_cfg_aud_auto_sound {
		DRX_AUD_AUTO_SOUND_OFF = 0,
		DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_ON,
		DRX_AUD_AUTO_SOUND_SELECT_ON_CHANGE_OFF
	};

/*
* \enum DRXAudASSThres_t
* \brief Automatic Sound Select Thresholds
*/
	struct drx_cfg_aud_ass_thres {
		u16 a2;	/* A2 Threshold for ASS configuration */
		u16 btsc;	/* BTSC Threshold for ASS configuration */
		u16 nicam;	/* Nicam Threshold for ASS configuration */
	};

/*
* \struct struct drx_aud_carrier * \brief Carrier detection related parameters
*/
	struct drx_aud_carrier {
		u16 thres;	/* carrier detetcion threshold for primary carrier (A) */
		enum drx_no_carrier_option opt;	/* Mute or noise at no carrier detection (A) */
		s32 shift;	/* DC level of incoming signal (A) */
		s32 dco;	/* frequency adjustment (A) */
	};

/*
* \struct struct drx_cfg_aud_carriers * \brief combining carrier A & B to one struct
*/
	struct drx_cfg_aud_carriers {
		struct drx_aud_carrier a;
		struct drx_aud_carrier b;
	};

/*
* /enum enum drx_aud_i2s_src * Selection of audio source
*/
	enum drx_aud_i2s_src {
		DRX_AUD_SRC_MONO,
		DRX_AUD_SRC_STEREO_OR_AB,
		DRX_AUD_SRC_STEREO_OR_A,
		DRX_AUD_SRC_STEREO_OR_B};

/*
* \enum enum drx_aud_i2s_matrix * \brief Used for selecting I2S output.
*/
	enum drx_aud_i2s_matrix {
		DRX_AUD_I2S_MATRIX_A_MONO,
					/*< A sound only, stereo or mono     */
		DRX_AUD_I2S_MATRIX_B_MONO,
					/*< B sound only, stereo or mono     */
		DRX_AUD_I2S_MATRIX_STEREO,
					/*< A+B sound, transparant           */
		DRX_AUD_I2S_MATRIX_MONO	/*< A+B mixed to mono sum, (L+R)/2   */};

/*
* /enum enum drx_aud_fm_matrix * setting for FM-Matrix in audio demodulator.
*
*/
	enum drx_aud_fm_matrix {
		DRX_AUD_FM_MATRIX_NO_MATRIX,
		DRX_AUD_FM_MATRIX_GERMAN,
		DRX_AUD_FM_MATRIX_KOREAN,
		DRX_AUD_FM_MATRIX_SOUND_A,
		DRX_AUD_FM_MATRIX_SOUND_B};

/*
* \struct DRXAudMatrices_t
* \brief Mixer settings
*/
struct drx_cfg_aud_mixer {
	enum drx_aud_i2s_src source_i2s;
	enum drx_aud_i2s_matrix matrix_i2s;
	enum drx_aud_fm_matrix matrix_fm;
};

/*
* \enum DRXI2SVidSync_t
* \brief Audio/video synchronization, interacts with I2S mode.
* AUTO_1 and AUTO_2 are for automatic video standard detection with preference
* for NTSC or Monochrome, because the frequencies are too close (59.94 & 60 Hz)
*/
	enum drx_cfg_aud_av_sync {
		DRX_AUD_AVSYNC_OFF,/*< audio/video synchronization is off   */
		DRX_AUD_AVSYNC_NTSC,
				   /*< it is an NTSC system                 */
		DRX_AUD_AVSYNC_MONOCHROME,
				   /*< it is a MONOCHROME system            */
		DRX_AUD_AVSYNC_PAL_SECAM
				   /*< it is a PAL/SECAM system             */};

/*
* \struct struct drx_cfg_aud_prescale * \brief Prescalers
*/
struct drx_cfg_aud_prescale {
	u16 fm_deviation;
	s16 nicam_gain;
};

/*
* \struct struct drx_aud_beep * \brief Beep
*/
struct drx_aud_beep {
	s16 volume;	/* dB */
	u16 frequency;	/* Hz */
	bool mute;
};

/*
* \enum enum drx_aud_btsc_detect * \brief BTSC detetcion mode
*/
	enum drx_aud_btsc_detect {
		DRX_BTSC_STEREO,
		DRX_BTSC_MONO_AND_SAP};

/*
* \struct struct drx_aud_data * \brief Audio data structure
*/
struct drx_aud_data {
	/* audio storage */
	bool audio_is_active;
	enum drx_aud_standard audio_standard;
	struct drx_cfg_i2s_output i2sdata;
	struct drx_cfg_aud_volume volume;
	enum drx_cfg_aud_auto_sound auto_sound;
	struct drx_cfg_aud_ass_thres ass_thresholds;
	struct drx_cfg_aud_carriers carriers;
	struct drx_cfg_aud_mixer mixer;
	enum drx_cfg_aud_deviation deviation;
	enum drx_cfg_aud_av_sync av_sync;
	struct drx_cfg_aud_prescale prescale;
	enum drx_aud_fm_deemphasis deemph;
	enum drx_aud_btsc_detect btsc_detect;
	/* rds */
	u16 rds_data_counter;
	bool rds_data_present;
};

/*
* \enum enum drx_qam_lock_range * \brief QAM lock range mode
*/
	enum drx_qam_lock_range {
		DRX_QAM_LOCKRANGE_NORMAL,
		DRX_QAM_LOCKRANGE_EXTENDED};

/*============================================================================*/
/*============================================================================*/
/*== Data access structures ==================================================*/
/*============================================================================*/
/*============================================================================*/

/* Address on device */
	typedef u32 dr_xaddr_t, *pdr_xaddr_t;

/* Protocol specific flags */
	typedef u32 dr_xflags_t, *pdr_xflags_t;

/* Write block of data to device */
	typedef int(*drx_write_block_func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						   u32 addr,	/* address of register/memory   */
						   u16 datasize,	/* size of data in bytes        */
						   u8 *data,	/* data to send                 */
						   u32 flags);

/* Read block of data from device */
	typedef int(*drx_read_block_func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						  u32 addr,	/* address of register/memory   */
						  u16 datasize,	/* size of data in bytes        */
						  u8 *data,	/* receive buffer               */
						  u32 flags);

/* Write 8-bits value to device */
	typedef int(*drx_write_reg8func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						  u32 addr,	/* address of register/memory   */
						  u8 data,	/* data to send                 */
						  u32 flags);

/* Read 8-bits value to device */
	typedef int(*drx_read_reg8func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						 u32 addr,	/* address of register/memory   */
						 u8 *data,	/* receive buffer               */
						 u32 flags);

/* Read modify write 8-bits value to device */
	typedef int(*drx_read_modify_write_reg8func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device       */
							    u32 waddr,	/* write address of register   */
							    u32 raddr,	/* read  address of register   */
							    u8 wdata,	/* data to write               */
							    u8 *rdata);	/* data to read                */

/* Write 16-bits value to device */
	typedef int(*drx_write_reg16func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						   u32 addr,	/* address of register/memory   */
						   u16 data,	/* data to send                 */
						   u32 flags);

/* Read 16-bits value to device */
	typedef int(*drx_read_reg16func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						  u32 addr,	/* address of register/memory   */
						  u16 *data,	/* receive buffer               */
						  u32 flags);

/* Read modify write 16-bits value to device */
	typedef int(*drx_read_modify_write_reg16func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device       */
							     u32 waddr,	/* write address of register   */
							     u32 raddr,	/* read  address of register   */
							     u16 wdata,	/* data to write               */
							     u16 *rdata);	/* data to read                */

/* Write 32-bits value to device */
	typedef int(*drx_write_reg32func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						   u32 addr,	/* address of register/memory   */
						   u32 data,	/* data to send                 */
						   u32 flags);

/* Read 32-bits value to device */
	typedef int(*drx_read_reg32func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device        */
						  u32 addr,	/* address of register/memory   */
						  u32 *data,	/* receive buffer               */
						  u32 flags);

/* Read modify write 32-bits value to device */
	typedef int(*drx_read_modify_write_reg32func_t) (struct i2c_device_addr *dev_addr,	/* address of I2C device       */
							     u32 waddr,	/* write address of register   */
							     u32 raddr,	/* read  address of register   */
							     u32 wdata,	/* data to write               */
							     u32 *rdata);	/* data to read                */

/*
* \struct struct drx_access_func * \brief Interface to an access protocol.
*/
struct drx_access_func {
	drx_write_block_func_t write_block_func;
	drx_read_block_func_t read_block_func;
	drx_write_reg8func_t write_reg8func;
	drx_read_reg8func_t read_reg8func;
	drx_read_modify_write_reg8func_t read_modify_write_reg8func;
	drx_write_reg16func_t write_reg16func;
	drx_read_reg16func_t read_reg16func;
	drx_read_modify_write_reg16func_t read_modify_write_reg16func;
	drx_write_reg32func_t write_reg32func;
	drx_read_reg32func_t read_reg32func;
	drx_read_modify_write_reg32func_t read_modify_write_reg32func;
};

/* Register address and data for register dump function */
struct drx_reg_dump {
	u32 address;
	u32 data;
};

/*============================================================================*/
/*============================================================================*/
/*== Demod instance data structures ==========================================*/
/*============================================================================*/
/*============================================================================*/

/*
* \struct struct drx_common_attr * \brief Set of common attributes, shared by all DRX devices.
*/
	struct drx_common_attr {
		/* Microcode (firmware) attributes */
		char *microcode_file;   /*<  microcode filename           */
		bool verify_microcode;
				   /*< Use microcode verify or not.          */
		struct drx_mc_version_rec mcversion;
				   /*< Version record of microcode from file */

		/* Clocks and tuner attributes */
		s32 intermediate_freq;
				     /*< IF,if tuner instance not used. (kHz)*/
		s32 sys_clock_freq;
				     /*< Systemclock frequency.  (kHz)       */
		s32 osc_clock_freq;
				     /*< Oscillator clock frequency.  (kHz)  */
		s16 osc_clock_deviation;
				     /*< Oscillator clock deviation.  (ppm)  */
		bool mirror_freq_spect;
				     /*< Mirror IF frequency spectrum or not.*/

		/* Initial MPEG output attributes */
		struct drx_cfg_mpeg_output mpeg_cfg;
				     /*< MPEG configuration                  */

		bool is_opened;     /*< if true instance is already opened. */

		/* Channel scan */
		struct drx_scan_param *scan_param;
				      /*< scan parameters                    */
		u16 scan_freq_plan_index;
				      /*< next index in freq plan            */
		s32 scan_next_frequency;
				      /*< next freq to scan                  */
		bool scan_ready;     /*< scan ready flag                    */
		u32 scan_max_channels;/*< number of channels in freqplan     */
		u32 scan_channels_scanned;
					/*< number of channels scanned       */
		/* Channel scan - inner loop: demod related */
		drx_scan_func_t scan_function;
				      /*< function to check channel          */
		/* Channel scan - inner loop: SYSObj related */
		void *scan_context;    /*< Context Pointer of SYSObj          */
		/* Channel scan - parameters for default DTV scan function in core driver  */
		u16 scan_demod_lock_timeout;
					 /*< millisecs to wait for lock      */
		enum drx_lock_status scan_desired_lock;
				      /*< lock requirement for channel found */
		/* scan_active can be used by SetChannel to decide how to program the tuner,
		   fast or slow (but stable). Usually fast during scan. */
		bool scan_active;    /*< true when scan routines are active */

		/* Power management */
		enum drx_power_mode current_power_mode;
				      /*< current power management mode      */

		/* Tuner */
		u8 tuner_port_nr;     /*< nr of I2C port to wich tuner is    */
		s32 tuner_min_freq_rf;
				      /*< minimum RF input frequency, in kHz */
		s32 tuner_max_freq_rf;
				      /*< maximum RF input frequency, in kHz */
		bool tuner_rf_agc_pol; /*< if true invert RF AGC polarity     */
		bool tuner_if_agc_pol; /*< if true invert IF AGC polarity     */
		bool tuner_slow_mode; /*< if true invert IF AGC polarity     */

		struct drx_channel current_channel;
				      /*< current channel parameters         */
		enum drx_standard current_standard;
				      /*< current standard selection         */
		enum drx_standard prev_standard;
				      /*< previous standard selection        */
		enum drx_standard di_cache_standard;
				      /*< standard in DI cache if available  */
		bool use_bootloader; /*< use bootloader in open             */
		u32 capabilities;   /*< capabilities flags                 */
		u32 product_id;      /*< product ID inc. metal fix number   */};

/*
* Generic functions for DRX devices.
*/

struct drx_demod_instance;

/*
* \struct struct drx_demod_instance * \brief Top structure of demodulator instance.
*/
struct drx_demod_instance {
				/*< data access protocol functions       */
	struct i2c_device_addr *my_i2c_dev_addr;
				/*< i2c address and device identifier    */
	struct drx_common_attr *my_common_attr;
				/*< common DRX attributes                */
	void *my_ext_attr;    /*< device specific attributes           */
	/* generic demodulator data */

	struct i2c_adapter	*i2c;
	const struct firmware	*firmware;
};

/*-------------------------------------------------------------------------
MACROS
Conversion from enum values to human readable form.
-------------------------------------------------------------------------*/

/* standard */

#define DRX_STR_STANDARD(x) ( \
	(x == DRX_STANDARD_DVBT)  ? "DVB-T"            : \
	(x == DRX_STANDARD_8VSB)  ? "8VSB"             : \
	(x == DRX_STANDARD_NTSC)  ? "NTSC"             : \
	(x == DRX_STANDARD_PAL_SECAM_BG)  ? "PAL/SECAM B/G"    : \
	(x == DRX_STANDARD_PAL_SECAM_DK)  ? "PAL/SECAM D/K"    : \
	(x == DRX_STANDARD_PAL_SECAM_I)  ? "PAL/SECAM I"      : \
	(x == DRX_STANDARD_PAL_SECAM_L)  ? "PAL/SECAM L"      : \
	(x == DRX_STANDARD_PAL_SECAM_LP)  ? "PAL/SECAM LP"     : \
	(x == DRX_STANDARD_ITU_A)  ? "ITU-A"            : \
	(x == DRX_STANDARD_ITU_B)  ? "ITU-B"            : \
	(x == DRX_STANDARD_ITU_C)  ? "ITU-C"            : \
	(x == DRX_STANDARD_ITU_D)  ? "ITU-D"            : \
	(x == DRX_STANDARD_FM)  ? "FM"               : \
	(x == DRX_STANDARD_DTMB)  ? "DTMB"             : \
	(x == DRX_STANDARD_AUTO)  ? "Auto"             : \
	(x == DRX_STANDARD_UNKNOWN)  ? "Unknown"          : \
	"(Invalid)")

/* channel */

#define DRX_STR_BANDWIDTH(x) ( \
	(x == DRX_BANDWIDTH_8MHZ)  ?  "8 MHz"            : \
	(x == DRX_BANDWIDTH_7MHZ)  ?  "7 MHz"            : \
	(x == DRX_BANDWIDTH_6MHZ)  ?  "6 MHz"            : \
	(x == DRX_BANDWIDTH_AUTO)  ?  "Auto"             : \
	(x == DRX_BANDWIDTH_UNKNOWN)  ?  "Unknown"          : \
	"(Invalid)")
#define DRX_STR_FFTMODE(x) ( \
	(x == DRX_FFTMODE_2K)  ?  "2k"               : \
	(x == DRX_FFTMODE_4K)  ?  "4k"               : \
	(x == DRX_FFTMODE_8K)  ?  "8k"               : \
	(x == DRX_FFTMODE_AUTO)  ?  "Auto"             : \
	(x == DRX_FFTMODE_UNKNOWN)  ?  "Unknown"          : \
	"(Invalid)")
#define DRX_STR_GUARD(x) ( \
	(x == DRX_GUARD_1DIV32)  ?  "1/32nd"           : \
	(x == DRX_GUARD_1DIV16)  ?  "1/16th"           : \
	(x == DRX_GUARD_1DIV8)  ?  "1/8th"            : \
	(x == DRX_GUARD_1DIV4)  ?  "1/4th"            : \
	(x == DRX_GUARD_AUTO)  ?  "Auto"             : \
	(x == DRX_GUARD_UNKNOWN)  ?  "Unknown"          : \
	"(Invalid)")
#define DRX_STR_CONSTELLATION(x) ( \
	(x == DRX_CONSTELLATION_BPSK)  ?  "BPSK"            : \
	(x == DRX_CONSTELLATION_QPSK)  ?  "QPSK"            : \
	(x == DRX_CONSTELLATION_PSK8)  ?  "PSK8"            : \
	(x == DRX_CONSTELLATION_QAM16)  ?  "QAM16"           : \
	(x == DRX_CONSTELLATION_QAM32)  ?  "QAM32"           : \
	(x == DRX_CONSTELLATION_QAM64)  ?  "QAM64"           : \
	(x == DRX_CONSTELLATION_QAM128)  ?  "QAM128"          : \
	(x == DRX_CONSTELLATION_QAM256)  ?  "QAM256"          : \
	(x == DRX_CONSTELLATION_QAM512)  ?  "QAM512"          : \
	(x == DRX_CONSTELLATION_QAM1024)  ?  "QAM1024"         : \
	(x == DRX_CONSTELLATION_QPSK_NR)  ?  "QPSK_NR"            : \
	(x == DRX_CONSTELLATION_AUTO)  ?  "Auto"            : \
	(x == DRX_CONSTELLATION_UNKNOWN)  ?  "Unknown"         : \
	"(Invalid)")
#define DRX_STR_CODERATE(x) ( \
	(x == DRX_CODERATE_1DIV2)  ?  "1/2nd"           : \
	(x == DRX_CODERATE_2DIV3)  ?  "2/3rd"           : \
	(x == DRX_CODERATE_3DIV4)  ?  "3/4th"           : \
	(x == DRX_CODERATE_5DIV6)  ?  "5/6th"           : \
	(x == DRX_CODERATE_7DIV8)  ?  "7/8th"           : \
	(x == DRX_CODERATE_AUTO)  ?  "Auto"            : \
	(x == DRX_CODERATE_UNKNOWN)  ?  "Unknown"         : \
	"(Invalid)")
#define DRX_STR_HIERARCHY(x) ( \
	(x == DRX_HIERARCHY_NONE)  ?  "None"            : \
	(x == DRX_HIERARCHY_ALPHA1)  ?  "Alpha=1"         : \
	(x == DRX_HIERARCHY_ALPHA2)  ?  "Alpha=2"         : \
	(x == DRX_HIERARCHY_ALPHA4)  ?  "Alpha=4"         : \
	(x == DRX_HIERARCHY_AUTO)  ?  "Auto"            : \
	(x == DRX_HIERARCHY_UNKNOWN)  ?  "Unknown"         : \
	"(Invalid)")
#define DRX_STR_PRIORITY(x) ( \
	(x == DRX_PRIORITY_LOW)  ?  "Low"             : \
	(x == DRX_PRIORITY_HIGH)  ?  "High"            : \
	(x == DRX_PRIORITY_UNKNOWN)  ?  "Unknown"         : \
	"(Invalid)")
#define DRX_STR_MIRROR(x) ( \
	(x == DRX_MIRROR_NO)  ?  "Normal"          : \
	(x == DRX_MIRROR_YES)  ?  "Mirrored"        : \
	(x == DRX_MIRROR_AUTO)  ?  "Auto"            : \
	(x == DRX_MIRROR_UNKNOWN)  ?  "Unknown"         : \
	"(Invalid)")
#define DRX_STR_CLASSIFICATION(x) ( \
	(x == DRX_CLASSIFICATION_GAUSS)  ?  "Gaussion"        : \
	(x == DRX_CLASSIFICATION_HVY_GAUSS)  ?  "Heavy Gaussion"  : \
	(x == DRX_CLASSIFICATION_COCHANNEL)  ?  "Co-channel"      : \
	(x == DRX_CLASSIFICATION_STATIC)  ?  "Static echo"     : \
	(x == DRX_CLASSIFICATION_MOVING)  ?  "Moving echo"     : \
	(x == DRX_CLASSIFICATION_ZERODB)  ?  "Zero dB echo"    : \
	(x == DRX_CLASSIFICATION_UNKNOWN)  ?  "Unknown"         : \
	(x == DRX_CLASSIFICATION_AUTO)  ?  "Auto"            : \
	"(Invalid)")

#define DRX_STR_INTERLEAVEMODE(x) ( \
	(x == DRX_INTERLEAVEMODE_I128_J1) ? "I128_J1"         : \
	(x == DRX_INTERLEAVEMODE_I128_J1_V2) ? "I128_J1_V2"      : \
	(x == DRX_INTERLEAVEMODE_I128_J2) ? "I128_J2"         : \
	(x == DRX_INTERLEAVEMODE_I64_J2) ? "I64_J2"          : \
	(x == DRX_INTERLEAVEMODE_I128_J3) ? "I128_J3"         : \
	(x == DRX_INTERLEAVEMODE_I32_J4) ? "I32_J4"          : \
	(x == DRX_INTERLEAVEMODE_I128_J4) ? "I128_J4"         : \
	(x == DRX_INTERLEAVEMODE_I16_J8) ? "I16_J8"          : \
	(x == DRX_INTERLEAVEMODE_I128_J5) ? "I128_J5"         : \
	(x == DRX_INTERLEAVEMODE_I8_J16) ? "I8_J16"          : \
	(x == DRX_INTERLEAVEMODE_I128_J6) ? "I128_J6"         : \
	(x == DRX_INTERLEAVEMODE_RESERVED_11) ? "Reserved 11"     : \
	(x == DRX_INTERLEAVEMODE_I128_J7) ? "I128_J7"         : \
	(x == DRX_INTERLEAVEMODE_RESERVED_13) ? "Reserved 13"     : \
	(x == DRX_INTERLEAVEMODE_I128_J8) ? "I128_J8"         : \
	(x == DRX_INTERLEAVEMODE_RESERVED_15) ? "Reserved 15"     : \
	(x == DRX_INTERLEAVEMODE_I12_J17) ? "I12_J17"         : \
	(x == DRX_INTERLEAVEMODE_I5_J4) ? "I5_J4"           : \
	(x == DRX_INTERLEAVEMODE_B52_M240) ? "B52_M240"        : \
	(x == DRX_INTERLEAVEMODE_B52_M720) ? "B52_M720"        : \
	(x == DRX_INTERLEAVEMODE_B52_M48) ? "B52_M48"         : \
	(x == DRX_INTERLEAVEMODE_B52_M0) ? "B52_M0"          : \
	(x == DRX_INTERLEAVEMODE_UNKNOWN) ? "Unknown"         : \
	(x == DRX_INTERLEAVEMODE_AUTO) ? "Auto"            : \
	"(Invalid)")

#define DRX_STR_LDPC(x) ( \
	(x == DRX_LDPC_0_4) ? "0.4"             : \
	(x == DRX_LDPC_0_6) ? "0.6"             : \
	(x == DRX_LDPC_0_8) ? "0.8"             : \
	(x == DRX_LDPC_AUTO) ? "Auto"            : \
	(x == DRX_LDPC_UNKNOWN) ? "Unknown"         : \
	"(Invalid)")

#define DRX_STR_CARRIER(x) ( \
	(x == DRX_CARRIER_MULTI) ? "Multi"           : \
	(x == DRX_CARRIER_SINGLE) ? "Single"          : \
	(x == DRX_CARRIER_AUTO) ? "Auto"            : \
	(x == DRX_CARRIER_UNKNOWN) ? "Unknown"         : \
	"(Invalid)")

#define DRX_STR_FRAMEMODE(x) ( \
	(x == DRX_FRAMEMODE_420)  ? "420"                : \
	(x == DRX_FRAMEMODE_595)  ? "595"                : \
	(x == DRX_FRAMEMODE_945)  ? "945"                : \
	(x == DRX_FRAMEMODE_420_FIXED_PN)  ? "420 with fixed PN"  : \
	(x == DRX_FRAMEMODE_945_FIXED_PN)  ? "945 with fixed PN"  : \
	(x == DRX_FRAMEMODE_AUTO)  ? "Auto"               : \
	(x == DRX_FRAMEMODE_UNKNOWN)  ? "Unknown"            : \
	"(Invalid)")

#define DRX_STR_PILOT(x) ( \
	(x == DRX_PILOT_ON) ?   "On"              : \
	(x == DRX_PILOT_OFF) ?   "Off"             : \
	(x == DRX_PILOT_AUTO) ?   "Auto"            : \
	(x == DRX_PILOT_UNKNOWN) ?   "Unknown"         : \
	"(Invalid)")
/* TPS */

#define DRX_STR_TPS_FRAME(x)  ( \
	(x == DRX_TPS_FRAME1)  ?  "Frame1"          : \
	(x == DRX_TPS_FRAME2)  ?  "Frame2"          : \
	(x == DRX_TPS_FRAME3)  ?  "Frame3"          : \
	(x == DRX_TPS_FRAME4)  ?  "Frame4"          : \
	(x == DRX_TPS_FRAME_UNKNOWN)  ?  "Unknown"         : \
	"(Invalid)")

/* lock status */

#define DRX_STR_LOCKSTATUS(x) ( \
	(x == DRX_NEVER_LOCK)  ?  "Never"           : \
	(x == DRX_NOT_LOCKED)  ?  "No"              : \
	(x == DRX_LOCKED)  ?  "Locked"          : \
	(x == DRX_LOCK_STATE_1)  ?  "Lock state 1"    : \
	(x == DRX_LOCK_STATE_2)  ?  "Lock state 2"    : \
	(x == DRX_LOCK_STATE_3)  ?  "Lock state 3"    : \
	(x == DRX_LOCK_STATE_4)  ?  "Lock state 4"    : \
	(x == DRX_LOCK_STATE_5)  ?  "Lock state 5"    : \
	(x == DRX_LOCK_STATE_6)  ?  "Lock state 6"    : \
	(x == DRX_LOCK_STATE_7)  ?  "Lock state 7"    : \
	(x == DRX_LOCK_STATE_8)  ?  "Lock state 8"    : \
	(x == DRX_LOCK_STATE_9)  ?  "Lock state 9"    : \
	"(Invalid)")

/* version information , modules */
#define DRX_STR_MODULE(x) ( \
	(x == DRX_MODULE_DEVICE)  ?  "Device"                : \
	(x == DRX_MODULE_MICROCODE)  ?  "Microcode"             : \
	(x == DRX_MODULE_DRIVERCORE)  ?  "CoreDriver"            : \
	(x == DRX_MODULE_DEVICEDRIVER)  ?  "DeviceDriver"          : \
	(x == DRX_MODULE_BSP_I2C)  ?  "BSP I2C"               : \
	(x == DRX_MODULE_BSP_TUNER)  ?  "BSP Tuner"             : \
	(x == DRX_MODULE_BSP_HOST)  ?  "BSP Host"              : \
	(x == DRX_MODULE_DAP)  ?  "Data Access Protocol"  : \
	(x == DRX_MODULE_UNKNOWN)  ?  "Unknown"               : \
	"(Invalid)")

#define DRX_STR_POWER_MODE(x) ( \
	(x == DRX_POWER_UP)  ?  "DRX_POWER_UP    "  : \
	(x == DRX_POWER_MODE_1)  ?  "DRX_POWER_MODE_1"  : \
	(x == DRX_POWER_MODE_2)  ?  "DRX_POWER_MODE_2"  : \
	(x == DRX_POWER_MODE_3)  ?  "DRX_POWER_MODE_3"  : \
	(x == DRX_POWER_MODE_4)  ?  "DRX_POWER_MODE_4"  : \
	(x == DRX_POWER_MODE_5)  ?  "DRX_POWER_MODE_5"  : \
	(x == DRX_POWER_MODE_6)  ?  "DRX_POWER_MODE_6"  : \
	(x == DRX_POWER_MODE_7)  ?  "DRX_POWER_MODE_7"  : \
	(x == DRX_POWER_MODE_8)  ?  "DRX_POWER_MODE_8"  : \
	(x == DRX_POWER_MODE_9)  ?  "DRX_POWER_MODE_9"  : \
	(x == DRX_POWER_MODE_10)  ?  "DRX_POWER_MODE_10" : \
	(x == DRX_POWER_MODE_11)  ?  "DRX_POWER_MODE_11" : \
	(x == DRX_POWER_MODE_12)  ?  "DRX_POWER_MODE_12" : \
	(x == DRX_POWER_MODE_13)  ?  "DRX_POWER_MODE_13" : \
	(x == DRX_POWER_MODE_14)  ?  "DRX_POWER_MODE_14" : \
	(x == DRX_POWER_MODE_15)  ?  "DRX_POWER_MODE_15" : \
	(x == DRX_POWER_MODE_16)  ?  "DRX_POWER_MODE_16" : \
	(x == DRX_POWER_DOWN)  ?  "DRX_POWER_DOWN  " : \
	"(Invalid)")

#define DRX_STR_OOB_STANDARD(x) ( \
	(x == DRX_OOB_MODE_A)  ?  "ANSI 55-1  " : \
	(x == DRX_OOB_MODE_B_GRADE_A)  ?  "ANSI 55-2 A" : \
	(x == DRX_OOB_MODE_B_GRADE_B)  ?  "ANSI 55-2 B" : \
	"(Invalid)")

#define DRX_STR_AUD_STANDARD(x) ( \
	(x == DRX_AUD_STANDARD_BTSC)  ? "BTSC"                     : \
	(x == DRX_AUD_STANDARD_A2)  ? "A2"                       : \
	(x == DRX_AUD_STANDARD_EIAJ)  ? "EIAJ"                     : \
	(x == DRX_AUD_STANDARD_FM_STEREO)  ? "FM Stereo"                : \
	(x == DRX_AUD_STANDARD_AUTO)  ? "Auto"                     : \
	(x == DRX_AUD_STANDARD_M_MONO)  ? "M-Standard Mono"          : \
	(x == DRX_AUD_STANDARD_D_K_MONO)  ? "D/K Mono FM"              : \
	(x == DRX_AUD_STANDARD_BG_FM)  ? "B/G-Dual Carrier FM (A2)" : \
	(x == DRX_AUD_STANDARD_D_K1)  ? "D/K1-Dual Carrier FM"     : \
	(x == DRX_AUD_STANDARD_D_K2)  ? "D/K2-Dual Carrier FM"     : \
	(x == DRX_AUD_STANDARD_D_K3)  ? "D/K3-Dual Carrier FM"     : \
	(x == DRX_AUD_STANDARD_BG_NICAM_FM)  ? "B/G-NICAM-FM"             : \
	(x == DRX_AUD_STANDARD_L_NICAM_AM)  ? "L-NICAM-AM"               : \
	(x == DRX_AUD_STANDARD_I_NICAM_FM)  ? "I-NICAM-FM"               : \
	(x == DRX_AUD_STANDARD_D_K_NICAM_FM)  ? "D/K-NICAM-FM"             : \
	(x == DRX_AUD_STANDARD_UNKNOWN)  ? "Unknown"                  : \
	"(Invalid)")
#define DRX_STR_AUD_STEREO(x) ( \
	(x == true)  ? "Stereo"           : \
	(x == false)  ? "Mono"             : \
	"(Invalid)")

#define DRX_STR_AUD_SAP(x) ( \
	(x == true)  ? "Present"          : \
	(x == false)  ? "Not present"      : \
	"(Invalid)")

#define DRX_STR_AUD_CARRIER(x) ( \
	(x == true)  ? "Present"          : \
	(x == false)  ? "Not present"      : \
	"(Invalid)")

#define DRX_STR_AUD_RDS(x) ( \
	(x == true)  ? "Available"        : \
	(x == false)  ? "Not Available"    : \
	"(Invalid)")

#define DRX_STR_AUD_NICAM_STATUS(x) ( \
	(x == DRX_AUD_NICAM_DETECTED)  ? "Detected"         : \
	(x == DRX_AUD_NICAM_NOT_DETECTED)  ? "Not detected"     : \
	(x == DRX_AUD_NICAM_BAD)  ? "Bad"              : \
	"(Invalid)")

#define DRX_STR_RDS_VALID(x) ( \
	(x == true)  ? "Valid"            : \
	(x == false)  ? "Not Valid"        : \
	"(Invalid)")

/*-------------------------------------------------------------------------
Access macros
-------------------------------------------------------------------------*/

/*
* \brief Create a compilable reference to the microcode attribute
* \param d pointer to demod instance
*
* Used as main reference to an attribute field.
* Used by both macro implementation and function implementation.
* These macros are defined to avoid duplication of code in macro and function
* definitions that handle access of demod common or extended attributes.
*
*/

#define DRX_ATTR_MCRECORD(d)        ((d)->my_common_attr->mcversion)
#define DRX_ATTR_MIRRORFREQSPECT(d) ((d)->my_common_attr->mirror_freq_spect)
#define DRX_ATTR_CURRENTPOWERMODE(d)((d)->my_common_attr->current_power_mode)
#define DRX_ATTR_ISOPENED(d)        ((d)->my_common_attr->is_opened)
#define DRX_ATTR_USEBOOTLOADER(d)   ((d)->my_common_attr->use_bootloader)
#define DRX_ATTR_CURRENTSTANDARD(d) ((d)->my_common_attr->current_standard)
#define DRX_ATTR_PREVSTANDARD(d)    ((d)->my_common_attr->prev_standard)
#define DRX_ATTR_CACHESTANDARD(d)   ((d)->my_common_attr->di_cache_standard)
#define DRX_ATTR_CURRENTCHANNEL(d)  ((d)->my_common_attr->current_channel)
#define DRX_ATTR_MICROCODE(d)       ((d)->my_common_attr->microcode)
#define DRX_ATTR_VERIFYMICROCODE(d) ((d)->my_common_attr->verify_microcode)
#define DRX_ATTR_CAPABILITIES(d)    ((d)->my_common_attr->capabilities)
#define DRX_ATTR_PRODUCTID(d)       ((d)->my_common_attr->product_id)
#define DRX_ATTR_INTERMEDIATEFREQ(d) ((d)->my_common_attr->intermediate_freq)
#define DRX_ATTR_SYSCLOCKFREQ(d)     ((d)->my_common_attr->sys_clock_freq)
#define DRX_ATTR_TUNERRFAGCPOL(d)   ((d)->my_common_attr->tuner_rf_agc_pol)
#define DRX_ATTR_TUNERIFAGCPOL(d)    ((d)->my_common_attr->tuner_if_agc_pol)
#define DRX_ATTR_TUNERSLOWMODE(d)    ((d)->my_common_attr->tuner_slow_mode)
#define DRX_ATTR_TUNERSPORTNR(d)     ((d)->my_common_attr->tuner_port_nr)
#define DRX_ATTR_I2CADDR(d)         ((d)->my_i2c_dev_addr->i2c_addr)
#define DRX_ATTR_I2CDEVID(d)        ((d)->my_i2c_dev_addr->i2c_dev_id)
#define DRX_ISMCVERTYPE(x) ((x) == AUX_VER_RECORD)

/*************************/

/* Macros with device-specific handling are converted to CFG functions */

#define DRX_ACCESSMACRO_SET(demod, value, cfg_name, data_type)             \
	do {                                                               \
		struct drx_cfg config;                                     \
		data_type cfg_data;                                        \
		config.cfg_type = cfg_name;                                \
		config.cfg_data = &cfg_data;                               \
		cfg_data = value;                                          \
		drx_ctrl(demod, DRX_CTRL_SET_CFG, &config);                \
	} while (0)

#define DRX_ACCESSMACRO_GET(demod, value, cfg_name, data_type, error_value) \
	do {                                                                \
		int cfg_status;                                             \
		struct drx_cfg config;                                      \
		data_type    cfg_data;                                      \
		config.cfg_type = cfg_name;                                 \
		config.cfg_data = &cfg_data;                                \
		cfg_status = drx_ctrl(demod, DRX_CTRL_GET_CFG, &config);    \
		if (cfg_status == 0) {                                      \
			value = cfg_data;                                   \
		} else {                                                    \
			value = (data_type)error_value;                     \
		}                                                           \
	} while (0)

/* Configuration functions for usage by Access (XS) Macros */

#ifndef DRX_XS_CFG_BASE
#define DRX_XS_CFG_BASE (500)
#endif

#define DRX_XS_CFG_PRESET          (DRX_XS_CFG_BASE + 0)
#define DRX_XS_CFG_AUD_BTSC_DETECT (DRX_XS_CFG_BASE + 1)
#define DRX_XS_CFG_QAM_LOCKRANGE   (DRX_XS_CFG_BASE + 2)

/* Access Macros with device-specific handling */

#define DRX_SET_PRESET(d, x) \
	DRX_ACCESSMACRO_SET((d), (x), DRX_XS_CFG_PRESET, char*)
#define DRX_GET_PRESET(d, x) \
	DRX_ACCESSMACRO_GET((d), (x), DRX_XS_CFG_PRESET, char*, "ERROR")

#define DRX_SET_AUD_BTSC_DETECT(d, x) DRX_ACCESSMACRO_SET((d), (x), \
	 DRX_XS_CFG_AUD_BTSC_DETECT, enum drx_aud_btsc_detect)
#define DRX_GET_AUD_BTSC_DETECT(d, x) DRX_ACCESSMACRO_GET((d), (x), \
	 DRX_XS_CFG_AUD_BTSC_DETECT, enum drx_aud_btsc_detect, DRX_UNKNOWN)

#define DRX_SET_QAM_LOCKRANGE(d, x) DRX_ACCESSMACRO_SET((d), (x), \
	 DRX_XS_CFG_QAM_LOCKRANGE, enum drx_qam_lock_range)
#define DRX_GET_QAM_LOCKRANGE(d, x) DRX_ACCESSMACRO_GET((d), (x), \
	 DRX_XS_CFG_QAM_LOCKRANGE, enum drx_qam_lock_range, DRX_UNKNOWN)

/*
* \brief Macro to check if std is an ATV standard
* \retval true std is an ATV standard
* \retval false std is an ATV standard
*/
#define DRX_ISATVSTD(std) (((std) == DRX_STANDARD_PAL_SECAM_BG) || \
			      ((std) == DRX_STANDARD_PAL_SECAM_DK) || \
			      ((std) == DRX_STANDARD_PAL_SECAM_I) || \
			      ((std) == DRX_STANDARD_PAL_SECAM_L) || \
			      ((std) == DRX_STANDARD_PAL_SECAM_LP) || \
			      ((std) == DRX_STANDARD_NTSC) || \
			      ((std) == DRX_STANDARD_FM))

/*
* \brief Macro to check if std is an QAM standard
* \retval true std is an QAM standards
* \retval false std is an QAM standards
*/
#define DRX_ISQAMSTD(std) (((std) == DRX_STANDARD_ITU_A) || \
			      ((std) == DRX_STANDARD_ITU_B) || \
			      ((std) == DRX_STANDARD_ITU_C) || \
			      ((std) == DRX_STANDARD_ITU_D))

/*
* \brief Macro to check if std is VSB standard
* \retval true std is VSB standard
* \retval false std is not VSB standard
*/
#define DRX_ISVSBSTD(std) ((std) == DRX_STANDARD_8VSB)

/*
* \brief Macro to check if std is DVBT standard
* \retval true std is DVBT standard
* \retval false std is not DVBT standard
*/
#define DRX_ISDVBTSTD(std) ((std) == DRX_STANDARD_DVBT)

/*-------------------------------------------------------------------------
THE END
-------------------------------------------------------------------------*/
#endif				/* __DRXDRIVER_H__ */
