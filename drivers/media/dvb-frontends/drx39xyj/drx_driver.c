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
* \file $Id: drx_driver.c,v 1.40 2010/01/12 01:24:56 lfeng Exp $
*
* \brief Generic DRX functionality, DRX driver core.
*
*/

/*------------------------------------------------------------------------------
INCLUDE FILES
------------------------------------------------------------------------------*/
#include "drx_driver.h"

#define VERSION_FIXED 0
#if     VERSION_FIXED
#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define VERSION_PATCH 0
#else
#include "drx_driver_version.h"
#endif

/*------------------------------------------------------------------------------
DEFINES
------------------------------------------------------------------------------*/

/*============================================================================*/
/*=== MICROCODE RELATED DEFINES ==============================================*/
/*============================================================================*/

/** \brief Magic word for checking correct Endianess of microcode data. */
#ifndef DRX_UCODE_MAGIC_WORD
#define DRX_UCODE_MAGIC_WORD         ((((u16)'H')<<8)+((u16)'L'))
#endif

/** \brief CRC flag in ucode header, flags field. */
#ifndef DRX_UCODE_CRC_FLAG
#define DRX_UCODE_CRC_FLAG           (0x0001)
#endif

/** \brief Compression flag in ucode header, flags field. */
#ifndef DRX_UCODE_COMPRESSION_FLAG
#define DRX_UCODE_COMPRESSION_FLAG   (0x0002)
#endif

/** \brief Maximum size of buffer used to verify the microcode.
   Must be an even number. */
#ifndef DRX_UCODE_MAX_BUF_SIZE
#define DRX_UCODE_MAX_BUF_SIZE       (DRXDAP_MAX_RCHUNKSIZE)
#endif
#if DRX_UCODE_MAX_BUF_SIZE & 1
#error DRX_UCODE_MAX_BUF_SIZE must be an even number
#endif

/*============================================================================*/
/*=== CHANNEL SCAN RELATED DEFINES ===========================================*/
/*============================================================================*/

/**
* \brief Maximum progress indication.
*
* Progress indication will run from 0 upto DRX_SCAN_MAX_PROGRESS during scan.
*
*/
#ifndef DRX_SCAN_MAX_PROGRESS
#define DRX_SCAN_MAX_PROGRESS 1000
#endif

/*============================================================================*/
/*=== MACROS =================================================================*/
/*============================================================================*/

#define DRX_ISPOWERDOWNMODE(mode) (( mode == DRX_POWER_MODE_9) || \
				       (mode == DRX_POWER_MODE_10) || \
				       (mode == DRX_POWER_MODE_11) || \
				       (mode == DRX_POWER_MODE_12) || \
				       (mode == DRX_POWER_MODE_13) || \
				       (mode == DRX_POWER_MODE_14) || \
				       (mode == DRX_POWER_MODE_15) || \
				       (mode == DRX_POWER_MODE_16) || \
				       (mode == DRX_POWER_DOWN))

/*------------------------------------------------------------------------------
GLOBAL VARIABLES
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
STRUCTURES
------------------------------------------------------------------------------*/
/** \brief  Structure of the microcode block headers */
typedef struct {
	u32 addr;
		  /**<  Destination address of the data in this block */
	u16 size;
		  /**<  Size of the block data following this header counted in
			16 bits words */
	u16 flags;
		  /**<  Flags for this data block:
			- bit[0]= CRC on/off
			- bit[1]= compression on/off
			- bit[15..2]=reserved */
	u16 CRC;/**<  CRC value of the data block, only valid if CRC flag is
			set. */
} drxu_code_block_hdr_t, *pdrxu_code_block_hdr_t;

/*------------------------------------------------------------------------------
FUNCTIONS
------------------------------------------------------------------------------*/

/*============================================================================*/
/*============================================================================*/
/*== Channel Scan Functions ==================================================*/
/*============================================================================*/
/*============================================================================*/

#ifndef DRX_EXCLUDE_SCAN

/* Prototype of default scanning function */
static int
scan_function_default(void *scan_context,
		      drx_scan_command_t scan_command,
		    pdrx_channel_t scan_channel, bool *get_next_channel);

/**
* \brief Get pointer to scanning function.
* \param demod:    Pointer to demodulator instance.
* \return drx_scan_func_t.
*/
static drx_scan_func_t get_scan_function(pdrx_demod_instance_t demod)
{
	pdrx_common_attr_t common_attr = (pdrx_common_attr_t) (NULL);
	drx_scan_func_t scan_func = (drx_scan_func_t) (NULL);

	/* get scan function from common attributes */
	common_attr = (pdrx_common_attr_t) demod->my_common_attr;
	scan_func = common_attr->scan_function;

	if (scan_func != NULL) {
		/* return device-specific scan function if it's not NULL */
		return scan_func;
	}
	/* otherwise return default scan function in core driver */
	return &scan_function_default;
}

/**
* \brief Get Context pointer.
* \param demod:    Pointer to demodulator instance.
* \param scan_context: Context Pointer.
* \return drx_scan_func_t.
*/
static void *get_scan_context(pdrx_demod_instance_t demod, void *scan_context)
{
	pdrx_common_attr_t common_attr = (pdrx_common_attr_t) (NULL);

	/* get scan function from common attributes */
	common_attr = (pdrx_common_attr_t) demod->my_common_attr;
	scan_context = common_attr->scan_context;

	if (scan_context == NULL) {
		scan_context = (void *)demod;
	}

	return scan_context;
}

