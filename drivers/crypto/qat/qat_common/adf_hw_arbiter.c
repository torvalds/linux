// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_transport_internal.h"

#define ADF_ARB_NUM 4
#define ADF_ARB_REG_SIZE 0x4
#define ADF_ARB_WTR_SIZE 0x20
#define ADF_ARB_OFFSET 0x30000
#define ADF_ARB_REG_SLOT 0x1000
#define ADF_ARB_WTR_OFFSET 0x010
#define ADF_ARB_RO_EN_OFFSET 0x090
#define ADF_ARB_WQCFG_OFFSET 0x100
#define ADF_ARB_WRK_2_SER_MAP_OFFSET 0x180
#define ADF_ARB_RINGSRVARBEN_OFFSET 0x19C

#define WRITE_CSR_ARB_RINGSRVARBEN(csr_addr, index, value) \
	ADF_CSR_WR(csr_addr, ADF_ARB_RINGSRVARBEN_OFFSET + \
	(ADF_ARB_REG_SLOT * index), value)

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
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *csr = accel_dev->transport->banks[0].csr_addr;
	u32 arb_cfg = 0x1 << 31 | 0x4 << 4 | 0x1;
	u32 arb, i;
	const u32 *thd_2_arb_cfg;

	/* Service arb configured for 32 bytes responses and
	 * ring flow control check enabled. */
	for (arb = 0; arb < ADF_ARB_NUM; arb++)
		WRITE_CSR_ARB_SARCONFIG(csr, arb, arb_cfg);

	/* Setup worker queue registers */
	for (i = 0; i < hw_data->num_engines; i++)
		WRITE_CSR_ARB_WQCFG(csr, i, i);

	/* Map worker threads to service arbiters */
	hw_data->get_arb_mapping(accel_dev, &thd_2_arb_cfg);

	if (!thd_2_arb_cfg)
		return -EFAULT;

	for (i = 0; i < hw_data->num_engines; i++)
		WRITE_CSR_ARB_WRK_2_SER_MAP(csr, i, *(thd_2_arb_cfg + i));

	return 0;
}
EXPORT_SYMBOL_GPL(adf_init_arb);

void adf_update_ring_arb(struct adf_etr_ring_data *ring)
{
	WRITE_CSR_ARB_RINGSRVARBEN(ring->bank->csr_addr,
				   ring->bank->bank_number,
				   ring->bank->ring_mask & 0xFF);
}

void adf_exit_arb(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *csr;
	unsigned int i;

	if (!accel_dev->transport)
		return;

	csr = accel_dev->transport->banks[0].csr_addr;

	/* Reset arbiter configuration */
	for (i = 0; i < ADF_ARB_NUM; i++)
		WRITE_CSR_ARB_SARCONFIG(csr, i, 0);

	/* Shutdown work queue */
	for (i = 0; i < hw_data->num_engines; i++)
		WRITE_CSR_ARB_WQCFG(csr, i, 0);

	/* Unmap worker threads to service arbiters */
	for (i = 0; i < hw_data->num_engines; i++)
		WRITE_CSR_ARB_WRK_2_SER_MAP(csr, i, 0);

	/* Disable arbitration on all rings */
	for (i = 0; i < GET_MAX_BANKS(accel_dev); i++)
		WRITE_CSR_ARB_RINGSRVARBEN(csr, i, 0);
}
EXPORT_SYMBOL_GPL(adf_exit_arb);
