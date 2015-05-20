/*
 * Copyright (c) 2012 Intel Corporation. All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#include "qib.h"

/*
 * Functions specific to the serial EEPROM on cards handled by ib_qib.
 * The actual serail interface code is in qib_twsi.c. This file is a client
 */

/**
 * qib_eeprom_read - receives bytes from the eeprom via I2C
 * @dd: the qlogic_ib device
 * @eeprom_offset: address to read from
 * @buffer: where to store result
 * @len: number of bytes to receive
 */
int qib_eeprom_read(struct qib_devdata *dd, u8 eeprom_offset,
		    void *buff, int len)
{
	int ret;

	ret = mutex_lock_interruptible(&dd->eep_lock);
	if (!ret) {
		ret = qib_twsi_reset(dd);
		if (ret)
			qib_dev_err(dd, "EEPROM Reset for read failed\n");
		else
			ret = qib_twsi_blk_rd(dd, dd->twsi_eeprom_dev,
					      eeprom_offset, buff, len);
		mutex_unlock(&dd->eep_lock);
	}

	return ret;
}

/*
 * Actually update the eeprom, first doing write enable if
 * needed, then restoring write enable state.
 * Must be called with eep_lock held
 */
static int eeprom_write_with_enable(struct qib_devdata *dd, u8 offset,
		     const void *buf, int len)
{
	int ret, pwen;

	pwen = dd->f_eeprom_wen(dd, 1);
	ret = qib_twsi_reset(dd);
	if (ret)
		qib_dev_err(dd, "EEPROM Reset for write failed\n");
	else
		ret = qib_twsi_blk_wr(dd, dd->twsi_eeprom_dev,
				      offset, buf, len);
	dd->f_eeprom_wen(dd, pwen);
	return ret;
}

/**
 * qib_eeprom_write - writes data to the eeprom via I2C
 * @dd: the qlogic_ib device
 * @eeprom_offset: where to place data
 * @buffer: data to write
 * @len: number of bytes to write
 */
int qib_eeprom_write(struct qib_devdata *dd, u8 eeprom_offset,
		     const void *buff, int len)
{
	int ret;

	ret = mutex_lock_interruptible(&dd->eep_lock);
	if (!ret) {
		ret = eeprom_write_with_enable(dd, eeprom_offset, buff, len);
		mutex_unlock(&dd->eep_lock);
	}

	return ret;
}

static u8 flash_csum(struct qib_flash *ifp, int adjust)
{
	u8 *ip = (u8 *) ifp;
	u8 csum = 0, len;

	/*
	 * Limit length checksummed to max length of actual data.
	 * Checksum of erased eeprom will still be bad, but we avoid
	 * reading past the end of the buffer we were passed.
	 */
	len = ifp->if_length;
	if (len > sizeof(struct qib_flash))
		len = sizeof(struct qib_flash);
	while (len--)
		csum += *ip++;
	csum -= ifp->if_csum;
	csum = ~csum;
	if (adjust)
		ifp->if_csum = csum;

	return csum;
}

/**
 * qib_get_eeprom_info- get the GUID et al. from the TSWI EEPROM device
 * @dd: the qlogic_ib device
 *
 * We have the capability to use the nguid field, and get
 * the guid from the first chip's flash, to use for all of them.
 */
