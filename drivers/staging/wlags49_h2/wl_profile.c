/*******************************************************************************
 * Agere Systems Inc.
 * Wireless device driver for Linux (wlags49).
 *
 * Copyright (c) 1998-2003 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * Initially developed by TriplePoint, Inc.
 *   http://www.triplepoint.com
 *
 *------------------------------------------------------------------------------
 *
 *   This file defines routines required to parse configuration parameters
 *   listed in a config file, if that config file exists.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2003 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 ******************************************************************************/

/* Only include this file if USE_PROFILE is defined */
#ifdef USE_PROFILE




/*******************************************************************************
 *  constant definitions
 ******************************************************************************/


/* Allow support for calling system fcns to parse config file */
#define __KERNEL_SYSCALLS__




/*******************************************************************************
 * include files
 ******************************************************************************/
#include <wl_version.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/unistd.h>
#include <asm/uaccess.h>
#include <limits.h>

#define BIN_DL 1

#include <debug.h>
#include <hcf.h>
/* #include <hcfdef.h> */

#include <wl_if.h>
#include <wl_internal.h>
#include <wl_util.h>
#include <wl_enc.h>
#include <wl_main.h>
#include <wl_profile.h>


/*******************************************************************************
 * global variables
 ******************************************************************************/

/* Definition needed to prevent unresolved external in unistd.h */
static int errno;

#if DBG
extern p_u32    DebugFlag;
extern dbg_info_t *DbgInfo;
#endif

int parse_yes_no(char *value);


int parse_yes_no(char *value)
{
int rc = 0;										/* default to NO for invalid parameters */

	if (strlen(value) == 1) {
		if ((value[0] | ('Y'^'y')) == 'y')
			rc = 1;
	/* } else { */
		/* this should not be debug time info, it is an enduser data entry error ;? */
		/* DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_MICROWAVE_ROBUSTNESS); */
	}
	return rc;
} /* parse_yes_no */


/*******************************************************************************
 *	parse_config()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function opens the device's config file and parses the options from
 *   it, so that it can properly configure itself. If no configuration file
 *   or configuration is present, then continue to use the options already
 *   parsed from config.opts or wireless.opts.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void parse_config(struct net_device *dev)
{
	int				    file_desc;
#if 0 /* BIN_DL */
	int				rc;
	char				*cp = NULL;
#endif /* BIN_DL */
	char                buffer[MAX_LINE_SIZE];
	char                filename[MAX_LINE_SIZE];
	mm_segment_t	    fs;
	struct wl_private   *wvlan_config = NULL;
	ENCSTRCT            sEncryption;
	/*------------------------------------------------------------------------*/

	DBG_FUNC("parse_config");
	DBG_ENTER(DbgInfo);

	/* Get the wavelan specific info for this device */
	wvlan_config = dev->priv;
	if (wvlan_config == NULL) {
		DBG_ERROR(DbgInfo, "Wavelan specific info struct not present?\n");
		return;
	}

	/* setup the default encryption string */
	strcpy(wvlan_config->szEncryption, DEF_CRYPT_STR);

	/* Obtain a user-space process context, storing the original context */
	fs = get_fs();
	set_fs(get_ds());

	/* Determine the filename for this device and attempt to open it */
	sprintf(filename, "%s%s", ROOT_CONFIG_FILENAME, dev->name);
	file_desc = open(filename, O_RDONLY, 0);
	if (file_desc != -1) {
		DBG_TRACE(DbgInfo, "Wireless config file found. Parsing options...\n");

		/* Read out the options */
		while (readline(file_desc, buffer))
			translate_option(buffer, wvlan_config);
		/* Close the file */
		close(file_desc);	/* ;?even if file_desc == -1 ??? */
	} else {
		DBG_TRACE(DbgInfo, "No iwconfig file found for this device; "
				   "config.opts or wireless.opts will be used\n");
	}
	/* Return to the original context */
	set_fs(fs);

	/* convert the WEP keys, if read in as key1, key2, type of data */
	if (wvlan_config->EnableEncryption) {
		memset(&sEncryption, 0, sizeof(sEncryption));

		wl_wep_decode(CRYPT_CODE, &sEncryption,
						   wvlan_config->szEncryption);

		/* the Linux driver likes to use 1-4 for the key IDs, and then
		   convert to 0-3 when sending to the card.  The Windows code
		   base used 0-3 in the API DLL, which was ported to Linux.  For
		   the sake of the user experience, we decided to keep 0-3 as the
		   numbers used in the DLL; and will perform the +1 conversion here.
		   We could have converted  the entire Linux driver, but this is
		   less obtrusive.  This may be a "todo" to convert the whole driver */
		sEncryption.wEnabled = wvlan_config->EnableEncryption;
		sEncryption.wTxKeyID = wvlan_config->TransmitKeyID - 1;

		memcpy(&sEncryption.EncStr, &wvlan_config->DefaultKeys,
				sizeof(CFG_DEFAULT_KEYS_STRCT));

		memset(wvlan_config->szEncryption, 0, sizeof(wvlan_config->szEncryption));

		wl_wep_code(CRYPT_CODE, wvlan_config->szEncryption, &sEncryption,
						 sizeof(sEncryption));
	}

	/* decode the encryption string for the call to wl_commit() */
	wl_wep_decode(CRYPT_CODE, &sEncryption, wvlan_config->szEncryption);

	wvlan_config->TransmitKeyID    = sEncryption.wTxKeyID + 1;
	wvlan_config->EnableEncryption = sEncryption.wEnabled;

	memcpy(&wvlan_config->DefaultKeys, &sEncryption.EncStr,
			sizeof(CFG_DEFAULT_KEYS_STRCT));