/**
* \brief Wait for lock while scanning.
* \param demod:    Pointer to demodulator instance.
* \param lock_stat: Pointer to bool indicating if end result is lock or not.
* \return int.
* \retval DRX_STS_OK:    Success
* \retval DRX_STS_ERROR: I2C failure or bsp function failure.
*
* Wait until timeout, desired lock or NEVER_LOCK.
* Assume:
* - lock function returns : at least DRX_NOT_LOCKED and a lock state
*   higher than DRX_NOT_LOCKED.
* - BSP has a clock function to retrieve a millisecond ticker value.
* - BSP has a sleep function to enable sleep of n millisecond.
*
* In case DRX_NEVER_LOCK is returned the poll-wait will be aborted.
*
*/
static int scan_wait_for_lock(pdrx_demod_instance_t demod, bool *is_locked)
{
	bool done_waiting = false;
	drx_lock_status_t lock_state = DRX_NOT_LOCKED;
	drx_lock_status_t desired_lock_state = DRX_NOT_LOCKED;
	u32 timeout_value = 0;
	u32 start_time_lock_stage = 0;
	u32 current_time = 0;
	u32 timer_value = 0;

	*is_locked = false;
	timeout_value = (u32) demod->my_common_attr->scan_demod_lock_timeout;
	desired_lock_state = demod->my_common_attr->scan_desired_lock;
	start_time_lock_stage = drxbsp_hst_clock();

	/* Start polling loop, checking for lock & timeout */
	while (done_waiting == false) {

		if (drx_ctrl(demod, DRX_CTRL_LOCK_STATUS, &lock_state) !=
		    DRX_STS_OK) {
			return DRX_STS_ERROR;
		}
		current_time = drxbsp_hst_clock();

		timer_value = current_time - start_time_lock_stage;
		if (lock_state >= desired_lock_state) {
			*is_locked = true;
			done_waiting = true;
		} /* if ( lock_state >= desired_lock_state ) .. */
		else if (lock_state == DRX_NEVER_LOCK) {
			done_waiting = true;
		} /* if ( lock_state == DRX_NEVER_LOCK ) .. */
		else if (timer_value > timeout_value) {
			/* lock_state == DRX_NOT_LOCKED  and timeout */
			done_waiting = true;
		} else {
			if (drxbsp_hst_sleep(10) != DRX_STS_OK) {
				return DRX_STS_ERROR;
			}
		}		/* if ( timer_value > timeout_value ) .. */

	}			/* while */

	return DRX_STS_OK;
}

/*============================================================================*/

/**
* \brief Determine next frequency to scan.
* \param demod: Pointer to demodulator instance.
* \param skip : Minimum frequency step to take.
* \return int.
* \retval DRX_STS_OK:          Succes.
* \retval DRX_STS_INVALID_ARG: Invalid frequency plan.
*
* Helper function for ctrl_scan_next() function.
* Compute next frequency & index in frequency plan.
* Check if scan is ready.
*
*/
static int
scan_prepare_next_scan(pdrx_demod_instance_t demod, s32 skip)
{
	pdrx_common_attr_t common_attr = (pdrx_common_attr_t) (NULL);
	u16 table_index = 0;
	u16 frequency_plan_size = 0;
	p_drx_frequency_plan_t frequency_plan = (p_drx_frequency_plan_t) (NULL);
	s32 next_frequency = 0;
	s32 tuner_min_frequency = 0;
	s32 tuner_max_frequency = 0;

	common_attr = (pdrx_common_attr_t) demod->my_common_attr;
	table_index = common_attr->scan_freq_plan_index;
	frequency_plan = common_attr->scan_param->frequency_plan;
	next_frequency = common_attr->scan_next_frequency;
	tuner_min_frequency = common_attr->tuner_min_freq_rf;
	tuner_max_frequency = common_attr->tuner_max_freq_rf;

	do {
		/* Search next frequency to scan */

		/* always take at least one step */
		(common_attr->scan_channelsScanned)++;
		next_frequency += frequency_plan[table_index].step;
		skip -= frequency_plan[table_index].step;

		/* and then as many steps necessary to exceed 'skip'
		   without exceeding end of the band */
		while ((skip > 0) &&
		       (next_frequency <= frequency_plan[table_index].last)) {
			(common_attr->scan_channelsScanned)++;
			next_frequency += frequency_plan[table_index].step;
			skip -= frequency_plan[table_index].step;
		}
		/* reset skip, in case we move to the next band later */
		skip = 0;

		if (next_frequency > frequency_plan[table_index].last) {
			/* reached end of this band */
			table_index++;
			frequency_plan_size =
			    common_attr->scan_param->frequency_plan_size;
			if (table_index >= frequency_plan_size) {
				/* reached end of frequency plan */
				common_attr->scan_ready = true;
			} else {
				next_frequency = frequency_plan[table_index].first;
			}
		}
		if (next_frequency > (tuner_max_frequency)) {
			/* reached end of tuner range */
			common_attr->scan_ready = true;
		}
	} while ((next_frequency < tuner_min_frequency) &&
		 (common_attr->scan_ready == false));

	/* Store new values */
	common_attr->scan_freq_plan_index = table_index;
	common_attr->scan_next_frequency = next_frequency;

	return DRX_STS_OK;
}

/*============================================================================*/

/**
* \brief Default DTV scanning function.
*
* \param demod:          Pointer to demodulator instance.
* \param scan_command:    Scanning command: INIT, NEXT or STOP.
* \param scan_channel:    Channel to check: frequency and bandwidth, others AUTO
* \param get_next_channel: Return true if next frequency is desired at next call
*
* \return int.
* \retval DRX_STS_OK:      Channel found, DRX_CTRL_GET_CHANNEL can be used
*                             to retrieve channel parameters.
* \retval DRX_STS_BUSY:    Channel not found (yet).
* \retval DRX_STS_ERROR:   Something went wrong.
*
* scan_channel and get_next_channel will be NULL for INIT and STOP.
*/
static int
scan_function_default(void *scan_context,
		      drx_scan_command_t scan_command,
		    pdrx_channel_t scan_channel, bool *get_next_channel)
{
	pdrx_demod_instance_t demod = NULL;
	int status = DRX_STS_ERROR;
	bool is_locked = false;

	demod = (pdrx_demod_instance_t) scan_context;

	if (scan_command != DRX_SCAN_COMMAND_NEXT) {
		/* just return OK if not doing "scan next" */
		return DRX_STS_OK;
	}

	*get_next_channel = false;

	status = drx_ctrl(demod, DRX_CTRL_SET_CHANNEL, scan_channel);
	if (status != DRX_STS_OK) {
		return (status);
	}

	status = scan_wait_for_lock(demod, &is_locked);
	if (status != DRX_STS_OK) {
		return status;
	}

	/* done with this channel, move to next one */
	*get_next_channel = true;

	if (is_locked == false) {
		/* no channel found */
		return DRX_STS_BUSY;
	}
	/* channel found */
	return DRX_STS_OK;
}

