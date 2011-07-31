/*
 * Copyright (c) 2006, 2007, 2008, 2009 QLogic Corporation. All rights reserved.
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
					qib_dev_err(dd, "Can't set %s GUID"
						    " from base, wraps to"
						    " OUI!\n",
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
		qib_dev_err(dd, "Couldn't allocate memory to read %u "
			    "bytes from eeprom for GUID\n", len);
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
		qib_devinfo(dd->pcidev, "Bad I2C flash checksum: "
			 "0x%x, not 0x%x\n", csum, ifp->if_csum);
		goto done;
	}
	if (*(__be64 *) ifp->if_guid == cpu_to_be64(0) ||
	    *(__be64 *) ifp->if_guid == ~cpu_to_be64(0)) {
		qib_dev_err(dd, "Invalid GUID %llx from flash; ignoring\n",
			    *(unsigned long long *) ifp->if_guid);
		/* don't allow GUID if all 0 or all 1's */
		goto done;
	}

	/* complain, but allow it */
	if (*(u64 *) ifp->if_guid == 0x100007511000000ULL)
		qib_devinfo(dd->pcidev, "Warning, GUID %llx is "
			 "default, probably not correct!\n",
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
		qib_dev_err(dd, "Board SN %s did not pass functional "
			    "test: %s\n", dd->serial, ifp->if_comment);

	memcpy(&dd->eep_st_errs, &ifp->if_errcntp, QIB_EEP_LOG_CNT);
	/*
	 * Power-on (actually "active") hours are kept as little-endian value
	 * in EEPROM, but as seconds in a (possibly as small as 24-bit)
	 * atomic_t while running.
	 */
	atomic_set(&dd->active_time, 0);
	dd->eep_hrs = ifp->if_powerhour[0] | (ifp->if_powerhour[1] << 8);

done:
	vfree(buf);

bail:;
}

/**
 * qib_update_eeprom_log - copy active-time and error counters to eeprom
 * @dd: the qlogic_ib device
 *
 * Although the time is kept as seconds in the qib_devdata struct, it is
 * rounded to hours for re-write, as we have only 16 bits in EEPROM.
 * First-cut code reads whole (expected) struct qib_flash, modifies,
 * re-writes. Future direction: read/write only what we need, assuming
 * that the EEPROM had to have been "good enough" for driver init, and
 * if not, we aren't making it worse.
 *
 */