#if 0 /* BIN_DL */
		/* Obtain a user-space process context, storing the original context */
		fs = get_fs();
		set_fs(get_ds());

		/* ;?just to fake something */
		strcpy(/*wvlan_config->fw_image_*/filename, "/etc/agere/fw.bin");
		file_desc = open(/*wvlan_config->fw_image_*/filename, 0, 0);
		if (file_desc == -1) {
			DBG_ERROR(DbgInfo, "No image file found\n");
		} else {
			DBG_TRACE(DbgInfo, "F/W image file found\n");
#define DHF_ALLOC_SIZE 96000			/* just below 96K, let's hope it suffices for now and for the future */
			cp = vmalloc(DHF_ALLOC_SIZE);
			if (cp == NULL) {
				DBG_ERROR(DbgInfo, "error in vmalloc\n");
			} else {
				rc = read(file_desc, cp, DHF_ALLOC_SIZE);
				if (rc == DHF_ALLOC_SIZE) {
					DBG_ERROR(DbgInfo, "buffer too small, %d\n", DHF_ALLOC_SIZE);
				} else if (rc > 0) {
					DBG_TRACE(DbgInfo, "read O.K.: %d bytes  %.12s\n", rc, cp);
					rc = read(file_desc, &cp[rc], 1);
					if (rc == 0)
						DBG_TRACE(DbgInfo, "no more to read\n");
				}
				if (rc != 0) {
					DBG_ERROR(DbgInfo, "file not read in one swoop or other error"\
										", give up, too complicated, rc = %0X\n", rc);
				}
				vfree(cp);
			}
			close(file_desc);
		}
		set_fs(fs);			/* Return to the original context */
#endif /* BIN_DL */

	DBG_LEAVE(DbgInfo);
	return;
} /* parse_config */

/*******************************************************************************
 *	readline()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function reads in data from a given file one line at a time,
 *   converting the detected newline character '\n' to a null '\0'. Note that
 *   the file descriptor must be valid before calling this function.
 *
 *  PARAMETERS:
 *
 *      filedesc    - the file descriptor for the open configuration file
 *      buffer      - a buffer pointer, passed in by the caller, to which the
 *                    line will be stored.
 *
 *  RETURNS:
 *
 *      the number of bytes read
 *      -1 on error
 *
 ******************************************************************************/
int readline(int filedesc, char *buffer)
{
	int result = -1;
	int bytes_read = 0;
	/*------------------------------------------------------------------------*/

	/* Make sure the file descriptor is good */
	if (filedesc != -1) {
		/* Read in from the file byte by byte until a newline is reached */
		while ((result = read(filedesc, &buffer[bytes_read], 1)) == 1) {
			if (buffer[bytes_read] == '\n') {
				buffer[bytes_read] = '\0';
				bytes_read++;
				break;
			}
			bytes_read++;
		}
	}

	/* Return the number of bytes read */
	if (result == -1)
		return result;
	else
		return bytes_read;
} /* readline */
/*============================================================================*/