/*============================================================================*/

/**
* \brief Initialize for channel scan.
* \param demod:     Pointer to demodulator instance.
* \param scan_param: Pointer to scan parameters.
* \return int.
* \retval DRX_STS_OK:          Initialized for scan.
* \retval DRX_STS_ERROR:       No overlap between frequency plan and tuner
*                              range.
* \retval DRX_STS_INVALID_ARG: Wrong parameters.
*
* This function should be called before starting a complete channel scan.
* It will prepare everything for a complete channel scan.
* After calling this function the DRX_CTRL_SCAN_NEXT control function can be
* used to perform the actual scanning. Scanning will start at the first
* center frequency of the frequency plan that is within the tuner range.
*
*/
static int
ctrl_scan_init(pdrx_demod_instance_t demod, p_drx_scan_param_t scan_param)
{
	int status = DRX_STS_ERROR;
	pdrx_common_attr_t common_attr = (pdrx_common_attr_t) (NULL);
	s32 max_tuner_freq = 0;
	s32 min_tuner_freq = 0;
	u16 nr_channels_in_plan = 0;
	u16 i = 0;
	void *scan_context = NULL;

	common_attr = (pdrx_common_attr_t) demod->my_common_attr;
	common_attr->scan_active = true;

	/* invalidate a previous SCAN_INIT */
	common_attr->scan_param = (p_drx_scan_param_t) (NULL);
	common_attr->scan_next_frequency = 0;

	/* Check parameters */
	if (((demod->my_tuner == NULL) &&
	     (scan_param->num_tries != 1)) ||
	    (scan_param == NULL) ||
	    (scan_param->num_tries == 0) ||
	    (scan_param->frequency_plan == NULL) ||
	    (scan_param->frequency_plan_size == 0)
	    ) {
		common_attr->scan_active = false;
		return DRX_STS_INVALID_ARG;
	}

	/* Check frequency plan contents */
	max_tuner_freq = common_attr->tuner_max_freq_rf;
	min_tuner_freq = common_attr->tuner_min_freq_rf;
	for (i = 0; i < (scan_param->frequency_plan_size); i++) {
		s32 width = 0;
		s32 step = scan_param->frequency_plan[i].step;
		s32 first_freq = scan_param->frequency_plan[i].first;
		s32 last_freq = scan_param->frequency_plan[i].last;
		s32 min_freq = 0;
		s32 max_freq = 0;

		if (step <= 0) {
			/* Step must be positive and non-zero */
			common_attr->scan_active = false;
			return DRX_STS_INVALID_ARG;
		}

		if (first_freq > last_freq) {
			/* First center frequency is higher than last center frequency */
			common_attr->scan_active = false;
			return DRX_STS_INVALID_ARG;
		}

		width = last_freq - first_freq;

		if ((width % step) != 0) {
			/* Difference between last and first center frequency is not
			   an integer number of steps */
			common_attr->scan_active = false;
			return DRX_STS_INVALID_ARG;
		}

		/* Check if frequency plan entry intersects with tuner range */
		if (last_freq >= min_tuner_freq) {
			if (first_freq <= max_tuner_freq) {
				if (first_freq >= min_tuner_freq) {
					min_freq = first_freq;
				} else {
					s32 n = 0;

					n = (min_tuner_freq - first_freq) / step;
					if (((min_tuner_freq -
					      first_freq) % step) != 0) {
						n++;
					}
					min_freq = first_freq + n * step;
				}

				if (last_freq <= max_tuner_freq) {
					max_freq = last_freq;
				} else {
					s32 n = 0;

					n = (last_freq - max_tuner_freq) / step;
					if (((last_freq -
					      max_tuner_freq) % step) != 0) {
						n++;
					}
					max_freq = last_freq - n * step;
				}
			}
		}

		/* Keep track of total number of channels within tuner range
		   in this frequency plan. */
		if ((min_freq != 0) && (max_freq != 0)) {
			nr_channels_in_plan +=
			    (u16) (((max_freq - min_freq) / step) + 1);

			/* Determine first frequency (within tuner range) to scan */
			if (common_attr->scan_next_frequency == 0) {
				common_attr->scan_next_frequency = min_freq;
				common_attr->scan_freq_plan_index = i;
			}
		}

	}			/* for ( ... ) */

	if (nr_channels_in_plan == 0) {
		/* Tuner range and frequency plan ranges do not overlap */
		common_attr->scan_active = false;
		return DRX_STS_ERROR;
	}

	/* Store parameters */
	common_attr->scan_ready = false;
	common_attr->scan_max_channels = nr_channels_in_plan;
	common_attr->scan_channelsScanned = 0;
	common_attr->scan_param = scan_param;	/* SCAN_NEXT is now allowed */

	scan_context = get_scan_context(demod, scan_context);

	status = (*(get_scan_function(demod)))
	    (scan_context, DRX_SCAN_COMMAND_INIT, NULL, NULL);

	common_attr->scan_active = false;

	return DRX_STS_OK;
}

/*============================================================================*/

