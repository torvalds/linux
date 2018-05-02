// SPDX-License-Identifier: GPL-2.0
/*
 * SCSI functions used by both the initiator and the target code.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <asm/unaligned.h>
#include <scsi/scsi_common.h>

/* NB: These are exposed through /proc/scsi/scsi and form part of the ABI.
 * You may not alter any existing entry (although adding new ones is
 * encouraged once assigned by ANSI/INCITS T10).
 */
static const char *const scsi_device_types[] = {
	"Direct-Access    ",
	"Sequential-Access",
	"Printer          ",
	"Processor        ",
	"WORM             ",
	"CD-ROM           ",
	"Scanner          ",
	"Optical Device   ",
	"Medium Changer   ",
	"Communications   ",
	"ASC IT8          ",
	"ASC IT8          ",
	"RAID             ",
	"Enclosure        ",
	"Direct-Access-RBC",
	"Optical card     ",
	"Bridge controller",
	"Object storage   ",
	"Automation/Drive ",
	"Security Manager ",
	"Direct-Access-ZBC",
};

/**
 * scsi_device_type - Return 17-char string indicating device type.
 * @type: type number to look up
 */
const char *scsi_device_type(unsigned type)
{
	if (type == 0x1e)
		return "Well-known LUN   ";
	if (type == 0x1f)
		return "No Device        ";
	if (type >= ARRAY_SIZE(scsi_device_types))
		return "Unknown          ";
	return scsi_device_types[type];
}
EXPORT_SYMBOL(scsi_device_type);

/**
 * scsilun_to_int - convert a scsi_lun to an int
 * @scsilun:	struct scsi_lun to be converted.
 *
 * Description:
 *     Convert @scsilun from a struct scsi_lun to a four-byte host byte-ordered
 *     integer, and return the result. The caller must check for
 *     truncation before using this function.
 *
 * Notes:
 *     For a description of the LUN format, post SCSI-3 see the SCSI
 *     Architecture Model, for SCSI-3 see the SCSI Controller Commands.
 *
 *     Given a struct scsi_lun of: d2 04 0b 03 00 00 00 00, this function
 *     returns the integer: 0x0b03d204
 *
 *     This encoding will return a standard integer LUN for LUNs smaller
 *     than 256, which typically use a single level LUN structure with
 *     addressing method 0.
 */
u64 scsilun_to_int(struct scsi_lun *scsilun)
{
	int i;
	u64 lun;

	lun = 0;
	for (i = 0; i < sizeof(lun); i += 2)
		lun = lun | (((u64)scsilun->scsi_lun[i] << ((i + 1) * 8)) |
			     ((u64)scsilun->scsi_lun[i + 1] << (i * 8)));
	return lun;
}
EXPORT_SYMBOL(scsilun_to_int);

/**
 * int_to_scsilun - reverts an int into a scsi_lun
 * @lun:        integer to be reverted
 * @scsilun:	struct scsi_lun to be set.
 *
 * Description:
 *     Reverts the functionality of the scsilun_to_int, which packed
 *     an 8-byte lun value into an int. This routine unpacks the int
 *     back into the lun value.
 *
 * Notes:
 *     Given an integer : 0x0b03d204, this function returns a
 *     struct scsi_lun of: d2 04 0b 03 00 00 00 00
 *
 */
void int_to_scsilun(u64 lun, struct scsi_lun *scsilun)
{
	int i;

	memset(scsilun->scsi_lun, 0, sizeof(scsilun->scsi_lun));

	for (i = 0; i < sizeof(lun); i += 2) {
		scsilun->scsi_lun[i] = (lun >> 8) & 0xFF;
		scsilun->scsi_lun[i+1] = lun & 0xFF;
		lun = lun >> 16;
	}
}
EXPORT_SYMBOL(int_to_scsilun);

/**
 * scsi_normalize_sense - normalize main elements from either fixed or
 *			descriptor sense data format into a common format.
 *
 * @sense_buffer:	byte array containing sense data returned by device
 * @sb_len:		number of valid bytes in sense_buffer
 * @sshdr:		pointer to instance of structure that common
 *			elements are written to.
 *
 * Notes:
 *	The "main elements" from sense data are: response_code, sense_key,
 *	asc, ascq and additional_length (only for descriptor format).
 *
 *	Typically this function can be called after a device has
 *	responded to a SCSI command with the CHECK_CONDITION status.
 *
 * Return value:
 *	true if valid sense data information found, else false;
 */
bool scsi_normalize_sense(const u8 *sense_buffer, int sb_len,
			  struct scsi_sense_hdr *sshdr)
{
	memset(sshdr, 0, sizeof(struct scsi_sense_hdr));

	if (!sense_buffer || !sb_len)
		return false;

	sshdr->response_code = (sense_buffer[0] & 0x7f);

	if (!scsi_sense_valid(sshdr))
		return false;

	if (sshdr->response_code >= 0x72) {
		/*
		 * descriptor format
		 */
		if (sb_len > 1)
			sshdr->sense_key = (sense_buffer[1] & 0xf);
		if (sb_len > 2)
			sshdr->asc = sense_buffer[2];
		if (sb_len > 3)
			sshdr->ascq = sense_buffer[3];
		if (sb_len > 7)
			sshdr->additional_length = sense_buffer[7];
	} else {
		/*
		 * fixed format
		 */
		if (sb_len > 2)
			sshdr->sense_key = (sense_buffer[2] & 0xf);
		if (sb_len > 7) {
			sb_len = (sb_len < (sense_buffer[7] + 8)) ?
					 sb_len : (sense_buffer[7] + 8);
			if (sb_len > 12)
				sshdr->asc = sense_buffer[12];
			if (sb_len > 13)
				sshdr->ascq = sense_buffer[13];
		}
	}