/*******************************************************************************
 *	translate_option()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function takes a line read in from the config file and parses out
 *   the key/value pairs. It then determines which key has been parsed and sets
 *   the card's configuration based on the value given.
 *
 *  PARAMETERS:
 *
 *      buffer - a buffer containing a line to translate
 *      config - a pointer to the device's private adapter structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void translate_option(char *buffer, struct wl_private *lp)
{
	unsigned int value_convert = 0;
	int string_length = 0;
	char *key = NULL;
	char *value = NULL;
	u_char mac_value[ETH_ALEN];
	/*------------------------------------------------------------------------*/

	DBG_FUNC("translate_option");

	if (buffer == NULL || lp == NULL) {
		DBG_ERROR(DbgInfo, "Config file buffer and/or wavelan buffer ptr NULL\n");
		return;
	}

	ParseConfigLine(buffer, &key, &value);

	if (key == NULL || value == NULL)
		return;

	/* Determine which key it is and perform the appropriate action */

	/* Configuration parameters used in all scenarios */
#if DBG
	/* handle DebugFlag as early as possible so it starts its influence as early
	 * as possible
	 */
	if (strcmp(key, PARM_NAME_DEBUG_FLAG) == 0) {
		if (DebugFlag == ~0) {			/* if DebugFlag is not specified on the command line */
			if (DbgInfo->DebugFlag == 0) {	/* if pc_debug did not set DebugFlag (i.e.pc_debug is
											 * not specified or specified outside the 4-8 range
											 */
				DbgInfo->DebugFlag |= DBG_DEFAULTS;
			}
		} else {
			DbgInfo->DebugFlag = simple_strtoul(value, NULL, 0); /* ;?DebugFlag; */
		}
		DbgInfo->DebugFlag = simple_strtoul(value, NULL, 0); /* ;?Delete ASAP */
	}
#endif /* DBG */
	if (strcmp(key, PARM_NAME_AUTH_KEY_MGMT_SUITE) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_AUTH_KEY_MGMT_SUITE, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_AUTH_KEY_MGMT_SUITE) || (value_convert <= PARM_MAX_AUTH_KEY_MGMT_SUITE))
			lp->AuthKeyMgmtSuite = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_AUTH_KEY_MGMT_SUITE);
	} else if (strcmp(key, PARM_NAME_BRSC_2GHZ) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_BRSC_2GHZ, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_BRSC) || (value_convert <= PARM_MAX_BRSC))
			lp->brsc[0] = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invaid; will be ignored\n", PARM_NAME_BRSC_2GHZ);
	} else if (strcmp(key, PARM_NAME_BRSC_5GHZ) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_BRSC_5GHZ, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_BRSC) || (value_convert <= PARM_MAX_BRSC))
			lp->brsc[1] = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invaid; will be ignored\n", PARM_NAME_BRSC_5GHZ);
	} else if ((strcmp(key, PARM_NAME_DESIRED_SSID) == 0) || (strcmp(key, PARM_NAME_OWN_SSID) == 0)) {
		DBG_TRACE(DbgInfo, "SSID, value: %s\n", value);

		memset(lp->NetworkName, 0, (PARM_MAX_NAME_LEN + 1));

		/* Make sure the value isn't too long */
		string_length = strlen(value);
		if (string_length > PARM_MAX_NAME_LEN) {
			DBG_WARNING(DbgInfo, "SSID too long; will be truncated\n");
			string_length = PARM_MAX_NAME_LEN;
		}

		memcpy(lp->NetworkName, value, string_length);
	}
#if 0
	else if (strcmp(key, PARM_NAME_DOWNLOAD_FIRMWARE) == 0) {
		DBG_TRACE(DbgInfo, "DOWNLOAD_FIRMWARE, value: %s\n", value);
		memset(lp->fw_image_filename, 0, (MAX_LINE_SIZE + 1));
		/* Make sure the value isn't too long */
		string_length = strlen(value);
		if (string_length > MAX_LINE_SIZE)
			DBG_WARNING(DbgInfo, "F/W image file name too long; will be ignored\n");
		else
			memcpy(lp->fw_image_filename, value, string_length);
	}
