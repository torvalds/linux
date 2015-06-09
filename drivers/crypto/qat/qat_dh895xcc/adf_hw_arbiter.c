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
#include <adf_accel_devices.h>
#include <adf_transport_internal.h>
#include "adf_drv.h"

#define ADF_ARB_NUM 4
#define ADF_ARB_REQ_RING_NUM 8
#define ADF_ARB_REG_SIZE 0x4
#define ADF_ARB_WTR_SIZE 0x20
#define ADF_ARB_OFFSET 0x30000
#define ADF_ARB_REG_SLOT 0x1000
#define ADF_ARB_WTR_OFFSET 0x010
#define ADF_ARB_RO_EN_OFFSET 0x090
#define ADF_ARB_WQCFG_OFFSET 0x100
#define ADF_ARB_WRK_2_SER_MAP_OFFSET 0x180
#define ADF_ARB_WRK_2_SER_MAP 10
#define ADF_ARB_RINGSRVARBEN_OFFSET 0x19C

#define WRITE_CSR_ARB_RINGSRVARBEN(csr_addr, index, value) \
	ADF_CSR_WR(csr_addr, ADF_ARB_RINGSRVARBEN_OFFSET + \
	(ADF_ARB_REG_SLOT * index), value)

#define WRITE_CSR_ARB_RESPORDERING(csr_addr, index, value) \
	ADF_CSR_WR(csr_addr, (ADF_ARB_OFFSET + \
	ADF_ARB_RO_EN_OFFSET) + (ADF_ARB_REG_SIZE * index), value)

#define WRITE_CSR_ARB_WEIGHT(csr_addr, arb, index, value) \
	ADF_CSR_WR(csr_addr, (ADF_ARB_OFFSET + \
	ADF_ARB_WTR_OFFSET) + (ADF_ARB_WTR_SIZE * arb) + \
	(ADF_ARB_REG_SIZE * index), value)

#define WRITE_CSR_ARB_SARCONFIG(csr_addr, index, value) \
	ADF_CSR_WR(csr_addr, ADF_ARB_OFFSET + \
	(ADF_ARB_REG_SIZE * index), value)

#define WRITE_CSR_ARB_WRK_2_SER_MAP(csr_addr, index, value) \
	ADF_CSR_WR(csr_addr, (ADF_ARB_OFFSET + \
	ADF_ARB_WRK_2_SER_MAP_OFFSET) + \
	(ADF_ARB_REG_SIZE * index), value)

#define WRITE_CSR_ARB_WQCFG(csr_addr, index, value) \
	ADF_CSR_WR(csr_addr, (ADF_ARB_OFFSET + \
	ADF_ARB_WQCFG_OFFSET) + (ADF_ARB_REG_SIZE * index), value)

int adf_init_arb(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr = accel_dev->transport->banks[0].csr_addr;
	uint32_t arb_cfg = 0x1 << 31 | 0x4 << 4 | 0x1;
	uint32_t arb, i;
	const uint32_t *thd_2_arb_cfg;

	/* Service arb configured for 32 bytes responses and
	 * ring flow control check enabled. */
	for (arb = 0; arb < ADF_ARB_NUM; arb++)
		WRITE_CSR_ARB_SARCONFIG(csr, arb, arb_cfg);

	/* Setup service weighting */
	for (arb = 0; arb < ADF_ARB_NUM; arb++)
		for (i = 0; i < ADF_ARB_REQ_RING_NUM; i++)
			WRITE_CSR_ARB_WEIGHT(csr, arb, i, 0xFFFFFFFF);

	/* Setup ring response ordering */
	for (i = 0; i < ADF_ARB_REQ_RING_NUM; i++)
		WRITE_CSR_ARB_RESPORDERING(csr, i, 0xFFFFFFFF);

	/* Setup worker queue registers */
	for (i = 0; i < ADF_ARB_WRK_2_SER_MAP; i++)
		WRITE_CSR_ARB_WQCFG(csr, i, i);

	/* Map worker threads to service arbiters */
	adf_get_arbiter_mapping(accel_dev, &thd_2_arb_cfg);

	if (!thd_2_arb_cfg)
		return -EFAULT;

	for (i = 0; i < ADF_ARB_WRK_2_SER_MAP; i++)
		WRITE_CSR_ARB_WRK_2_SER_MAP(csr, i, *(thd_2_arb_cfg + i));

	return 0;
}

void adf_update_ring_arb_enable(struct adf_etr_ring_data *ring)
{
	WRITE_CSR_ARB_RINGSRVARBEN(ring->bank->csr_addr,
				   ring->bank->bank_number,
				   ring->bank->ring_mask & 0xFF);
}

void adf_exit_arb(struct adf_accel_dev *accel_dev)
{
	void __iomem *csr;
	unsigned int i;

	if (!accel_dev->transport)
		return;

	csr = accel_dev->transport->banks[0].csr_addr;

	/* Reset arbiter configuration */
	for (i = 0; i < ADF_ARB_NUM; i++)
		WRITE_CSR_ARB_SARCONFIG(csr, i, 0);

	/* Shutdown work queue */
	for (i = 0; i < ADF_ARB_WRK_2_SER_MAP; i++)
		WRITE_CSR_ARB_WQCFG(csr, i, 0);

	/* Unmap worker threads to service arbiters */
	for (i = 0; i < ADF_ARB_WRK_2_SER_MAP; i++)
		WRITE_CSR_ARB_WRK_2_SER_MAP(csr, i, 0);

	/* Disable arbitration on all rings */
	for (i = 0; i < GET_MAX_BANKS(accel_dev); i++)
		WRITE_CSR_ARB_RINGSRVARBEN(csr, i, 0);
}