	return true;
}
EXPORT_SYMBOL(scsi_normalize_sense);

/**
 * scsi_sense_desc_find - search for a given descriptor type in	descriptor sense data format.
 * @sense_buffer:	byte array of descriptor format sense data
 * @sb_len:		number of valid bytes in sense_buffer
 * @desc_type:		value of descriptor type to find
 *			(e.g. 0 -> information)
 *
 * Notes:
 *	only valid when sense data is in descriptor format
 *
 * Return value:
 *	pointer to start of (first) descriptor if found else NULL
 */
const u8 * scsi_sense_desc_find(const u8 * sense_buffer, int sb_len,
				int desc_type)
{
	int add_sen_len, add_len, desc_len, k;
	const u8 * descp;

	if ((sb_len < 8) || (0 == (add_sen_len = sense_buffer[7])))
		return NULL;
	if ((sense_buffer[0] < 0x72) || (sense_buffer[0] > 0x73))
		return NULL;
	add_sen_len = (add_sen_len < (sb_len - 8)) ?
			add_sen_len : (sb_len - 8);
	descp = &sense_buffer[8];
	for (desc_len = 0, k = 0; k < add_sen_len; k += desc_len) {
		descp += desc_len;
		add_len = (k < (add_sen_len - 1)) ? descp[1]: -1;
		desc_len = add_len + 2;
		if (descp[0] == desc_type)
			return descp;
		if (add_len < 0) // short descriptor ??
			break;
	}
	return NULL;
}
EXPORT_SYMBOL(scsi_sense_desc_find);

/**
 * scsi_build_sense_buffer - build sense data in a buffer
 * @desc:	Sense format (non-zero == descriptor format,
 *              0 == fixed format)
 * @buf:	Where to build sense data
 * @key:	Sense key
 * @asc:	Additional sense code
 * @ascq:	Additional sense code qualifier
 *
 **/
void scsi_build_sense_buffer(int desc, u8 *buf, u8 key, u8 asc, u8 ascq)
{
	if (desc) {
		buf[0] = 0x72;	/* descriptor, current */
		buf[1] = key;
		buf[2] = asc;
		buf[3] = ascq;
		buf[7] = 0;
	} else {
		buf[0] = 0x70;	/* fixed, current */
		buf[2] = key;
		buf[7] = 0xa;
		buf[12] = asc;
		buf[13] = ascq;
	}
}
EXPORT_SYMBOL(scsi_build_sense_buffer);

/**
 * scsi_set_sense_information - set the information field in a
 *		formatted sense data buffer
 * @buf:	Where to build sense data
 * @buf_len:    buffer length
 * @info:	64-bit information value to be set
 *
 * Return value:
 *	0 on success or -EINVAL for invalid sense buffer length
 **/
int scsi_set_sense_information(u8 *buf, int buf_len, u64 info)
{
	if ((buf[0] & 0x7f) == 0x72) {
		u8 *ucp, len;

		len = buf[7];
		ucp = (char *)scsi_sense_desc_find(buf, len + 8, 0);
		if (!ucp) {
			buf[7] = len + 0xc;
			ucp = buf + 8 + len;
		}

		if (buf_len < len + 0xc)
			/* Not enough room for info */
			return -EINVAL;

		ucp[0] = 0;
		ucp[1] = 0xa;
		ucp[2] = 0x80; /* Valid bit */
		ucp[3] = 0;
		put_unaligned_be64(info, &ucp[4]);
	} else if ((buf[0] & 0x7f) == 0x70) {
		/*
		 * Only set the 'VALID' bit if we can represent the value
		 * correctly; otherwise just fill out the lower bytes and
		 * clear the 'VALID' flag.
		 */
		if (info <= 0xffffffffUL)
			buf[0] |= 0x80;
		else
			buf[0] &= 0x7f;
		put_unaligned_be32((u32)info, &buf[3]);
	}

	return 0;
}
EXPORT_SYMBOL(scsi_set_sense_information);

/**
 * scsi_set_sense_field_pointer - set the field pointer sense key
 *		specific information in a formatted sense data buffer
 * @buf:	Where to build sense data
 * @buf_len:    buffer length
 * @fp:		field pointer to be set
 * @bp:		bit pointer to be set
 * @cd:		command/data bit
 *
 * Return value:
 *	0 on success or -EINVAL for invalid sense buffer length
 */
int scsi_set_sense_field_pointer(u8 *buf, int buf_len, u16 fp, u8 bp, bool cd)
{
	u8 *ucp, len;

	if ((buf[0] & 0x7f) == 0x72) {
		len = buf[7];
		ucp = (char *)scsi_sense_desc_find(buf, len + 8, 2);
		if (!ucp) {
			buf[7] = len + 8;
			ucp = buf + 8 + len;
		}

		if (buf_len < len + 8)
			/* Not enough room for info */
			return -EINVAL;

		ucp[0] = 2;
		ucp[1] = 6;
		ucp[4] = 0x80; /* Valid bit */
		if (cd)
			ucp[4] |= 0x40;
		if (bp < 0x8)
			ucp[4] |= 0x8 | bp;
		put_unaligned_be16(fp, &ucp[5]);
	} else if ((buf[0] & 0x7f) == 0x70) {
		len = buf[7];
		if (len < 18)
			buf[7] = 18;

		buf[15] = 0x80;
		if (cd)
			buf[15] |= 0x40;
		if (bp < 0x8)
			buf[15] |= 0x8 | bp;
		put_unaligned_be16(fp, &buf[16]);
	}

	return 0;
}
EXPORT_SYMBOL(scsi_set_sense_field_pointer);