#endif
	else if (strcmp(key, PARM_NAME_ENABLE_ENCRYPTION) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_ENABLE_ENCRYPTION, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_ENABLE_ENCRYPTION) && (value_convert <= PARM_MAX_ENABLE_ENCRYPTION))
			lp->EnableEncryption = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_ENABLE_ENCRYPTION);
	} else if (strcmp(key, PARM_NAME_ENCRYPTION) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_ENCRYPTION, value);

		memset(lp->szEncryption, 0, sizeof(lp->szEncryption));

		/* Make sure the value isn't too long */
		string_length = strlen(value);
		if (string_length > sizeof(lp->szEncryption)) {
			DBG_WARNING(DbgInfo, "%s too long; will be truncated\n", PARM_NAME_ENCRYPTION);
			string_length = sizeof(lp->szEncryption);
		}

		memcpy(lp->szEncryption, value, string_length);
	} else if (strcmp(key, PARM_NAME_KEY1) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_KEY1, value);

		if (is_valid_key_string(value)) {
			memset(lp->DefaultKeys.key[0].key, 0, MAX_KEY_SIZE);

			key_string2key(value, &lp->DefaultKeys.key[0]);
		} else {
			 DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_KEY1);
		}
	} else if (strcmp(key, PARM_NAME_KEY2) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_KEY2, value);

		if (is_valid_key_string(value)) {
			memset(lp->DefaultKeys.key[1].key, 0, MAX_KEY_SIZE);

			key_string2key(value, &lp->DefaultKeys.key[1]);
		} else {
			 DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_KEY2);
		}
	} else if (strcmp(key, PARM_NAME_KEY3) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_KEY3, value);

		if (is_valid_key_string(value)) {
			memset(lp->DefaultKeys.key[2].key, 0, MAX_KEY_SIZE);

			key_string2key(value, &lp->DefaultKeys.key[2]);
		} else {
			 DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_KEY3);
		}
	} else if (strcmp(key, PARM_NAME_KEY4) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_KEY4, value);

		if (is_valid_key_string(value)) {
			memset(lp->DefaultKeys.key[3].key, 0, MAX_KEY_SIZE);

			key_string2key(value, &lp->DefaultKeys.key[3]);
		} else {
			 DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_KEY4);
		}
	}
	/* New Parameters for WARP */
	else if (strcmp(key, PARM_NAME_LOAD_BALANCING) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_LOAD_BALANCING, value);
		lp->loadBalancing = parse_yes_no(value);
	} else if (strcmp(key, PARM_NAME_MEDIUM_DISTRIBUTION) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_MEDIUM_DISTRIBUTION, value);
		lp->mediumDistribution = parse_yes_no(value);
	} else if (strcmp(key, PARM_NAME_MICROWAVE_ROBUSTNESS) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_MICROWAVE_ROBUSTNESS, value);
		lp->MicrowaveRobustness = parse_yes_no(value);
	} else if (strcmp(key, PARM_NAME_MULTICAST_RATE) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_MULTICAST_RATE, value);

		value_convert = simple_strtoul(value, NULL, 0);

		if ((value_convert >= PARM_MIN_MULTICAST_RATE) && (value_convert <= PARM_MAX_MULTICAST_RATE))
			lp->MulticastRate[0] = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_MULTICAST_RATE);
	} else if (strcmp(key, PARM_NAME_OWN_CHANNEL) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_OWN_CHANNEL, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if (wl_is_a_valid_chan(value_convert)) {
			if (value_convert > 14)
				value_convert = value_convert | 0x100;
			lp->Channel = value_convert;
		} else {
			DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_OWN_CHANNEL);
		}
	} else if (strcmp(key, PARM_NAME_OWN_NAME) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_OWN_NAME, value);

		memset(lp->StationName, 0, (PARM_MAX_NAME_LEN + 1));

		/* Make sure the value isn't too long */
		string_length = strlen(value);
		if (string_length > PARM_MAX_NAME_LEN) {
			DBG_WARNING(DbgInfo, "%s too long; will be truncated\n", PARM_NAME_OWN_NAME);
			string_length = PARM_MAX_NAME_LEN;
		}

		memcpy(lp->StationName, value, string_length);
	} else if (strcmp(key, PARM_NAME_RTS_THRESHOLD) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_RTS_THRESHOLD, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_RTS_THRESHOLD) && (value_convert <= PARM_MAX_RTS_THRESHOLD))
			lp->RTSThreshold = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_RTS_THRESHOLD);
	} else if (strcmp(key, PARM_NAME_SRSC_2GHZ) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_SRSC_2GHZ, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_SRSC) || (value_convert <= PARM_MAX_SRSC))
			lp->srsc[0] = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invaid; will be ignored\n", PARM_NAME_SRSC_2GHZ);
	} else if (strcmp(key, PARM_NAME_SRSC_5GHZ) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_SRSC_5GHZ, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_SRSC) || (value_convert <= PARM_MAX_SRSC))
			lp->srsc[1] = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invaid; will be ignored\n", PARM_NAME_SRSC_5GHZ);
	} else if (strcmp(key, PARM_NAME_SYSTEM_SCALE) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_SYSTEM_SCALE, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_SYSTEM_SCALE) && (value_convert <= PARM_MAX_SYSTEM_SCALE))
			lp->DistanceBetweenAPs = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_SYSTEM_SCALE);
	} else if (strcmp(key, PARM_NAME_TX_KEY) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_TX_KEY, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_TX_KEY) && (value_convert <= PARM_MAX_TX_KEY))
			lp->TransmitKeyID = simple_strtoul(value, NULL, 0);
		else
			DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_TX_KEY);
	} else if (strcmp(key, PARM_NAME_TX_RATE) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_TX_RATE, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_TX_RATE) && (value_convert <= PARM_MAX_TX_RATE))
			lp->TxRateControl[0] = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_TX_RATE);
	} else if (strcmp(key, PARM_NAME_TX_POW_LEVEL) == 0) {
		DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_TX_POW_LEVEL, value);

		value_convert = simple_strtoul(value, NULL, 0);
		if ((value_convert >= PARM_MIN_TX_POW_LEVEL) || (value_convert <= PARM_MAX_TX_POW_LEVEL))
			lp->txPowLevel = value_convert;
		else
			DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_TX_POW_LEVEL);
	}

	/* Need to add? : Country code, Short/Long retry */

	/* Configuration parameters specific to STA mode */
