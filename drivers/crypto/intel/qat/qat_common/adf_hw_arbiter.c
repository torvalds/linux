// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2014 - 2020 Intel Corporation */
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_transport_internal.h"

#define ADF_ARB_NUM 4
#define ADF_ARB_REG_SIZE 0x4

#define WRITE_CSR_ARB_SARCONFIG(csr_addr, arb_offset, index, value) \
	ADF_CSR_WR(csr_addr, (arb_offset) + \
	(ADF_ARB_REG_SIZE * (index)), value)

#define WRITE_CSR_ARB_WT2SAM(csr_addr, arb_offset, wt_offset, index, value) \
	ADF_CSR_WR(csr_addr, ((arb_offset) + (wt_offset)) + \
	(ADF_ARB_REG_SIZE * (index)), value)

int adf_init_arb(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	void __iomem *csr = accel_dev->transport->banks[0].csr_addr;
	unsigned long ae_mask = hw_data->ae_mask;
	u32 arb_off, wt_off, arb_cfg;
	const u32 *thd_2_arb_cfg;
	struct arb_info info;
	int arb, i;

	hw_data->get_arb_info(&info);
	arb_cfg = info.arb_cfg;
	arb_off = info.arb_offset;
	wt_off = info.wt2sam_offset;

	/* Service arb configured for 32 bytes responses and
	 * ring flow control check enabled. */
	for (arb = 0; arb < ADF_ARB_NUM; arb++)
		WRITE_CSR_ARB_SARCONFIG(csr, arb_off, arb, arb_cfg);

	/* Map worker threads to service arbiters */
	thd_2_arb_cfg = hw_data->get_arb_mapping(accel_dev);

	for_each_set_bit(i, &ae_mask, hw_data->num_engines)
		WRITE_CSR_ARB_WT2SAM(csr, arb_off, wt_off, i, thd_2_arb_cfg[i]);

	return 0;
}
EXPORT_SYMBOL_GPL(adf_init_arb);

void adf_update_ring_arb(struct adf_etr_ring_data *ring)
{
	struct adf_accel_dev *accel_dev = ring->bank->accel_dev;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_hw_csr_ops *csr_ops = GET_CSR_OPS(accel_dev);
	u32 tx_ring_mask = hw_data->tx_rings_mask;
	u32 shift = hw_data->tx_rx_gap;
	u32 arben, arben_tx, arben_rx;
	u32 rx_ring_mask;

	/*
	 * Enable arbitration on a ring only if the TX half of the ring mask
	 * matches the RX part. This results in writes to CSR on both TX and
	 * RX update - only one is necessary, but both are done for
	 * simplicity.
	 */
	rx_ring_mask = tx_ring_mask << shift;
	arben_tx = (ring->bank->ring_mask & tx_ring_mask) >> 0;
	arben_rx = (ring->bank->ring_mask & rx_ring_mask) >> shift;
	arben = arben_tx & arben_rx;

	csr_ops->write_csr_ring_srv_arb_en(ring->bank->csr_addr,
					   ring->bank->bank_number, arben);
}

void adf_exit_arb(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	struct adf_hw_csr_ops *csr_ops = GET_CSR_OPS(accel_dev);
	u32 arb_off, wt_off;
	struct arb_info info;
	void __iomem *csr;
	unsigned int i;

	hw_data->get_arb_info(&info);
	arb_off = info.arb_offset;
	wt_off = info.wt2sam_offset;

	if (!accel_dev->transport)
		return;

	csr = accel_dev->transport->banks[0].csr_addr;

	hw_data->get_arb_info(&info);

	/* Unmap worker threads to service arbiters */
	for (i = 0; i < hw_data->num_engines; i++)
		WRITE_CSR_ARB_WT2SAM(csr, arb_off, wt_off, i, 0);

	/* Disable arbitration on all rings */
	for (i = 0; i < GET_MAX_BANKS(accel_dev); i++)
		csr_ops->write_csr_ring_srv_arb_en(csr, i, 0);
}
EXPORT_SYMBOL_GPL(adf_exit_arb);

int adf_disable_arb_thd(struct adf_accel_dev *accel_dev, u32 ae, u32 thr)
{
	void __iomem *csr = accel_dev->transport->banks[0].csr_addr;
	struct adf_hw_device_data *hw_data = accel_dev->hw_device;
	const u32 *thd_2_arb_cfg;
	struct arb_info info;
	u32 ae_thr_map;

	if (ADF_AE_STRAND0_THREAD == thr || ADF_AE_STRAND1_THREAD == thr)
		thr = ADF_AE_ADMIN_THREAD;

	hw_data->get_arb_info(&info);
	thd_2_arb_cfg = hw_data->get_arb_mapping(accel_dev);
	if (!thd_2_arb_cfg)
		return -EFAULT;

	/* Disable scheduling for this particular AE and thread */
	ae_thr_map = *(thd_2_arb_cfg + ae);
	ae_thr_map &= ~(GENMASK(3, 0) << (thr * BIT(2)));

	WRITE_CSR_ARB_WT2SAM(csr, info.arb_offset, info.wt2sam_offset, ae,
			     ae_thr_map);
	return 0;
}
