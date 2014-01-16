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
* \file $Id: bsp_tuner.h,v 1.5 2009/10/19 22:15:13 dingtao Exp $
*
* \brief Tuner dependable type definitions, macro's and functions
*
*/

#ifndef __DRXBSP_TUNER_H__
#define __DRXBSP_TUNER_H__
/*------------------------------------------------------------------------------
INCLUDES
------------------------------------------------------------------------------*/
#include "bsp_types.h"
#include "bsp_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

/*------------------------------------------------------------------------------
DEFINES
------------------------------------------------------------------------------*/

	/* Sub-mode bits should be adjacent and incremental */
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

/*------------------------------------------------------------------------------
TYPEDEFS
------------------------------------------------------------------------------*/

	typedef u32 tuner_mode_t;
	typedef u32 *ptuner_mode_t;

	typedef char *tuner_sub_mode_t;	/* description of submode */
	typedef tuner_sub_mode_t *ptuner_sub_mode_t;

	typedef enum {

		TUNER_LOCKED,
		TUNER_NOT_LOCKED
	} tuner_lock_status_t, *ptuner_lock_status_t;

	typedef struct {

		char *name;	/* Tuner brand & type name */
		s32 min_freq_rf;	/* Lowest  RF input frequency, in kHz */
		s32 max_freq_rf;	/* Highest RF input frequency, in kHz */

		u8 sub_mode;	/* Index to sub-mode in use */
		ptuner_sub_mode_t sub_modeDescriptions;	/* Pointer to description of sub-modes */
		u8 sub_modes;	/* Number of available sub-modes      */

		/* The following fields will be either 0, NULL or false and do not need
		   initialisation */
		void *self_check;	/* gives proof of initialization  */
		bool programmed;	/* only valid if self_check is OK  */
		s32 r_ffrequency;	/* only valid if programmed       */
		s32 i_ffrequency;	/* only valid if programmed       */

		void *myUser_data;	/* pointer to associated demod instance */
		u16 my_capabilities;	/* value for storing application flags  */

	} tuner_common_attr_t, *ptuner_common_attr_t;

/*
* Generic functions for DRX devices.
*/
	typedef struct tuner_instance_s *p_tuner_instance_t;

	typedef drx_status_t(*tuner_open_func_t) (p_tuner_instance_t tuner);
	typedef drx_status_t(*tuner_close_func_t) (p_tuner_instance_t tuner);

	typedef drx_status_t(*tuner_set_frequency_func_t) (p_tuner_instance_t tuner,
						       tuner_mode_t mode,
						       s32
						       frequency);

	typedef drx_status_t(*tuner_get_frequency_func_t) (p_tuner_instance_t tuner,
						       tuner_mode_t mode,
						       s32 *
						       r_ffrequency,
						       s32 *
						       i_ffrequency);

	typedef drx_status_t(*tuner_lock_status_func_t) (p_tuner_instance_t tuner,
						     ptuner_lock_status_t
						     lock_stat);

	typedef drx_status_t(*tune_ri2c_write_read_func_t) (p_tuner_instance_t tuner,
						       struct i2c_device_addr *
						       w_dev_addr, u16 w_count,
						       u8 *wData,
						       struct i2c_device_addr *
						       r_dev_addr, u16 r_count,
						       u8 *r_data);

	typedef struct {
		tuner_open_func_t open_func;
		tuner_close_func_t close_func;
		tuner_set_frequency_func_t set_frequency_func;
		tuner_get_frequency_func_t get_frequency_func;
		tuner_lock_status_func_t lock_statusFunc;
		tune_ri2c_write_read_func_t i2c_write_read_func;

	} tuner_func_t, *ptuner_func_t;

	typedef struct tuner_instance_s {

		struct i2c_device_addr my_i2c_dev_addr;
		ptuner_common_attr_t my_common_attr;
		void *my_ext_attr;
		ptuner_func_t my_funct;

	} tuner_instance_t;

/*------------------------------------------------------------------------------
ENUM
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
STRUCTS
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
Exported FUNCTIONS
------------------------------------------------------------------------------*/

	drx_status_t drxbsp_tuner_open(p_tuner_instance_t tuner);

	drx_status_t drxbsp_tuner_close(p_tuner_instance_t tuner);

	drx_status_t drxbsp_tuner_set_frequency(p_tuner_instance_t tuner,
					      tuner_mode_t mode,
					      s32 frequency);

	drx_status_t drxbsp_tuner_get_frequency(p_tuner_instance_t tuner,
					      tuner_mode_t mode,
					      s32 *r_ffrequency,
					      s32 *i_ffrequency);

	drx_status_t drxbsp_tuner_lock_status(p_tuner_instance_t tuner,
					    ptuner_lock_status_t lock_stat);

	drx_status_t drxbsp_tuner_default_i2c_write_read(p_tuner_instance_t tuner,
						     struct i2c_device_addr *w_dev_addr,
						     u16 w_count,
						     u8 *wData,
						     struct i2c_device_addr *r_dev_addr,
						     u16 r_count, u8 *r_data);

/*------------------------------------------------------------------------------
THE END
------------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif				/* __DRXBSP_TUNER_H__ */
/* End of file */
