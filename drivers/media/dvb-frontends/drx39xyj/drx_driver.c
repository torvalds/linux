/*
  Generic DRX functionality, DRX driver core.

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

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__


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

#define DRX_ISPOWERDOWNMODE(mode) ((mode == DRX_POWER_MODE_9) || \
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
struct drxu_code_block_hdr {
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
			set. */};

/*------------------------------------------------------------------------------
FUNCTIONS
------------------------------------------------------------------------------*/

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
			if (carry != 0)
				crc_word ^= 0x80050000UL;
			carry = crc_word & 0x80000000UL;
		}
		i++;
		block_data += (sizeof(u16));
	}
	return (u16)(crc_word >> 16);
}

/*============================================================================*/


static int check_firmware(struct drx_demod_instance *demod, u8 *mc_data,
			  unsigned size)
{
	struct drxu_code_block_hdr block_hdr;
	int i;
	unsigned count = 2 * sizeof(u16);
	u32 mc_dev_type, mc_version, mc_base_version;
	u16 mc_nr_of_blks = u_code_read16(mc_data + sizeof(u16));

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
		block_hdr.addr = u_code_read32(mc_data + count);
		count += sizeof(u32);
		block_hdr.size = u_code_read16(mc_data + count);
		count += sizeof(u16);
		block_hdr.flags = u_code_read16(mc_data + count);
		count += sizeof(u16);
		block_hdr.CRC = u_code_read16(mc_data + count);
		count += sizeof(u16);

		pr_debug("%u: addr %u, size %u, flags 0x%04x, CRC 0x%04x\n",
			count, block_hdr.addr, block_hdr.size, block_hdr.flags,
			block_hdr.CRC);

		if (block_hdr.flags & 0x8) {
			u8 *auxblk = ((void *)mc_data) + block_hdr.addr;
			u16 auxtype;

			if (block_hdr.addr + sizeof(u16) > size)
				goto eof;

			auxtype = u_code_read16(auxblk);

			/* Aux block. Check type */
			if (DRX_ISMCVERTYPE(auxtype)) {
				if (block_hdr.addr + 2 * sizeof(u16) + 2 * sizeof (u32) > size)
					goto eof;

				auxblk += sizeof(u16);
				mc_dev_type = u_code_read32(auxblk);
				auxblk += sizeof(u32);
				mc_version = u_code_read32(auxblk);
				auxblk += sizeof(u32);
				mc_base_version = u_code_read32(auxblk);

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
* \brief Handle microcode upload or verify.
* \param dev_addr: Address of device.
* \param mc_info:  Pointer to information about microcode data.
* \param action:  Either UCODE_UPLOAD or UCODE_VERIFY
* \return int.
* \retval 0:
*                    - In case of UCODE_UPLOAD: code is successfully uploaded.
*                    - In case of UCODE_VERIFY: image on device is equal to
*                      image provided to this control function.
* \retval -EIO:
*                    - In case of UCODE_UPLOAD: I2C error.
*                    - In case of UCODE_VERIFY: I2C error or image on device
*                      is not equal to image provided to this control function.
* \retval -EINVAL:
*                    - Invalid arguments.
*                    - Provided image is corrupt
*/
static int
ctrl_u_code(struct drx_demod_instance *demod,
	    struct drxu_code_info *mc_info, enum drxu_code_action action)
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
	mc_magic_word = u_code_read16(mc_data);
	mc_data += sizeof(u16);
	mc_nr_of_blks = u_code_read16(mc_data);
	mc_data += sizeof(u16);

	if ((mc_magic_word != DRX_UCODE_MAGIC_WORD) || (mc_nr_of_blks == 0)) {
		rc = -EINVAL;
		pr_err("Firmware magic word doesn't match\n");
		goto release;
	}

	if (action == UCODE_UPLOAD) {
		rc = check_firmware(demod, (u8 *)mc_data_init, size);
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
		block_hdr.addr = u_code_read32(mc_data);
		mc_data += sizeof(u32);
		block_hdr.size = u_code_read16(mc_data);
		mc_data += sizeof(u16);
		block_hdr.flags = u_code_read16(mc_data);
		mc_data += sizeof(u16);
		block_hdr.CRC = u_code_read16(mc_data);
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
		     (block_hdr.CRC != u_code_compute_crc(mc_data, block_hdr.size)))
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
* \brief Build list of version information.
* \param demod: A pointer to a demodulator instance.
* \param version_list: Pointer to linked list of versions.
* \return int.
* \retval 0:          Version information stored in version_list
* \retval -EINVAL: Invalid arguments.
*/
static int
ctrl_version(struct drx_demod_instance *demod, struct drx_version_list **version_list)
{
	static char drx_driver_core_module_name[] = "Core driver";
	static char drx_driver_core_version_text[] =
	    DRX_VERSIONSTRING(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);

	static struct drx_version drx_driver_core_version;
	static struct drx_version_list drx_driver_core_version_list;

	struct drx_version_list *demod_version_list = (struct drx_version_list *) (NULL);
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

/*============================================================================*/
/*============================================================================*/
/*== Exported functions ======================================================*/
/*============================================================================*/
/*============================================================================*/

/**
* \brief Open a demodulator instance.
* \param demod: A pointer to a demodulator instance.
* \return int Return status.
* \retval 0:          Opened demod instance with succes.
* \retval -EIO:       Driver not initialized or unable to initialize
*                              demod.
* \retval -EINVAL: Demod instance has invalid content.
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
* \brief Close device.
* \param demod: A pointer to a demodulator instance.
* \return int Return status.
* \retval 0:          Closed demod instance with succes.
* \retval -EIO:       Driver not initialized or error during close
*                              demod.
* \retval -EINVAL: Demod instance has invalid content.
*
* Free resources occupied by device instance.
* Put device into sleep mode.
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

/*============================================================================*/

/**
* \brief Control the device.
* \param demod:    A pointer to a demodulator instance.
* \param ctrl:     Reference to desired control function.
* \param ctrl_data: Pointer to data structure for control function.
* \return int Return status.
* \retval 0:                 Control function completed successfully.
* \retval -EIO:              Driver not initialized or error during
*                                     control demod.
* \retval -EINVAL:        Demod instance or ctrl_data has invalid
*                                     content.
* \retval -ENOTSUPP: Specified control function is not
*                                     available.
*
* Data needed or returned by the control function is stored in ctrl_data.
*
*/

int
drx_ctrl(struct drx_demod_instance *demod, u32 ctrl, void *ctrl_data)
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
		return ctrl_version(demod, (struct drx_version_list **)ctrl_data);
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
			return ctrl_u_code(demod,
					 (struct drxu_code_info *)ctrl_data,
					 UCODE_UPLOAD);
			break;

	 /*===================================================================*/
		case DRX_CTRL_VERIFY_UCODE:
			{
				return ctrl_u_code(demod,
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

/*============================================================================*/

/* END OF FILE */