/**
* \brief Stop scanning.
* \param demod:         Pointer to demodulator instance.
* \return int.
* \retval DRX_STS_OK:          Scan stopped.
* \retval DRX_STS_ERROR:       Something went wrong.
* \retval DRX_STS_INVALID_ARG: Wrong parameters.
*/
static int ctrl_scan_stop(pdrx_demod_instance_t demod)
{
	int status = DRX_STS_ERROR;
	pdrx_common_attr_t common_attr = (pdrx_common_attr_t) (NULL);
	void *scan_context = NULL;

	common_attr = (pdrx_common_attr_t) demod->my_common_attr;
	common_attr->scan_active = true;

	if ((common_attr->scan_param == NULL) ||
	    (common_attr->scan_max_channels == 0)) {
		/* Scan was not running, just return OK */
		common_attr->scan_active = false;
		return DRX_STS_OK;
	}

	/* Call default or device-specific scanning stop function */
	scan_context = get_scan_context(demod, scan_context);

	status = (*(get_scan_function(demod)))
	    (scan_context, DRX_SCAN_COMMAND_STOP, NULL, NULL);

	/* All done, invalidate scan-init */
	common_attr->scan_param = NULL;
	common_attr->scan_max_channels = 0;
	common_attr->scan_active = false;

	return status;
}

/*============================================================================*/

/**
* \brief Scan for next channel.
* \param demod:         Pointer to demodulator instance.
* \param scan_progress:  Pointer to scan progress.
* \return int.
* \retval DRX_STS_OK:          Channel found, DRX_CTRL_GET_CHANNEL can be used
*                              to retrieve channel parameters.
* \retval DRX_STS_BUSY:        Tried part of the channels, as specified in
*                              num_tries field of scan parameters. At least one
*                              more call to DRX_CTRL_SCAN_NEXT is needed to
*                              complete scanning.
* \retval DRX_STS_READY:       Reached end of scan range.
* \retval DRX_STS_ERROR:       Something went wrong.
* \retval DRX_STS_INVALID_ARG: Wrong parameters. The scan_progress may be NULL.
*
* Progress indication will run from 0 upto DRX_SCAN_MAX_PROGRESS during scan.
*
*/
static int ctrl_scan_next(pdrx_demod_instance_t demod, u16 *scan_progress)
{
	pdrx_common_attr_t common_attr = (pdrx_common_attr_t) (NULL);
	bool *scan_ready = (bool *)(NULL);
	u16 max_progress = DRX_SCAN_MAX_PROGRESS;
	u32 num_tries = 0;
	u32 i = 0;

	common_attr = (pdrx_common_attr_t) demod->my_common_attr;

	/* Check scan parameters */
	if (scan_progress == NULL) {
		common_attr->scan_active = false;
		return DRX_STS_INVALID_ARG;
	}

	*scan_progress = 0;
	common_attr->scan_active = true;
	if ((common_attr->scan_param == NULL) ||
	    (common_attr->scan_max_channels == 0)) {
		/* ctrl_scan_init() was not called succesfully before ctrl_scan_next() */
		common_attr->scan_active = false;
		return DRX_STS_ERROR;
	}

	*scan_progress = (u16) (((common_attr->scan_channelsScanned) *
				  ((u32) (max_progress))) /
				 (common_attr->scan_max_channels));

	/* Scan */
	num_tries = common_attr->scan_param->num_tries;
	scan_ready = &(common_attr->scan_ready);

	for (i = 0; ((i < num_tries) && ((*scan_ready) == false)); i++) {
		drx_channel_t scan_channel = { 0 };
		int status = DRX_STS_ERROR;
		p_drx_frequency_plan_t freq_plan = (p_drx_frequency_plan_t) (NULL);
		bool next_channel = false;
		void *scan_context = NULL;

		/* Next channel to scan */
		freq_plan =
		    &(common_attr->scan_param->
		      frequency_plan[common_attr->scan_freq_plan_index]);
		scan_channel.frequency = common_attr->scan_next_frequency;
		scan_channel.bandwidth = freq_plan->bandwidth;
		scan_channel.mirror = DRX_MIRROR_AUTO;
		scan_channel.constellation = DRX_CONSTELLATION_AUTO;
		scan_channel.hierarchy = DRX_HIERARCHY_AUTO;
		scan_channel.priority = DRX_PRIORITY_HIGH;
		scan_channel.coderate = DRX_CODERATE_AUTO;
		scan_channel.guard = DRX_GUARD_AUTO;
		scan_channel.fftmode = DRX_FFTMODE_AUTO;
		scan_channel.classification = DRX_CLASSIFICATION_AUTO;
		scan_channel.symbolrate = 0;
		scan_channel.interleavemode = DRX_INTERLEAVEMODE_AUTO;
		scan_channel.ldpc = DRX_LDPC_AUTO;
		scan_channel.carrier = DRX_CARRIER_AUTO;
		scan_channel.framemode = DRX_FRAMEMODE_AUTO;
		scan_channel.pilot = DRX_PILOT_AUTO;

		/* Call default or device-specific scanning function */
		scan_context = get_scan_context(demod, scan_context);

		status = (*(get_scan_function(demod)))
		    (scan_context, DRX_SCAN_COMMAND_NEXT, &scan_channel,
		     &next_channel);

		/* Proceed to next channel if requested */
		if (next_channel == true) {
			int next_status = DRX_STS_ERROR;
			s32 skip = 0;

			if (status == DRX_STS_OK) {
				/* a channel was found, so skip some frequency steps */
				skip = common_attr->scan_param->skip;
			}
			next_status = scan_prepare_next_scan(demod, skip);

			/* keep track of progress */
			*scan_progress =
			    (u16) (((common_attr->scan_channelsScanned) *
				      ((u32) (max_progress))) /
				     (common_attr->scan_max_channels));

			if (next_status != DRX_STS_OK) {
				common_attr->scan_active = false;
				return (next_status);
			}
		}
		if (status != DRX_STS_BUSY) {
			/* channel found or error */
			common_attr->scan_active = false;
			return status;
		}
	}			/* for ( i = 0; i < ( ... num_tries); i++) */

	if ((*scan_ready) == true) {
		/* End of scan reached: call stop-scan, ignore any error */
		ctrl_scan_stop(demod);
		common_attr->scan_active = false;
		return (DRX_STS_READY);
	}

	common_attr->scan_active = false;

	return DRX_STS_BUSY;
}

