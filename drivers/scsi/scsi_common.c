/*
 * SCSI functions used by both the initiator and the target code.
 */

#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <scsi/scsi_common.h>

/* NB: These are exposed through /proc/scsi/scsi and form part of the ABI.
 * You may not alter any existing entry (although adding new ones is
 * encouraged once assigned by ANSI/INCITS T10
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
 * scsi_device_type - Return 17 char string indicating device type.
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
 *     Convert @scsilun from a struct scsi_lun to a four byte host byte-ordered
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
 *     Given an integer : 0x0b03d204,  this function returns a
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
	if (!sense_buffer || !sb_len)
		return false;

	memset(sshdr, 0, sizeof(struct scsi_sense_hdr));

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