#if 1 /* ;? (HCF_TYPE) & HCF_TYPE_STA */
/* ;?seems reasonable that even an AP-only driver could afford this small additional footprint */
	if (CNV_INT_TO_LITTLE(lp->hcfCtx.IFB_FWIdentity.comp_id) == COMP_ID_FW_STA) {
					/* ;?should we return an error status in AP mode */
		if (strcmp(key, PARM_NAME_PORT_TYPE) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_PORT_TYPE, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert == PARM_MIN_PORT_TYPE) || (value_convert == PARM_MAX_PORT_TYPE))
				lp->PortType = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_PORT_TYPE);
		} else if (strcmp(key, PARM_NAME_PM_ENABLED) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_PM_ENABLED, value);
			value_convert = simple_strtoul(value, NULL, 0);
	/* ;? how about wl_main.c containing
	 * VALID_PARAM(PARM_PM_ENABLED <= WVLAN_PM_STATE_STANDARD ||
	 *					 (PARM_PM_ENABLED & 0x7FFF) <= WVLAN_PM_STATE_STANDARD);
	 */
			if ((value_convert & 0x7FFF) <= PARM_MAX_PM_ENABLED) {
				lp->PMEnabled = value_convert;
			} else {
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_PM_ENABLED);
				/* ;?this is a data entry error, hence not a DBG_WARNING */
			}
		} else if (strcmp(key, PARM_NAME_CREATE_IBSS) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_CREATE_IBSS, value);
			lp->CreateIBSS = parse_yes_no(value);
		} else if (strcmp(key, PARM_NAME_MULTICAST_RX) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_MULTICAST_RX, value);
			lp->MulticastReceive = parse_yes_no(value);
		} else if (strcmp(key, PARM_NAME_MAX_SLEEP) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_MAX_SLEEP, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= 0) && (value_convert <= 65535))
				lp->MaxSleepDuration = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_MAX_SLEEP);
		} else if (strcmp(key, PARM_NAME_NETWORK_ADDR) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_NETWORK_ADDR, value);

			if (parse_mac_address(value, mac_value) == ETH_ALEN)
				memcpy(lp->MACAddress, mac_value, ETH_ALEN);
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_NETWORK_ADDR);
		} else if (strcmp(key, PARM_NAME_AUTHENTICATION) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_AUTHENTICATION, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_AUTHENTICATION) && (value_convert <= PARM_MAX_AUTHENTICATION))
				lp->authentication = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_AUTHENTICATION);
		} else if (strcmp(key, PARM_NAME_OWN_ATIM_WINDOW) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_OWN_ATIM_WINDOW, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_OWN_ATIM_WINDOW) && (value_convert <= PARM_MAX_OWN_ATIM_WINDOW))
				lp->atimWindow = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_OWN_ATIM_WINDOW);
		} else if (strcmp(key, PARM_NAME_PM_HOLDOVER_DURATION) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_PM_HOLDOVER_DURATION, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_PM_HOLDOVER_DURATION) && (value_convert <= PARM_MAX_PM_HOLDOVER_DURATION))
				lp->holdoverDuration = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_PM_HOLDOVER_DURATION);
		} else if (strcmp(key, PARM_NAME_PROMISCUOUS_MODE) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_PROMISCUOUS_MODE, value);
			lp->promiscuousMode = parse_yes_no(value);
		} else if (strcmp(key, PARM_NAME_CONNECTION_CONTROL) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_CONNECTION_CONTROL, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_CONNECTION_CONTROL) && (value_convert <= PARM_MAX_CONNECTION_CONTROL))
				lp->connectionControl = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_CONNECTION_CONTROL);
		}

		/* Need to add? : Probe Data Rate */
	}