#endif /* #ifndef DRX_EXCLUDE_SCAN */

/*============================================================================*/

/**
* \brief Program tuner.
* \param demod:         Pointer to demodulator instance.
* \param tunerChannel:  Pointer to tuning parameters.
* \return int.
* \retval DRX_STS_OK:          Tuner programmed successfully.
* \retval DRX_STS_ERROR:       Something went wrong.
* \retval DRX_STS_INVALID_ARG: Wrong parameters.
*
* tunerChannel passes parameters to program the tuner,
* but also returns the actual RF and IF frequency from the tuner.
*
*/
static int
ctrl_program_tuner(pdrx_demod_instance_t demod, pdrx_channel_t channel)
{
	pdrx_common_attr_t common_attr = (pdrx_common_attr_t) (NULL);
	enum drx_standard standard = DRX_STANDARD_UNKNOWN;
	u32 tuner_mode = 0;
	int status = DRX_STS_ERROR;
	s32 if_frequency = 0;
	bool tuner_slow_mode = false;

	/* can't tune without a tuner */
	if (demod->my_tuner == NULL) {
		return DRX_STS_INVALID_ARG;
	}

	common_attr = (pdrx_common_attr_t) demod->my_common_attr;

	/* select analog or digital tuner mode based on current standard */
	if (drx_ctrl(demod, DRX_CTRL_GET_STANDARD, &standard) != DRX_STS_OK) {
		return DRX_STS_ERROR;
	}

	if (DRX_ISATVSTD(standard)) {
		tuner_mode |= TUNER_MODE_ANALOG;
	} else {		/* note: also for unknown standard */

		tuner_mode |= TUNER_MODE_DIGITAL;
	}

	/* select tuner bandwidth */
	switch (channel->bandwidth) {
	case DRX_BANDWIDTH_6MHZ:
		tuner_mode |= TUNER_MODE_6MHZ;
		break;
	case DRX_BANDWIDTH_7MHZ:
		tuner_mode |= TUNER_MODE_7MHZ;
		break;
	case DRX_BANDWIDTH_8MHZ:
		tuner_mode |= TUNER_MODE_8MHZ;
		break;
	default:		/* note: also for unknown bandwidth */
		return DRX_STS_INVALID_ARG;
	}

	DRX_GET_TUNERSLOWMODE(demod, tuner_slow_mode);

	/* select fast (switch) or slow (lock) tuner mode */
	if (tuner_slow_mode) {
		tuner_mode |= TUNER_MODE_LOCK;
	} else {
		tuner_mode |= TUNER_MODE_SWITCH;
	}

	if (common_attr->tuner_port_nr == 1) {
		bool bridge_closed = true;
		int status_bridge = DRX_STS_ERROR;

		status_bridge =
		    drx_ctrl(demod, DRX_CTRL_I2C_BRIDGE, &bridge_closed);
		if (status_bridge != DRX_STS_OK) {
			return status_bridge;
		}
	}

	status = drxbsp_tuner_set_frequency(demod->my_tuner,
					   tuner_mode, channel->frequency);

	/* attempt restoring bridge before checking status of set_frequency */
	if (common_attr->tuner_port_nr == 1) {
		bool bridge_closed = false;
		int status_bridge = DRX_STS_ERROR;

		status_bridge =
		    drx_ctrl(demod, DRX_CTRL_I2C_BRIDGE, &bridge_closed);
		if (status_bridge != DRX_STS_OK) {
			return status_bridge;
		}
	}

	/* now check status of drxbsp_tuner_set_frequency */
	if (status != DRX_STS_OK) {
		return status;
	}

	/* get actual RF and IF frequencies from tuner */
	status = drxbsp_tuner_get_frequency(demod->my_tuner,
					   tuner_mode,
					   &(channel->frequency),
					   &(if_frequency));
	if (status != DRX_STS_OK) {
		return status;
	}

	/* update common attributes with information available from this function;
	   TODO: check if this is required and safe */
	DRX_SET_INTERMEDIATEFREQ(demod, if_frequency);

	return DRX_STS_OK;
}

/*============================================================================*/

/**
* \brief function to do a register dump.
* \param demod:            Pointer to demodulator instance.
* \param registers:        Registers to dump.
* \return int.
* \retval DRX_STS_OK:          Dump executed successfully.
* \retval DRX_STS_ERROR:       Something went wrong.
* \retval DRX_STS_INVALID_ARG: Wrong parameters.
*
*/
static int ctrl_dump_registers(pdrx_demod_instance_t demod,
			      p_drx_reg_dump_t registers)
{
	u16 i = 0;

	if (registers == NULL) {
		/* registers not supplied */
		return DRX_STS_INVALID_ARG;
	}

	/* start dumping registers */
	while (registers[i].address != 0) {
		int status = DRX_STS_ERROR;
		u16 value = 0;
		u32 data = 0;

		status =
		    demod->my_access_funct->read_reg16func(demod->my_i2c_dev_addr,
							registers[i].address,
							&value, 0);

		data = (u32) value;

		if (status != DRX_STS_OK) {
			/* no breakouts;
			   depending on device ID, some HW blocks might not be available */
			data |= ((u32) status) << 16;
		}
		registers[i].data = data;
		i++;
	}

	/* all done, all OK (any errors are saved inside data) */
	return DRX_STS_OK;
}

