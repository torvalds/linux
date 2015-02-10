/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <adf_accel_devices.h>
#include "adf_drv.h"
#include "adf_dh895xcc_hw_data.h"

#define ADF_ADMINMSG_LEN 32

struct adf_admin_comms {
	dma_addr_t phy_addr;
	void *virt_addr;
	void __iomem *mailbox_addr;
	struct mutex lock;	/* protects adf_admin_comms struct */
};

int adf_put_admin_msg_sync(struct adf_accel_dev *accel_dev,
			   uint32_t ae, void *in, void *out)
{
	struct adf_admin_comms *admin = accel_dev->admin;
	int offset = ae * ADF_ADMINMSG_LEN * 2;
	void __iomem *mailbox = admin->mailbox_addr;
	int mb_offset = ae * ADF_DH895XCC_MAILBOX_STRIDE;
	int times, received;

	mutex_lock(&admin->lock);

	if (ADF_CSR_RD(mailbox, mb_offset) == 1) {
		mutex_unlock(&admin->lock);
		return -EAGAIN;
	}

	memcpy(admin->virt_addr + offset, in, ADF_ADMINMSG_LEN);
	ADF_CSR_WR(mailbox, mb_offset, 1);
	received = 0;
	for (times = 0; times < 50; times++) {
		msleep(20);
		if (ADF_CSR_RD(mailbox, mb_offset) == 0) {
			received = 1;
			break;
		}
	}
	if (received)
		memcpy(out, admin->virt_addr + offset +
		       ADF_ADMINMSG_LEN, ADF_ADMINMSG_LEN);
	else
		pr_err("QAT: Failed to send admin msg to accelerator\n");

	mutex_unlock(&admin->lock);
	return received ? 0 : -EFAULT;
}

int adf_init_admin_comms(struct adf_accel_dev *accel_dev)
{
	struct adf_admin_comms *admin;
	struct adf_bar *pmisc = &GET_BARS(accel_dev)[ADF_DH895XCC_PMISC_BAR];
	void __iomem *csr = pmisc->virt_addr;
	void __iomem *mailbox = csr + ADF_DH895XCC_MAILBOX_BASE_OFFSET;
	uint64_t reg_val;

	admin = kzalloc_node(sizeof(*accel_dev->admin), GFP_KERNEL,
			     dev_to_node(&GET_DEV(accel_dev)));
	if (!admin)
		return -ENOMEM;
	admin->virt_addr = dma_zalloc_coherent(&GET_DEV(accel_dev), PAGE_SIZE,
					       &admin->phy_addr, GFP_KERNEL);
	if (!admin->virt_addr) {
		dev_err(&GET_DEV(accel_dev), "Failed to allocate dma buff\n");
		kfree(admin);
		return -ENOMEM;
	}
	reg_val = (uint64_t)admin->phy_addr;
	ADF_CSR_WR(csr, ADF_DH895XCC_ADMINMSGUR_OFFSET, reg_val >> 32);
	ADF_CSR_WR(csr, ADF_DH895XCC_ADMINMSGLR_OFFSET, reg_val);
	mutex_init(&admin->lock);
	admin->mailbox_addr = mailbox;
	accel_dev->admin = admin;
	return 0;
}

void adf_exit_admin_comms(struct adf_accel_dev *accel_dev)
{
	struct adf_admin_comms *admin = accel_dev->admin;

	if (!admin)
		return;

	if (admin->virt_addr)
		dma_free_coherent(&GET_DEV(accel_dev), PAGE_SIZE,
				  admin->virt_addr, admin->phy_addr);

	mutex_destroy(&admin->lock);
	kfree(admin);
	accel_dev->admin = NULL;
}