#endif  /* (HCF_TYPE) & HCF_TYPE_STA */

	/* Configuration parameters specific to AP mode */
#if 1 /* ;? (HCF_TYPE) & HCF_TYPE_AP */
		/* ;?should we restore this to allow smaller memory footprint */
	if (CNV_INT_TO_LITTLE(lp->hcfCtx.IFB_FWIdentity.comp_id) == COMP_ID_FW_AP) {
		if (strcmp(key, PARM_NAME_OWN_DTIM_PERIOD) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_OWN_DTIM_PERIOD, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if (value_convert >= PARM_MIN_OWN_DTIM_PERIOD)
				lp->DTIMPeriod = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_OWN_DTIM_PERIOD);
		} else if (strcmp(key, PARM_NAME_REJECT_ANY) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_REJECT_ANY, value);
			lp->RejectAny = parse_yes_no(value);
		} else if (strcmp(key, PARM_NAME_EXCLUDE_UNENCRYPTED) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_EXCLUDE_UNENCRYPTED, value);
			lp->ExcludeUnencrypted = parse_yes_no(value);
		} else if (strcmp(key, PARM_NAME_MULTICAST_PM_BUFFERING) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_MULTICAST_PM_BUFFERING, value);
			lp->ExcludeUnencrypted = parse_yes_no(value);
		} else if (strcmp(key, PARM_NAME_INTRA_BSS_RELAY) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_INTRA_BSS_RELAY, value);
			lp->ExcludeUnencrypted = parse_yes_no(value);
		} else if (strcmp(key, PARM_NAME_OWN_BEACON_INTERVAL) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_OWN_BEACON_INTERVAL, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if (value_convert >= PARM_MIN_OWN_BEACON_INTERVAL)
				lp->ownBeaconInterval = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_OWN_BEACON_INTERVAL);
		} else if (strcmp(key, PARM_NAME_COEXISTENCE) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_COEXISTENCE, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if (value_convert >= PARM_MIN_COEXISTENCE)
				lp->coexistence = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_COEXISTENCE);
		}

