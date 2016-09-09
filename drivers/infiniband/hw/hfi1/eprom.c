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

#define CMD_SHIFT 24
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
