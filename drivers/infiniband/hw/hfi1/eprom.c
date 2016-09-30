/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/delay.h>
#include "hfi.h"
#include "common.h"
#include "eprom.h"

/*
 * The EPROM is logically divided into three partitions:
 *	partition 0: the first 128K, visible from PCI ROM BAR
 *	partition 1: 4K config file (sector size)
 *	partition 2: the rest
 */
#define P0_SIZE (128 * 1024)
#define P1_SIZE   (4 * 1024)
#define P1_START P0_SIZE
#define P2_START (P0_SIZE + P1_SIZE)

/* controller page size, in bytes */
#define EP_PAGE_SIZE 256
#define EP_PAGE_MASK (EP_PAGE_SIZE - 1)
#define EP_PAGE_DWORDS (EP_PAGE_SIZE / sizeof(u32))

/* controller commands */
#define CMD_SHIFT 24
#define CMD_NOP			    (0)
#define CMD_READ_DATA(addr)	    ((0x03 << CMD_SHIFT) | addr)
#define CMD_RELEASE_POWERDOWN_NOID  ((0xab << CMD_SHIFT))

/* controller interface speeds */
#define EP_SPEED_FULL 0x2	/* full speed */

/*
 * How long to wait for the EPROM to become available, in ms.
 * The spec 32 Mb EPROM takes around 40s to erase then write.
 * Double it for safety.
 */
#define EPROM_TIMEOUT 80000 /* ms */

/*
 * Read a 256 byte (64 dword) EPROM page.
 * All callers have verified the offset is at a page boundary.
 */
static void read_page(struct hfi1_devdata *dd, u32 offset, u32 *result)
{
	int i;

	write_csr(dd, ASIC_EEP_ADDR_CMD, CMD_READ_DATA(offset));
	for (i = 0; i < EP_PAGE_DWORDS; i++)
		result[i] = (u32)read_csr(dd, ASIC_EEP_DATA);
	write_csr(dd, ASIC_EEP_ADDR_CMD, CMD_NOP); /* close open page */
}

/*
 * Read length bytes starting at offset from the start of the EPROM.
 */
static int read_length(struct hfi1_devdata *dd, u32 start, u32 len, void *dest)
{
	u32 buffer[EP_PAGE_DWORDS];
	u32 end;
	u32 start_offset;
	u32 read_start;
	u32 bytes;

	if (len == 0)
		return 0;

	end = start + len;

	/*
	 * Make sure the read range is not outside of the controller read
	 * command address range.  Note that '>' is correct below - the end
	 * of the range is OK if it stops at the limit, but no higher.
	 */
	if (end > (1 << CMD_SHIFT))
		return -EINVAL;

	/* read the first partial page */
	start_offset = start & EP_PAGE_MASK;
	if (start_offset) {
		/* partial starting page */

		/* align and read the page that contains the start */
		read_start = start & ~EP_PAGE_MASK;
		read_page(dd, read_start, buffer);

		/* the rest of the page is available data */
		bytes = EP_PAGE_SIZE - start_offset;

		if (len <= bytes) {
			/* end is within this page */
			memcpy(dest, (u8 *)buffer + start_offset, len);
			return 0;
		}

		memcpy(dest, (u8 *)buffer + start_offset, bytes);

		start += bytes;
		len -= bytes;
		dest += bytes;
	}
	/* start is now page aligned */

	/* read whole pages */
	while (len >= EP_PAGE_SIZE) {
		read_page(dd, start, buffer);
		memcpy(dest, buffer, EP_PAGE_SIZE);

		start += EP_PAGE_SIZE;
		len -= EP_PAGE_SIZE;
		dest += EP_PAGE_SIZE;
	}

	/* read the last partial page */
	if (len) {
		read_page(dd, start, buffer);
		memcpy(dest, buffer, len);
	}

	return 0;
}

/*
 * Initialize the EPROM handler.
 */
int eprom_init(struct hfi1_devdata *dd)
{
	int ret = 0;

	/* only the discrete chip has an EPROM */
	if (dd->pcidev->device != PCI_DEVICE_ID_INTEL0)
		return 0;

	/*
	 * It is OK if both HFIs reset the EPROM as long as they don't
	 * do it at the same time.
	 */
	ret = acquire_chip_resource(dd, CR_EPROM, EPROM_TIMEOUT);
	if (ret) {
		dd_dev_err(dd,
			   "%s: unable to acquire EPROM resource, no EPROM support\n",
			   __func__);
		goto done_asic;
	}

	/* reset EPROM to be sure it is in a good state */

	/* set reset */
	write_csr(dd, ASIC_EEP_CTL_STAT, ASIC_EEP_CTL_STAT_EP_RESET_SMASK);
	/* clear reset, set speed */
	write_csr(dd, ASIC_EEP_CTL_STAT,
		  EP_SPEED_FULL << ASIC_EEP_CTL_STAT_RATE_SPI_SHIFT);

	/* wake the device with command "release powerdown NoID" */
	write_csr(dd, ASIC_EEP_ADDR_CMD, CMD_RELEASE_POWERDOWN_NOID);

	dd->eprom_available = true;
	release_chip_resource(dd, CR_EPROM);
done_asic:
	return ret;
}