#ifdef USE_WDS
		else if (strcmp(key, PARM_NAME_RTS_THRESHOLD1) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_RTS_THRESHOLD1, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_RTS_THRESHOLD) && (value_convert <= PARM_MAX_RTS_THRESHOLD))
				lp->wds_port[0].rtsThreshold = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_RTS_THRESHOLD1);
		} else if (strcmp(key, PARM_NAME_RTS_THRESHOLD2) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_RTS_THRESHOLD2, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_RTS_THRESHOLD) && (value_convert <= PARM_MAX_RTS_THRESHOLD))
				lp->wds_port[1].rtsThreshold = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_RTS_THRESHOLD2);
		} else if (strcmp(key, PARM_NAME_RTS_THRESHOLD3) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_RTS_THRESHOLD3, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_RTS_THRESHOLD) && (value_convert <= PARM_MAX_RTS_THRESHOLD))
				lp->wds_port[2].rtsThreshold = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_RTS_THRESHOLD3);
		} else if (strcmp(key, PARM_NAME_RTS_THRESHOLD4) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_RTS_THRESHOLD4, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_RTS_THRESHOLD) && (value_convert <= PARM_MAX_RTS_THRESHOLD))
				lp->wds_port[3].rtsThreshold = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_RTS_THRESHOLD4);
		} else if (strcmp(key, PARM_NAME_RTS_THRESHOLD5) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_RTS_THRESHOLD5, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_RTS_THRESHOLD) && (value_convert <= PARM_MAX_RTS_THRESHOLD))
				lp->wds_port[4].rtsThreshold = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_RTS_THRESHOLD5);
		} else if (strcmp(key, PARM_NAME_RTS_THRESHOLD6) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_RTS_THRESHOLD6, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_RTS_THRESHOLD) && (value_convert <= PARM_MAX_RTS_THRESHOLD))
				lp->wds_port[5].rtsThreshold = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_RTS_THRESHOLD6);
		} else if (strcmp(key, PARM_NAME_TX_RATE1) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_TX_RATE1, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_TX_RATE) && (value_convert <= PARM_MAX_TX_RATE))
				lp->wds_port[0].txRateCntl = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_TX_RATE1);
		} else if (strcmp(key, PARM_NAME_TX_RATE2) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_TX_RATE2, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_TX_RATE) && (value_convert <= PARM_MAX_TX_RATE))
				lp->wds_port[1].txRateCntl = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_TX_RATE2);
		} else if (strcmp(key, PARM_NAME_TX_RATE3) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_TX_RATE3, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_TX_RATE) && (value_convert <= PARM_MAX_TX_RATE))
				lp->wds_port[2].txRateCntl = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_TX_RATE3);
		} else if (strcmp(key, PARM_NAME_TX_RATE4) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_TX_RATE4, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_TX_RATE) && (value_convert <= PARM_MAX_TX_RATE))
				lp->wds_port[3].txRateCntl = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_TX_RATE4);
		} else if (strcmp(key, PARM_NAME_TX_RATE5) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_TX_RATE5, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_TX_RATE) && (value_convert <= PARM_MAX_TX_RATE))
				lp->wds_port[4].txRateCntl = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_TX_RATE5);
		} else if (strcmp(key, PARM_NAME_TX_RATE6) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_TX_RATE6, value);

			value_convert = simple_strtoul(value, NULL, 0);
			if ((value_convert >= PARM_MIN_TX_RATE) && (value_convert <= PARM_MAX_TX_RATE))
				lp->wds_port[5].txRateCntl = value_convert;
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_TX_RATE6);
		} else if (strcmp(key, PARM_NAME_WDS_ADDRESS1) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_WDS_ADDRESS1, value);

			if (parse_mac_address(value, mac_value) == ETH_ALEN)
				memcpy(lp->wds_port[0].wdsAddress, mac_value, ETH_ALEN);
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_WDS_ADDRESS1);
		} else if (strcmp(key, PARM_NAME_WDS_ADDRESS2) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_WDS_ADDRESS2, value);

			if (parse_mac_address(value, mac_value) == ETH_ALEN)
				memcpy(lp->wds_port[1].wdsAddress, mac_value, ETH_ALEN);
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_WDS_ADDRESS2);
		} else if (strcmp(key, PARM_NAME_WDS_ADDRESS3) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_WDS_ADDRESS3, value);

			if (parse_mac_address(value, mac_value) == ETH_ALEN)
				memcpy(lp->wds_port[2].wdsAddress, mac_value, ETH_ALEN);
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_WDS_ADDRESS3);
		} else if (strcmp(key, PARM_NAME_WDS_ADDRESS4) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_WDS_ADDRESS4, value);

			if (parse_mac_address(value, mac_value) == ETH_ALEN)
				memcpy(lp->wds_port[3].wdsAddress, mac_value, ETH_ALEN);
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_WDS_ADDRESS4);
		} else if (strcmp(key, PARM_NAME_WDS_ADDRESS5) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_WDS_ADDRESS5, value);

			if (parse_mac_address(value, mac_value) == ETH_ALEN)
				memcpy(lp->wds_port[4].wdsAddress, mac_value, ETH_ALEN);
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_WDS_ADDRESS5);
		} else if (strcmp(key, PARM_NAME_WDS_ADDRESS6) == 0) {
			DBG_TRACE(DbgInfo, "%s, value: %s\n", PARM_NAME_WDS_ADDRESS6, value);

			if (parse_mac_address(value, mac_value) == ETH_ALEN)
				memcpy(lp->wds_port[5].wdsAddress, mac_value, ETH_ALEN);
			else
				DBG_WARNING(DbgInfo, "%s invalid; will be ignored\n", PARM_NAME_WDS_ADDRESS6);
		}