/*============================================================================*/
/*============================================================================*/
/*===Microcode related functions==============================================*/
/*============================================================================*/
/*============================================================================*/

/**
* \brief Read a 16 bits word, expects big endian data.
* \param addr: Pointer to memory from which to read the 16 bits word.
* \return u16 The data read.
*
* This function takes care of the possible difference in endianness between the
* host and the data contained in the microcode image file.
*
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
* \brief Read a 32 bits word, expects big endian data.
* \param addr: Pointer to memory from which to read the 32 bits word.
* \return u32 The data read.
*
* This function takes care of the possible difference in endianness between the
* host and the data contained in the microcode image file.
*
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
* \brief Compute CRC of block of microcode data.
* \param block_data: Pointer to microcode data.
* \param nr_words:   Size of microcode block (number of 16 bits words).
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
			if (carry != 0) {
				crc_word ^= 0x80050000UL;
			}
			carry = crc_word & 0x80000000UL;
		}
		i++;
		block_data += (sizeof(u16));
	}
	return ((u16) (crc_word >> 16));
}

/*============================================================================*/

/**
* \brief Handle microcode upload or verify.
* \param dev_addr: Address of device.
* \param mc_info:  Pointer to information about microcode data.
* \param action:  Either UCODE_UPLOAD or UCODE_VERIFY
* \return int.
* \retval DRX_STS_OK:
*                    - In case of UCODE_UPLOAD: code is successfully uploaded.
*                    - In case of UCODE_VERIFY: image on device is equal to
*                      image provided to this control function.
* \retval DRX_STS_ERROR:
*                    - In case of UCODE_UPLOAD: I2C error.
*                    - In case of UCODE_VERIFY: I2C error or image on device
*                      is not equal to image provided to this control function.
* \retval DRX_STS_INVALID_ARG:
*                    - Invalid arguments.
*                    - Provided image is corrupt
*/
static int
ctrl_u_code(pdrx_demod_instance_t demod,
	    p_drxu_code_info_t mc_info, drxu_code_action_t action)
{
	int rc;
	u16 i = 0;
	u16 mc_nr_of_blks = 0;
	u16 mc_magic_word = 0;
	u8 *mc_data = (u8 *)(NULL);
	struct i2c_device_addr *dev_addr = (struct i2c_device_addr *)(NULL);

	dev_addr = demod->my_i2c_dev_addr;

	/* Check arguments */
	if ((mc_info == NULL) || (mc_info->mc_data == NULL)) {
		return DRX_STS_INVALID_ARG;
	}

	mc_data = mc_info->mc_data;

	/* Check data */
	mc_magic_word = u_code_read16(mc_data);
	mc_data += sizeof(u16);
	mc_nr_of_blks = u_code_read16(mc_data);
	mc_data += sizeof(u16);

	if ((mc_magic_word != DRX_UCODE_MAGIC_WORD) || (mc_nr_of_blks == 0)) {
		/* wrong endianess or wrong data ? */
		return DRX_STS_INVALID_ARG;
	}

	/* Scan microcode blocks first for version info if uploading */
	if (action == UCODE_UPLOAD) {
		/* Clear version block */
		DRX_SET_MCVERTYPE(demod, 0);
		DRX_SET_MCDEV(demod, 0);
		DRX_SET_MCVERSION(demod, 0);
		DRX_SET_MCPATCH(demod, 0);
		for (i = 0; i < mc_nr_of_blks; i++) {
			drxu_code_block_hdr_t block_hdr;

			/* Process block header */
			block_hdr.addr = u_code_read32(mc_data);
			mc_data += sizeof(u32);
			block_hdr.size = u_code_read16(mc_data);
			mc_data += sizeof(u16);
			block_hdr.flags = u_code_read16(mc_data);
			mc_data += sizeof(u16);
			block_hdr.CRC = u_code_read16(mc_data);
			mc_data += sizeof(u16);

			if (block_hdr.flags & 0x8) {
				/* Aux block. Check type */
				u8 *auxblk = mc_info->mc_data + block_hdr.addr;
				u16 auxtype = u_code_read16(auxblk);
				if (DRX_ISMCVERTYPE(auxtype)) {
					DRX_SET_MCVERTYPE(demod,
							  u_code_read16(auxblk));
					auxblk += sizeof(u16);
					DRX_SET_MCDEV(demod,
						      u_code_read32(auxblk));
					auxblk += sizeof(u32);
					DRX_SET_MCVERSION(demod,
							  u_code_read32(auxblk));
					auxblk += sizeof(u32);
					DRX_SET_MCPATCH(demod,
							u_code_read32(auxblk));
				}
			}

			/* Next block */
			mc_data += block_hdr.size * sizeof(u16);
		}

		/* After scanning, validate the microcode.
		   It is also valid if no validation control exists.
		 */
		rc = drx_ctrl(demod, DRX_CTRL_VALIDATE_UCODE, NULL);
		if (rc != DRX_STS_OK && rc != DRX_STS_FUNC_NOT_AVAILABLE) {
			return rc;
		}

		/* Restore data pointer */
		mc_data = mc_info->mc_data + 2 * sizeof(u16);
	}

	/* Process microcode blocks */
	for (i = 0; i < mc_nr_of_blks; i++) {
		drxu_code_block_hdr_t block_hdr;
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
		   - data larger than 64Kb
		   - if CRC enabled check CRC
		 */
		if ((block_hdr.size > 0x7FFF) ||
		    (((block_hdr.flags & DRX_UCODE_CRC_FLAG) != 0) &&
		     (block_hdr.CRC != u_code_compute_crc(mc_data, block_hdr.size)))
		    ) {
			/* Wrong data ! */
			return DRX_STS_INVALID_ARG;
		}

		mc_block_nr_bytes = block_hdr.size * ((u16) sizeof(u16));

		if (block_hdr.size != 0) {
			/* Perform the desired action */
			switch (action) {
	    /*================================================================*/
			case UCODE_UPLOAD:
				{
					/* Upload microcode */
					if (demod->my_access_funct->
					    write_block_func(dev_addr,
							   (dr_xaddr_t) block_hdr.
							   addr, mc_block_nr_bytes,
							   mc_data,
							   0x0000) !=
					    DRX_STS_OK) {
						return (DRX_STS_ERROR);
					}	/* if */
				};
				break;

	    /*================================================================*/
			case UCODE_VERIFY:
				{
					int result = 0;
					u8 mc_dataBuffer
					    [DRX_UCODE_MAX_BUF_SIZE];
					u32 bytes_to_compare = 0;
					u32 bytes_left_to_compare = 0;
					dr_xaddr_t curr_addr = (dr_xaddr_t) 0;
					u8 *curr_ptr = NULL;

					bytes_left_to_compare = mc_block_nr_bytes;
					curr_addr = block_hdr.addr;
					curr_ptr = mc_data;

					while (bytes_left_to_compare != 0) {
						if (bytes_left_to_compare >
						    ((u32)
						     DRX_UCODE_MAX_BUF_SIZE)) {
							bytes_to_compare =
							    ((u32)
							     DRX_UCODE_MAX_BUF_SIZE);
						} else {
							bytes_to_compare =
							    bytes_left_to_compare;
						}

						if (demod->my_access_funct->
						    read_block_func(dev_addr,
								  curr_addr,
								  (u16)
								  bytes_to_compare,
								  (u8 *)
								  mc_dataBuffer,
								  0x0000) !=
						    DRX_STS_OK) {
							return (DRX_STS_ERROR);
						}

						result =
						    drxbsp_hst_memcmp(curr_ptr,
								      mc_dataBuffer,
								      bytes_to_compare);

						if (result != 0) {
							return DRX_STS_ERROR;
						}

						curr_addr +=
						    ((dr_xaddr_t)
						     (bytes_to_compare / 2));
						curr_ptr =
						    &(curr_ptr[bytes_to_compare]);
						bytes_left_to_compare -=
						    ((u32) bytes_to_compare);
					}	/* while( bytes_to_compare > DRX_UCODE_MAX_BUF_SIZE ) */
				};
				break;

	    /*================================================================*/
			default:
				return DRX_STS_INVALID_ARG;
				break;

			}	/* switch ( action ) */
		}

		/* if (block_hdr.size != 0 ) */
		/* Next block */
		mc_data += mc_block_nr_bytes;

	}			/* for( i = 0 ; i<mc_nr_of_blks ; i++ ) */

	return DRX_STS_OK;
}