void qib_get_eeprom_info(struct qib_devdata *dd)
{
	void *buf;
	struct qib_flash *ifp;
	__be64 guid;
	int len, eep_stat;
	u8 csum, *bguid;
	int t = dd->unit;
	struct qib_devdata *dd0 = qib_lookup(0);

	if (t && dd0->nguid > 1 && t <= dd0->nguid) {
		u8 oguid;
		dd->base_guid = dd0->base_guid;
		bguid = (u8 *) &dd->base_guid;

		oguid = bguid[7];
		bguid[7] += t;
		if (oguid > bguid[7]) {
			if (bguid[6] == 0xff) {
				if (bguid[5] == 0xff) {
					qib_dev_err(dd,
						"Can't set %s GUID from base, wraps to OUI!\n",
						qib_get_unit_name(t));
					dd->base_guid = 0;
					goto bail;
				}
				bguid[5]++;
			}
			bguid[6]++;
		}
		dd->nguid = 1;
		goto bail;
	}

	/*
	 * Read full flash, not just currently used part, since it may have
	 * been written with a newer definition.
	 * */
	len = sizeof(struct qib_flash);
	buf = vmalloc(len);
	if (!buf) {
		qib_dev_err(dd,
			"Couldn't allocate memory to read %u bytes from eeprom for GUID\n",
			len);
		goto bail;
	}

	/*
	 * Use "public" eeprom read function, which does locking and
	 * figures out device. This will migrate to chip-specific.
	 */
	eep_stat = qib_eeprom_read(dd, 0, buf, len);

	if (eep_stat) {
		qib_dev_err(dd, "Failed reading GUID from eeprom\n");
		goto done;
	}
	ifp = (struct qib_flash *)buf;

	csum = flash_csum(ifp, 0);
	if (csum != ifp->if_csum) {
		qib_devinfo(dd->pcidev,
			"Bad I2C flash checksum: 0x%x, not 0x%x\n",
			csum, ifp->if_csum);
		goto done;
	}
	if (*(__be64 *) ifp->if_guid == cpu_to_be64(0) ||
	    *(__be64 *) ifp->if_guid == ~cpu_to_be64(0)) {
		qib_dev_err(dd,
			"Invalid GUID %llx from flash; ignoring\n",
			*(unsigned long long *) ifp->if_guid);
		/* don't allow GUID if all 0 or all 1's */
		goto done;
	}

	/* complain, but allow it */
	if (*(u64 *) ifp->if_guid == 0x100007511000000ULL)
		qib_devinfo(dd->pcidev,
			"Warning, GUID %llx is default, probably not correct!\n",
			*(unsigned long long *) ifp->if_guid);

	bguid = ifp->if_guid;
	if (!bguid[0] && !bguid[1] && !bguid[2]) {
		/*
		 * Original incorrect GUID format in flash; fix in
		 * core copy, by shifting up 2 octets; don't need to
		 * change top octet, since both it and shifted are 0.
		 */
		bguid[1] = bguid[3];
		bguid[2] = bguid[4];
		bguid[3] = 0;
		bguid[4] = 0;
		guid = *(__be64 *) ifp->if_guid;
	} else
		guid = *(__be64 *) ifp->if_guid;
	dd->base_guid = guid;
	dd->nguid = ifp->if_numguid;
	/*
	 * Things are slightly complicated by the desire to transparently
	 * support both the Pathscale 10-digit serial number and the QLogic
	 * 13-character version.
	 */
	if ((ifp->if_fversion > 1) && ifp->if_sprefix[0] &&
	    ((u8 *) ifp->if_sprefix)[0] != 0xFF) {
		char *snp = dd->serial;

		/*
		 * This board has a Serial-prefix, which is stored
		 * elsewhere for backward-compatibility.
		 */
		memcpy(snp, ifp->if_sprefix, sizeof ifp->if_sprefix);
		snp[sizeof ifp->if_sprefix] = '\0';
		len = strlen(snp);
		snp += len;
		len = (sizeof dd->serial) - len;
		if (len > sizeof ifp->if_serial)
			len = sizeof ifp->if_serial;
		memcpy(snp, ifp->if_serial, len);
	} else
		memcpy(dd->serial, ifp->if_serial,
		       sizeof ifp->if_serial);
	if (!strstr(ifp->if_comment, "Tested successfully"))
		qib_dev_err(dd,
			"Board SN %s did not pass functional test: %s\n",
			dd->serial, ifp->if_comment);

done:
	vfree(buf);

bail:;
}