#endif  /* USE_WDS */
	}
#endif  /* (HCF_TYPE) & HCF_TYPE_AP */

	return;
} /* translate_option */
/*============================================================================*/

/*******************************************************************************
 *	parse_mac_address()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This function will parse a mac address string and convert it to a byte
 *   array.
 *
 *  PARAMETERS:
 *
 *      value       - the MAC address, represented as a string
 *      byte_array  - the MAC address, represented as a byte array of length
 *                    ETH_ALEN
 *
 *  RETURNS:
 *
 *      The number of bytes in the final MAC address, should equal to ETH_ALEN.
 *
 ******************************************************************************/
int parse_mac_address(char *value, u_char *byte_array)
{
	int     value_offset = 0;
	int     array_offset = 0;
	int     field_offset = 0;
	char    byte_field[3];
	/*------------------------------------------------------------------------*/

	memset(byte_field, '\0', 3);

	while (value[value_offset] != '\0') {
		/* Skip over the colon chars seperating the bytes, if they exist */
		if (value[value_offset] == ':') {
			value_offset++;
			continue;
		}

		byte_field[field_offset] = value[value_offset];
		field_offset++;
		value_offset++;

		/* Once the byte_field is filled, convert it and store it */
		if (field_offset == 2) {
			byte_field[field_offset] = '\0';
			byte_array[array_offset] = simple_strtoul(byte_field, NULL, 16);
			field_offset = 0;
			array_offset++;
		}
	}

	/* Use the array_offset as a check; 6 bytes should be written to the
	   byte_array */
	return array_offset;
} /* parse_mac_address */
/*============================================================================*/

/*******************************************************************************
 *	ParseConfigLine()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Parses a line from the configuration file into an L-val and an R-val,
 *  representing a key/value pair.
 *
 *  PARAMETERS:
 *
 *      pszLine     - the line from the config file to parse
 *      ppszLVal    - the resulting L-val (Key)
 *      ppszRVal    - the resulting R-val (Value)
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void ParseConfigLine(char *pszLine, char **ppszLVal, char **ppszRVal)
{
	int i;
	int size;
	/*------------------------------------------------------------------------*/

	DBG_FUNC("ParseConfigLine");
	DBG_ENTER(DbgInfo);

	/* get a snapshot of our string size */
	size      = strlen(pszLine);
	*ppszLVal = NULL;
	*ppszRVal = NULL;

	if (pszLine[0] != '#' &&							/* skip the line if it is a comment */
		 pszLine[0] != '\n' &&							/* if it's an empty UNIX line, do nothing */
		 !(pszLine[0] == '\r' && pszLine[1] == '\n')	/* if it's an empty MS-DOS line, do nothing */
	   ) {
		/* advance past any whitespace, and assign the L-value */
		for (i = 0; i < size; i++) {
			if (pszLine[i] != ' ') {
				*ppszLVal = &pszLine[i];
				break;
			}
		}
		/* advance to the end of the l-value*/
		for (i++; i < size; i++) {
			if (pszLine[i] == ' ' || pszLine[i] == '=') {
				pszLine[i] = '\0';
				break;
			}
		}
		/* make any whitespace and the equal sign a NULL character, and
		   advance to the R-Value */
		for (i++; i < size; i++) {
			if (pszLine[i] == ' ' || pszLine[i] == '=') {
				pszLine[i] = '\0';
				continue;
			}
			*ppszRVal = &pszLine[i];
			break;
		}
		/* make the line ending character(s) a NULL */
		for (i++; i < size; i++) {
			if (pszLine[i] == '\n')
				pszLine[i] = '\0';
			if ((pszLine[i] == '\r') && (pszLine[i+1] == '\n'))
				pszLine[i] = '\0';
		}
	}
	DBG_LEAVE(DbgInfo);
} /* ParseConfigLine */
/*============================================================================*/

#endif  /* USE_PROFILE */