/*============================================================================*/

/**
* \brief Build list of version information.
* \param demod: A pointer to a demodulator instance.
* \param version_list: Pointer to linked list of versions.
* \return int.
* \retval DRX_STS_OK:          Version information stored in version_list
* \retval DRX_STS_INVALID_ARG: Invalid arguments.
*/
static int
ctrl_version(pdrx_demod_instance_t demod, p_drx_version_list_t *version_list)
{
	static char drx_driver_core_module_name[] = "Core driver";
	static char drx_driver_core_version_text[] =
	    DRX_VERSIONSTRING(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

	static drx_version_t drx_driver_core_version;
	static drx_version_list_t drx_driver_core_versionList;

	p_drx_version_list_t demod_version_list = (p_drx_version_list_t) (NULL);
	int return_status = DRX_STS_ERROR;

	/* Check arguments */
	if (version_list == NULL) {
		return DRX_STS_INVALID_ARG;
	}

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

	drx_driver_core_versionList.version = &drx_driver_core_version;
	drx_driver_core_versionList.next = (p_drx_version_list_t) (NULL);

	if ((return_status == DRX_STS_OK) && (demod_version_list != NULL)) {
		/* Append versioninfo from driver to versioninfo from demod  */
		/* Return version info in "bottom-up" order. This way, multiple
		   devices can be handled without using malloc. */
		p_drx_version_list_t current_list_element = demod_version_list;
		while (current_list_element->next != NULL) {
			current_list_element = current_list_element->next;
		}
		current_list_element->next = &drx_driver_core_versionList;

		*version_list = demod_version_list;
	} else {
		/* Just return versioninfo from driver */
		*version_list = &drx_driver_core_versionList;
	}

	return DRX_STS_OK;
}

/*============================================================================*/
/*============================================================================*/
/*== Exported functions ======================================================*/
/*============================================================================*/
/*============================================================================*/

/**
* \brief This function is obsolete.
* \param demods: Don't care, parameter is ignored.
* \return int Return status.
* \retval DRX_STS_OK: Initialization completed.
*
* This function is obsolete, prototype available for backward compatability.
*
*/

int drx_init(pdrx_demod_instance_t demods[])
{
	return DRX_STS_OK;
}

/*============================================================================*/

/**
* \brief This function is obsolete.
* \return int Return status.
* \retval DRX_STS_OK: Terminated driver successful.
*
* This function is obsolete, prototype available for backward compatability.
*
*/

int drx_term(void)
{
	return DRX_STS_OK;
}

/*============================================================================*/

/**
* \brief Open a demodulator instance.
* \param demod: A pointer to a demodulator instance.
* \return int Return status.
* \retval DRX_STS_OK:          Opened demod instance with succes.
* \retval DRX_STS_ERROR:       Driver not initialized or unable to initialize
*                              demod.
* \retval DRX_STS_INVALID_ARG: Demod instance has invalid content.
*
*/

int drx_open(pdrx_demod_instance_t demod)
{
	int status = DRX_STS_OK;

	if ((demod == NULL) ||
	    (demod->my_demod_funct == NULL) ||
	    (demod->my_common_attr == NULL) ||
	    (demod->my_ext_attr == NULL) ||
	    (demod->my_i2c_dev_addr == NULL) ||
	    (demod->my_common_attr->is_opened == true)) {
		return (DRX_STS_INVALID_ARG);
	}

	status = (*(demod->my_demod_funct->open_func)) (demod);

	if (status == DRX_STS_OK) {
		demod->my_common_attr->is_opened = true;
	}

	return status;
}

/*============================================================================*/

/**
* \brief Close device.
* \param demod: A pointer to a demodulator instance.
* \return int Return status.
* \retval DRX_STS_OK:          Closed demod instance with succes.
* \retval DRX_STS_ERROR:       Driver not initialized or error during close
*                              demod.
* \retval DRX_STS_INVALID_ARG: Demod instance has invalid content.
*
* Free resources occupied by device instance.
* Put device into sleep mode.
*/

int drx_close(pdrx_demod_instance_t demod)
{
	int status = DRX_STS_OK;

	if ((demod == NULL) ||
	    (demod->my_demod_funct == NULL) ||
	    (demod->my_common_attr == NULL) ||
	    (demod->my_ext_attr == NULL) ||
	    (demod->my_i2c_dev_addr == NULL) ||
	    (demod->my_common_attr->is_opened == false)) {
		return DRX_STS_INVALID_ARG;
	}

	status = (*(demod->my_demod_funct->close_func)) (demod);

	DRX_SET_ISOPENED(demod, false);

	return status;
}

/*============================================================================*/

/**
* \brief Control the device.
* \param demod:    A pointer to a demodulator instance.
* \param ctrl:     Reference to desired control function.
* \param ctrl_data: Pointer to data structure for control function.
* \return int Return status.
* \retval DRX_STS_OK:                 Control function completed successfully.
* \retval DRX_STS_ERROR:              Driver not initialized or error during
*                                     control demod.
* \retval DRX_STS_INVALID_ARG:        Demod instance or ctrl_data has invalid
*                                     content.
* \retval DRX_STS_FUNC_NOT_AVAILABLE: Specified control function is not
*                                     available.
*
* Data needed or returned by the control function is stored in ctrl_data.
*
*/

int
drx_ctrl(pdrx_demod_instance_t demod, u32 ctrl, void *ctrl_data)
{
	int status = DRX_STS_ERROR;

	if ((demod == NULL) ||
	    (demod->my_demod_funct == NULL) ||
	    (demod->my_common_attr == NULL) ||
	    (demod->my_ext_attr == NULL) || (demod->my_i2c_dev_addr == NULL)
	    ) {
		return (DRX_STS_INVALID_ARG);
	}

	if (((demod->my_common_attr->is_opened == false) &&
	     (ctrl != DRX_CTRL_PROBE_DEVICE) && (ctrl != DRX_CTRL_VERSION))
	    ) {
		return (DRX_STS_INVALID_ARG);
	}

	if ((DRX_ISPOWERDOWNMODE(demod->my_common_attr->current_power_mode) &&
	     (ctrl != DRX_CTRL_POWER_MODE) &&
	     (ctrl != DRX_CTRL_PROBE_DEVICE) &&
	     (ctrl != DRX_CTRL_NOP) && (ctrl != DRX_CTRL_VERSION)
	    )
	    ) {
		return DRX_STS_FUNC_NOT_AVAILABLE;
	}

	/* Fixed control functions */
	switch (ctrl) {
      /*======================================================================*/
	case DRX_CTRL_NOP:
		/* No operation */
		return DRX_STS_OK;
		break;

      /*======================================================================*/
	case DRX_CTRL_VERSION:
		return ctrl_version(demod, (p_drx_version_list_t *)ctrl_data);
		break;

      /*======================================================================*/
	default:
		/* Do nothing */
		break;
	}

	/* Virtual functions */
	/* First try calling function from derived class */
	status = (*(demod->my_demod_funct->ctrl_func)) (demod, ctrl, ctrl_data);
	if (status == DRX_STS_FUNC_NOT_AVAILABLE) {
		/* Now try calling a the base class function */
		switch (ctrl) {
	 /*===================================================================*/
		case DRX_CTRL_LOAD_UCODE:
			return ctrl_u_code(demod,
					 (p_drxu_code_info_t) ctrl_data,
					 UCODE_UPLOAD);
			break;

	 /*===================================================================*/
		case DRX_CTRL_VERIFY_UCODE:
			{
				return ctrl_u_code(demod,
						 (p_drxu_code_info_t) ctrl_data,
						 UCODE_VERIFY);
			}
			break;

#ifndef DRX_EXCLUDE_SCAN
	 /*===================================================================*/
		case DRX_CTRL_SCAN_INIT:
			{
				return ctrl_scan_init(demod,
						    (p_drx_scan_param_t) ctrl_data);
			}
			break;

	 /*===================================================================*/
		case DRX_CTRL_SCAN_NEXT:
			{
				return ctrl_scan_next(demod, (u16 *)ctrl_data);
			}
			break;

	 /*===================================================================*/
		case DRX_CTRL_SCAN_STOP:
			{
				return ctrl_scan_stop(demod);
			}
			break;
#endif /* #ifndef DRX_EXCLUDE_SCAN */

	 /*===================================================================*/
		case DRX_CTRL_PROGRAM_TUNER:
			{
				return ctrl_program_tuner(demod,
							(pdrx_channel_t)
							ctrl_data);
			}
			break;

	 /*===================================================================*/
		case DRX_CTRL_DUMP_REGISTERS:
			{
				return ctrl_dump_registers(demod,
							 (p_drx_reg_dump_t)
							 ctrl_data);
			}
			break;

	 /*===================================================================*/
		default:
			return DRX_STS_FUNC_NOT_AVAILABLE;
		}
	} else {
		return (status);
	}

	return DRX_STS_OK;
}

/*============================================================================*/

/* END OF FILE */