int qib_update_eeprom_log(struct qib_devdata *dd)
{
	void *buf;
	struct qib_flash *ifp;
	int len, hi_water;
	uint32_t new_time, new_hrs;
	u8 csum;
	int ret, idx;
	unsigned long flags;

	/* first, check if we actually need to do anything. */
	ret = 0;
	for (idx = 0; idx < QIB_EEP_LOG_CNT; ++idx) {
		if (dd->eep_st_new_errs[idx]) {
			ret = 1;
			break;
		}
	}
	new_time = atomic_read(&dd->active_time);

	if (ret == 0 && new_time < 3600)
		goto bail;

	/*
	 * The quick-check above determined that there is something worthy
	 * of logging, so get current contents and do a more detailed idea.
	 * read full flash, not just currently used part, since it may have
	 * been written with a newer definition
	 */
	len = sizeof(struct qib_flash);
	buf = vmalloc(len);
	ret = 1;
	if (!buf) {
		qib_dev_err(dd, "Couldn't allocate memory to read %u "
			    "bytes from eeprom for logging\n", len);
		goto bail;
	}

	/* Grab semaphore and read current EEPROM. If we get an
	 * error, let go, but if not, keep it until we finish write.
	 */
	ret = mutex_lock_interruptible(&dd->eep_lock);
	if (ret) {
		qib_dev_err(dd, "Unable to acquire EEPROM for logging\n");
		goto free_bail;
	}
	ret = qib_twsi_blk_rd(dd, dd->twsi_eeprom_dev, 0, buf, len);
	if (ret) {
		mutex_unlock(&dd->eep_lock);
		qib_dev_err(dd, "Unable read EEPROM for logging\n");
		goto free_bail;
	}
	ifp = (struct qib_flash *)buf;

	csum = flash_csum(ifp, 0);
	if (csum != ifp->if_csum) {
		mutex_unlock(&dd->eep_lock);
		qib_dev_err(dd, "EEPROM cks err (0x%02X, S/B 0x%02X)\n",
			    csum, ifp->if_csum);
		ret = 1;
		goto free_bail;
	}
	hi_water = 0;
	spin_lock_irqsave(&dd->eep_st_lock, flags);
	for (idx = 0; idx < QIB_EEP_LOG_CNT; ++idx) {
		int new_val = dd->eep_st_new_errs[idx];
		if (new_val) {
			/*
			 * If we have seen any errors, add to EEPROM values
			 * We need to saturate at 0xFF (255) and we also
			 * would need to adjust the checksum if we were
			 * trying to minimize EEPROM traffic
			 * Note that we add to actual current count in EEPROM,
			 * in case it was altered while we were running.
			 */
			new_val += ifp->if_errcntp[idx];
			if (new_val > 0xFF)
				new_val = 0xFF;
			if (ifp->if_errcntp[idx] != new_val) {
				ifp->if_errcntp[idx] = new_val;
				hi_water = offsetof(struct qib_flash,
						    if_errcntp) + idx;
			}
			/*
			 * update our shadow (used to minimize EEPROM
			 * traffic), to match what we are about to write.
			 */
			dd->eep_st_errs[idx] = new_val;
			dd->eep_st_new_errs[idx] = 0;
		}
	}
	/*
	 * Now update active-time. We would like to round to the nearest hour
	 * but unless atomic_t are sure to be proper signed ints we cannot,
	 * because we need to account for what we "transfer" to EEPROM and
	 * if we log an hour at 31 minutes, then we would need to set
	 * active_time to -29 to accurately count the _next_ hour.
	 */
	if (new_time >= 3600) {
		new_hrs = new_time / 3600;
		atomic_sub((new_hrs * 3600), &dd->active_time);
		new_hrs += dd->eep_hrs;
		if (new_hrs > 0xFFFF)
			new_hrs = 0xFFFF;
		dd->eep_hrs = new_hrs;
		if ((new_hrs & 0xFF) != ifp->if_powerhour[0]) {
			ifp->if_powerhour[0] = new_hrs & 0xFF;
			hi_water = offsetof(struct qib_flash, if_powerhour);
		}
		if ((new_hrs >> 8) != ifp->if_powerhour[1]) {
			ifp->if_powerhour[1] = new_hrs >> 8;
			hi_water = offsetof(struct qib_flash, if_powerhour) + 1;
		}
	}
	/*
	 * There is a tiny possibility that we could somehow fail to write
	 * the EEPROM after updating our shadows, but problems from holding
	 * the spinlock too long are a much bigger issue.
	 */
	spin_unlock_irqrestore(&dd->eep_st_lock, flags);
	if (hi_water) {
		/* we made some change to the data, uopdate cksum and write */
		csum = flash_csum(ifp, 1);
		ret = eeprom_write_with_enable(dd, 0, buf, hi_water + 1);
	}
	mutex_unlock(&dd->eep_lock);
	if (ret)
		qib_dev_err(dd, "Failed updating EEPROM\n");

free_bail:
	vfree(buf);
bail:
	return ret;
}

/**
 * qib_inc_eeprom_err - increment one of the four error counters
 * that are logged to EEPROM.
 * @dd: the qlogic_ib device
 * @eidx: 0..3, the counter to increment
 * @incr: how much to add
 *
 * Each counter is 8-bits, and saturates at 255 (0xFF). They
 * are copied to the EEPROM (aka flash) whenever qib_update_eeprom_log()
 * is called, but it can only be called in a context that allows sleep.
 * This function can be called even at interrupt level.
 */
void qib_inc_eeprom_err(struct qib_devdata *dd, u32 eidx, u32 incr)
{
	uint new_val;
	unsigned long flags;

	spin_lock_irqsave(&dd->eep_st_lock, flags);
	new_val = dd->eep_st_new_errs[eidx] + incr;
	if (new_val > 255)
		new_val = 255;
	dd->eep_st_new_errs[eidx] = new_val;
	spin_unlock_irqrestore(&dd->eep_st_lock, flags);
}
