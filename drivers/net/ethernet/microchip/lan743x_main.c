/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2018 Microchip Technology Inc. */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/crc32.h>
#include <linux/microchipphy.h>
#include <linux/net_tstamp.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/rtnetlink.h>
#include <linux/iopoll.h>
#include <linux/crc16.h>
#include "lan743x_main.h"
#include "lan743x_ethtool.h"

static void lan743x_pci_cleanup(struct lan743x_adapter *adapter)
{
	pci_release_selected_regions(adapter->pdev,
				     pci_select_bars(adapter->pdev,
						     IORESOURCE_MEM));
	pci_disable_device(adapter->pdev);
}

static int lan743x_pci_init(struct lan743x_adapter *adapter,
			    struct pci_dev *pdev)
{
	unsigned long bars = 0;
	int ret;

	adapter->pdev = pdev;
	ret = pci_enable_device_mem(pdev);
	if (ret)
		goto return_error;

	netif_info(adapter, probe, adapter->netdev,
		   "PCI: Vendor ID = 0x%04X, Device ID = 0x%04X\n",
		   pdev->vendor, pdev->device);
	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if (!test_bit(0, &bars))
		goto disable_device;

	ret = pci_request_selected_regions(pdev, bars, DRIVER_NAME);
	if (ret)
		goto disable_device;

	pci_set_master(pdev);
	return 0;

disable_device:
	pci_disable_device(adapter->pdev);

return_error:
	return ret;
}

u32 lan743x_csr_read(struct lan743x_adapter *adapter, int offset)
{
	return ioread32(&adapter->csr.csr_address[offset]);
}

void lan743x_csr_write(struct lan743x_adapter *adapter, int offset,
		       u32 data)
{
	iowrite32(data, &adapter->csr.csr_address[offset]);
}

#define LAN743X_CSR_READ_OP(offset)	lan743x_csr_read(adapter, offset)

static int lan743x_csr_light_reset(struct lan743x_adapter *adapter)
{
	u32 data;

	data = lan743x_csr_read(adapter, HW_CFG);
	data |= HW_CFG_LRST_;
	lan743x_csr_write(adapter, HW_CFG, data);

	return readx_poll_timeout(LAN743X_CSR_READ_OP, HW_CFG, data,
				  !(data & HW_CFG_LRST_), 100000, 10000000);
}

static int lan743x_csr_wait_for_bit(struct lan743x_adapter *adapter,
				    int offset, u32 bit_mask,
				    int target_value, int usleep_min,
				    int usleep_max, int count)
{
	u32 data;

	return readx_poll_timeout(LAN743X_CSR_READ_OP, offset, data,
				  target_value == ((data & bit_mask) ? 1 : 0),
				  usleep_max, usleep_min * count);
}

static int lan743x_csr_init(struct lan743x_adapter *adapter)
{
	struct lan743x_csr *csr = &adapter->csr;
	resource_size_t bar_start, bar_length;
	int result;

	bar_start = pci_resource_start(adapter->pdev, 0);
	bar_length = pci_resource_len(adapter->pdev, 0);
	csr->csr_address = devm_ioremap(&adapter->pdev->dev,
					bar_start, bar_length);
	if (!csr->csr_address) {
		result = -ENOMEM;
		goto clean_up;
	}

	csr->id_rev = lan743x_csr_read(adapter, ID_REV);
	csr->fpga_rev = lan743x_csr_read(adapter, FPGA_REV);
	netif_info(adapter, probe, adapter->netdev,
		   "ID_REV = 0x%08X, FPGA_REV = %d.%d\n",
		   csr->id_rev,	FPGA_REV_GET_MAJOR_(csr->fpga_rev),
		   FPGA_REV_GET_MINOR_(csr->fpga_rev));
	if (!ID_REV_IS_VALID_CHIP_ID_(csr->id_rev)) {
		result = -ENODEV;
		goto clean_up;
	}

	csr->flags = LAN743X_CSR_FLAG_SUPPORTS_INTR_AUTO_SET_CLR;
	switch (csr->id_rev & ID_REV_CHIP_REV_MASK_) {
	case ID_REV_CHIP_REV_A0_:
		csr->flags |= LAN743X_CSR_FLAG_IS_A0;
		csr->flags &= ~LAN743X_CSR_FLAG_SUPPORTS_INTR_AUTO_SET_CLR;
		break;
	case ID_REV_CHIP_REV_B0_:
		csr->flags |= LAN743X_CSR_FLAG_IS_B0;
		break;
	}

	result = lan743x_csr_light_reset(adapter);
	if (result)
		goto clean_up;
	return 0;
clean_up:
	return result;
}

static void lan743x_intr_software_isr(struct lan743x_adapter *adapter)
{
	struct lan743x_intr *intr = &adapter->intr;

	/* disable the interrupt to prevent repeated re-triggering */
	lan743x_csr_write(adapter, INT_EN_CLR, INT_BIT_SW_GP_);
	intr->software_isr_flag = true;
	wake_up(&intr->software_isr_wq);
}

static void lan743x_tx_isr(void *context, u32 int_sts, u32 flags)
{
	struct lan743x_tx *tx = context;
	struct lan743x_adapter *adapter = tx->adapter;
	bool enable_flag = true;

	lan743x_csr_read(adapter, INT_EN_SET);
	if (flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CLEAR) {
		lan743x_csr_write(adapter, INT_EN_CLR,
				  INT_BIT_DMA_TX_(tx->channel_number));
	}

	if (int_sts & INT_BIT_DMA_TX_(tx->channel_number)) {
		u32 ioc_bit = DMAC_INT_BIT_TX_IOC_(tx->channel_number);
		u32 dmac_int_sts;
		u32 dmac_int_en;

		if (flags & LAN743X_VECTOR_FLAG_SOURCE_STATUS_READ)
			dmac_int_sts = lan743x_csr_read(adapter, DMAC_INT_STS);
		else
			dmac_int_sts = ioc_bit;
		if (flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CHECK)
			dmac_int_en = lan743x_csr_read(adapter,
						       DMAC_INT_EN_SET);
		else
			dmac_int_en = ioc_bit;

		dmac_int_en &= ioc_bit;
		dmac_int_sts &= dmac_int_en;
		if (dmac_int_sts & ioc_bit) {
			napi_schedule(&tx->napi);
			enable_flag = false;/* poll func will enable later */
		}
	}

	if (enable_flag)
		/* enable isr */
		lan743x_csr_write(adapter, INT_EN_SET,
				  INT_BIT_DMA_TX_(tx->channel_number));
}

static void lan743x_rx_isr(void *context, u32 int_sts, u32 flags)
{
	struct lan743x_rx *rx = context;
	struct lan743x_adapter *adapter = rx->adapter;
	bool enable_flag = true;

	if (flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CLEAR) {
		lan743x_csr_write(adapter, INT_EN_CLR,
				  INT_BIT_DMA_RX_(rx->channel_number));
	}

	if (int_sts & INT_BIT_DMA_RX_(rx->channel_number)) {
		u32 rx_frame_bit = DMAC_INT_BIT_RXFRM_(rx->channel_number);
		u32 dmac_int_sts;
		u32 dmac_int_en;

		if (flags & LAN743X_VECTOR_FLAG_SOURCE_STATUS_READ)
			dmac_int_sts = lan743x_csr_read(adapter, DMAC_INT_STS);
		else
			dmac_int_sts = rx_frame_bit;
		if (flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CHECK)
			dmac_int_en = lan743x_csr_read(adapter,
						       DMAC_INT_EN_SET);
		else
			dmac_int_en = rx_frame_bit;

		dmac_int_en &= rx_frame_bit;
		dmac_int_sts &= dmac_int_en;
		if (dmac_int_sts & rx_frame_bit) {
			napi_schedule(&rx->napi);
			enable_flag = false;/* poll funct will enable later */
		}
	}

	if (enable_flag) {
		/* enable isr */
		lan743x_csr_write(adapter, INT_EN_SET,
				  INT_BIT_DMA_RX_(rx->channel_number));
	}
}

static void lan743x_intr_shared_isr(void *context, u32 int_sts, u32 flags)
{
	struct lan743x_adapter *adapter = context;
	unsigned int channel;

	if (int_sts & INT_BIT_ALL_RX_) {
		for (channel = 0; channel < LAN743X_USED_RX_CHANNELS;
			channel++) {
			u32 int_bit = INT_BIT_DMA_RX_(channel);

			if (int_sts & int_bit) {
				lan743x_rx_isr(&adapter->rx[channel],
					       int_bit, flags);
				int_sts &= ~int_bit;
			}
		}
	}
	if (int_sts & INT_BIT_ALL_TX_) {
		for (channel = 0; channel < LAN743X_USED_TX_CHANNELS;
			channel++) {
			u32 int_bit = INT_BIT_DMA_TX_(channel);

			if (int_sts & int_bit) {
				lan743x_tx_isr(&adapter->tx[channel],
					       int_bit, flags);
				int_sts &= ~int_bit;
			}
		}
	}
	if (int_sts & INT_BIT_ALL_OTHER_) {
		if (int_sts & INT_BIT_SW_GP_) {
			lan743x_intr_software_isr(adapter);
			int_sts &= ~INT_BIT_SW_GP_;
		}
		if (int_sts & INT_BIT_1588_) {
			lan743x_ptp_isr(adapter);
			int_sts &= ~INT_BIT_1588_;
		}
	}
	if (int_sts)
		lan743x_csr_write(adapter, INT_EN_CLR, int_sts);
}

static irqreturn_t lan743x_intr_entry_isr(int irq, void *ptr)
{
	struct lan743x_vector *vector = ptr;
	struct lan743x_adapter *adapter = vector->adapter;
	irqreturn_t result = IRQ_NONE;
	u32 int_enables;
	u32 int_sts;

	if (vector->flags & LAN743X_VECTOR_FLAG_SOURCE_STATUS_READ) {
		int_sts = lan743x_csr_read(adapter, INT_STS);
	} else if (vector->flags &
		   (LAN743X_VECTOR_FLAG_SOURCE_STATUS_R2C |
		   LAN743X_VECTOR_FLAG_SOURCE_ENABLE_R2C)) {
		int_sts = lan743x_csr_read(adapter, INT_STS_R2C);
	} else {
		/* use mask as implied status */
		int_sts = vector->int_mask | INT_BIT_MAS_;
	}

	if (!(int_sts & INT_BIT_MAS_))
		goto irq_done;

	if (vector->flags & LAN743X_VECTOR_FLAG_VECTOR_ENABLE_ISR_CLEAR)
		/* disable vector interrupt */
		lan743x_csr_write(adapter,
				  INT_VEC_EN_CLR,
				  INT_VEC_EN_(vector->vector_index));

	if (vector->flags & LAN743X_VECTOR_FLAG_MASTER_ENABLE_CLEAR)
		/* disable master interrupt */
		lan743x_csr_write(adapter, INT_EN_CLR, INT_BIT_MAS_);

	if (vector->flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CHECK) {
		int_enables = lan743x_csr_read(adapter, INT_EN_SET);
	} else {
		/*  use vector mask as implied enable mask */
		int_enables = vector->int_mask;
	}

	int_sts &= int_enables;
	int_sts &= vector->int_mask;
	if (int_sts) {
		if (vector->handler) {
			vector->handler(vector->context,
					int_sts, vector->flags);
		} else {
			/* disable interrupts on this vector */
			lan743x_csr_write(adapter, INT_EN_CLR,
					  vector->int_mask);
		}
		result = IRQ_HANDLED;
	}

	if (vector->flags & LAN743X_VECTOR_FLAG_MASTER_ENABLE_SET)
		/* enable master interrupt */
		lan743x_csr_write(adapter, INT_EN_SET, INT_BIT_MAS_);

	if (vector->flags & LAN743X_VECTOR_FLAG_VECTOR_ENABLE_ISR_SET)
		/* enable vector interrupt */
		lan743x_csr_write(adapter,
				  INT_VEC_EN_SET,
				  INT_VEC_EN_(vector->vector_index));
irq_done:
	return result;
}

static int lan743x_intr_test_isr(struct lan743x_adapter *adapter)
{
	struct lan743x_intr *intr = &adapter->intr;
	int ret;

	intr->software_isr_flag = false;

	/* enable and activate test interrupt */
	lan743x_csr_write(adapter, INT_EN_SET, INT_BIT_SW_GP_);
	lan743x_csr_write(adapter, INT_SET, INT_BIT_SW_GP_);

	ret = wait_event_timeout(intr->software_isr_wq,
				 intr->software_isr_flag,
				 msecs_to_jiffies(200));

	/* disable test interrupt */
	lan743x_csr_write(adapter, INT_EN_CLR, INT_BIT_SW_GP_);

	return ret > 0 ? 0 : -ENODEV;
}

static int lan743x_intr_register_isr(struct lan743x_adapter *adapter,
				     int vector_index, u32 flags,
				     u32 int_mask,
				     lan743x_vector_handler handler,
				     void *context)
{
	struct lan743x_vector *vector = &adapter->intr.vector_list
					[vector_index];
	int ret;

	vector->adapter = adapter;
	vector->flags = flags;
	vector->vector_index = vector_index;
	vector->int_mask = int_mask;
	vector->handler = handler;
	vector->context = context;

	ret = request_irq(vector->irq,
			  lan743x_intr_entry_isr,
			  (flags & LAN743X_VECTOR_FLAG_IRQ_SHARED) ?
			  IRQF_SHARED : 0, DRIVER_NAME, vector);
	if (ret) {
		vector->handler = NULL;
		vector->context = NULL;
		vector->int_mask = 0;
		vector->flags = 0;
	}
	return ret;
}

static void lan743x_intr_unregister_isr(struct lan743x_adapter *adapter,
					int vector_index)
{
	struct lan743x_vector *vector = &adapter->intr.vector_list
					[vector_index];

	free_irq(vector->irq, vector);
	vector->handler = NULL;
	vector->context = NULL;
	vector->int_mask = 0;
	vector->flags = 0;
}

static u32 lan743x_intr_get_vector_flags(struct lan743x_adapter *adapter,
					 u32 int_mask)
{
	int index;

	for (index = 0; index < LAN743X_MAX_VECTOR_COUNT; index++) {
		if (adapter->intr.vector_list[index].int_mask & int_mask)
			return adapter->intr.vector_list[index].flags;
	}
	return 0;
}

static void lan743x_intr_close(struct lan743x_adapter *adapter)
{
	struct lan743x_intr *intr = &adapter->intr;
	int index = 0;

	lan743x_csr_write(adapter, INT_EN_CLR, INT_BIT_MAS_);
	lan743x_csr_write(adapter, INT_VEC_EN_CLR, 0x000000FF);

	for (index = 0; index < LAN743X_MAX_VECTOR_COUNT; index++) {
		if (intr->flags & INTR_FLAG_IRQ_REQUESTED(index)) {
			lan743x_intr_unregister_isr(adapter, index);
			intr->flags &= ~INTR_FLAG_IRQ_REQUESTED(index);
		}
	}

	if (intr->flags & INTR_FLAG_MSI_ENABLED) {
		pci_disable_msi(adapter->pdev);
		intr->flags &= ~INTR_FLAG_MSI_ENABLED;
	}

	if (intr->flags & INTR_FLAG_MSIX_ENABLED) {
		pci_disable_msix(adapter->pdev);
		intr->flags &= ~INTR_FLAG_MSIX_ENABLED;
	}
}

static int lan743x_intr_open(struct lan743x_adapter *adapter)
{
	struct msix_entry msix_entries[LAN743X_MAX_VECTOR_COUNT];
	struct lan743x_intr *intr = &adapter->intr;
	u32 int_vec_en_auto_clr = 0;
	u32 int_vec_map0 = 0;
	u32 int_vec_map1 = 0;
	int ret = -ENODEV;
	int index = 0;
	u32 flags = 0;

	intr->number_of_vectors = 0;

	/* Try to set up MSIX interrupts */
	memset(&msix_entries[0], 0,
	       sizeof(struct msix_entry) * LAN743X_MAX_VECTOR_COUNT);
	for (index = 0; index < LAN743X_MAX_VECTOR_COUNT; index++)
		msix_entries[index].entry = index;
	ret = pci_enable_msix_range(adapter->pdev,
				    msix_entries, 1,
				    1 + LAN743X_USED_TX_CHANNELS +
				    LAN743X_USED_RX_CHANNELS);

	if (ret > 0) {
		intr->flags |= INTR_FLAG_MSIX_ENABLED;
		intr->number_of_vectors = ret;
		intr->using_vectors = true;
		for (index = 0; index < intr->number_of_vectors; index++)
			intr->vector_list[index].irq = msix_entries
						       [index].vector;
		netif_info(adapter, ifup, adapter->netdev,
			   "using MSIX interrupts, number of vectors = %d\n",
			   intr->number_of_vectors);
	}

	/* If MSIX failed try to setup using MSI interrupts */
	if (!intr->number_of_vectors) {
		if (!(adapter->csr.flags & LAN743X_CSR_FLAG_IS_A0)) {
			if (!pci_enable_msi(adapter->pdev)) {
				intr->flags |= INTR_FLAG_MSI_ENABLED;
				intr->number_of_vectors = 1;
				intr->using_vectors = true;
				intr->vector_list[0].irq =
					adapter->pdev->irq;
				netif_info(adapter, ifup, adapter->netdev,
					   "using MSI interrupts, number of vectors = %d\n",
					   intr->number_of_vectors);
			}
		}
	}

	/* If MSIX, and MSI failed, setup using legacy interrupt */
	if (!intr->number_of_vectors) {
		intr->number_of_vectors = 1;
		intr->using_vectors = false;
		intr->vector_list[0].irq = intr->irq;
		netif_info(adapter, ifup, adapter->netdev,
			   "using legacy interrupts\n");
	}

	/* At this point we must have at least one irq */
	lan743x_csr_write(adapter, INT_VEC_EN_CLR, 0xFFFFFFFF);

	/* map all interrupts to vector 0 */
	lan743x_csr_write(adapter, INT_VEC_MAP0, 0x00000000);
	lan743x_csr_write(adapter, INT_VEC_MAP1, 0x00000000);
	lan743x_csr_write(adapter, INT_VEC_MAP2, 0x00000000);
	flags = LAN743X_VECTOR_FLAG_SOURCE_STATUS_READ |
		LAN743X_VECTOR_FLAG_SOURCE_STATUS_W2C |
		LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CHECK |
		LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CLEAR;

	if (intr->using_vectors) {
		flags |= LAN743X_VECTOR_FLAG_VECTOR_ENABLE_ISR_CLEAR |
			 LAN743X_VECTOR_FLAG_VECTOR_ENABLE_ISR_SET;
	} else {
		flags |= LAN743X_VECTOR_FLAG_MASTER_ENABLE_CLEAR |
			 LAN743X_VECTOR_FLAG_MASTER_ENABLE_SET |
			 LAN743X_VECTOR_FLAG_IRQ_SHARED;
	}

	if (adapter->csr.flags & LAN743X_CSR_FLAG_SUPPORTS_INTR_AUTO_SET_CLR) {
		flags &= ~LAN743X_VECTOR_FLAG_SOURCE_STATUS_READ;
		flags &= ~LAN743X_VECTOR_FLAG_SOURCE_STATUS_W2C;
		flags &= ~LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CLEAR;
		flags &= ~LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CHECK;
		flags |= LAN743X_VECTOR_FLAG_SOURCE_STATUS_R2C;
		flags |= LAN743X_VECTOR_FLAG_SOURCE_ENABLE_R2C;
	}

	init_waitqueue_head(&intr->software_isr_wq);

	ret = lan743x_intr_register_isr(adapter, 0, flags,
					INT_BIT_ALL_RX_ | INT_BIT_ALL_TX_ |
					INT_BIT_ALL_OTHER_,
					lan743x_intr_shared_isr, adapter);
	if (ret)
		goto clean_up;
	intr->flags |= INTR_FLAG_IRQ_REQUESTED(0);

	if (intr->using_vectors)
		lan743x_csr_write(adapter, INT_VEC_EN_SET,
				  INT_VEC_EN_(0));

	if (!(adapter->csr.flags & LAN743X_CSR_FLAG_IS_A0)) {
		lan743x_csr_write(adapter, INT_MOD_CFG0, LAN743X_INT_MOD);
		lan743x_csr_write(adapter, INT_MOD_CFG1, LAN743X_INT_MOD);
		lan743x_csr_write(adapter, INT_MOD_CFG2, LAN743X_INT_MOD);
		lan743x_csr_write(adapter, INT_MOD_CFG3, LAN743X_INT_MOD);
		lan743x_csr_write(adapter, INT_MOD_CFG4, LAN743X_INT_MOD);
		lan743x_csr_write(adapter, INT_MOD_CFG5, LAN743X_INT_MOD);
		lan743x_csr_write(adapter, INT_MOD_CFG6, LAN743X_INT_MOD);
		lan743x_csr_write(adapter, INT_MOD_CFG7, LAN743X_INT_MOD);
		lan743x_csr_write(adapter, INT_MOD_MAP0, 0x00005432);
		lan743x_csr_write(adapter, INT_MOD_MAP1, 0x00000001);
		lan743x_csr_write(adapter, INT_MOD_MAP2, 0x00FFFFFF);
	}

	/* enable interrupts */
	lan743x_csr_write(adapter, INT_EN_SET, INT_BIT_MAS_);
	ret = lan743x_intr_test_isr(adapter);
	if (ret)
		goto clean_up;

	if (intr->number_of_vectors > 1) {
		int number_of_tx_vectors = intr->number_of_vectors - 1;

		if (number_of_tx_vectors > LAN743X_USED_TX_CHANNELS)
			number_of_tx_vectors = LAN743X_USED_TX_CHANNELS;
		flags = LAN743X_VECTOR_FLAG_SOURCE_STATUS_READ |
			LAN743X_VECTOR_FLAG_SOURCE_STATUS_W2C |
			LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CHECK |
			LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CLEAR |
			LAN743X_VECTOR_FLAG_VECTOR_ENABLE_ISR_CLEAR |
			LAN743X_VECTOR_FLAG_VECTOR_ENABLE_ISR_SET;

		if (adapter->csr.flags &
		   LAN743X_CSR_FLAG_SUPPORTS_INTR_AUTO_SET_CLR) {
			flags = LAN743X_VECTOR_FLAG_VECTOR_ENABLE_AUTO_SET |
				LAN743X_VECTOR_FLAG_SOURCE_ENABLE_AUTO_SET |
				LAN743X_VECTOR_FLAG_SOURCE_ENABLE_AUTO_CLEAR |
				LAN743X_VECTOR_FLAG_SOURCE_STATUS_AUTO_CLEAR;
		}

		for (index = 0; index < number_of_tx_vectors; index++) {
			u32 int_bit = INT_BIT_DMA_TX_(index);
			int vector = index + 1;

			/* map TX interrupt to vector */
			int_vec_map1 |= INT_VEC_MAP1_TX_VEC_(index, vector);
			lan743x_csr_write(adapter, INT_VEC_MAP1, int_vec_map1);

			/* Remove TX interrupt from shared mask */
			intr->vector_list[0].int_mask &= ~int_bit;
			ret = lan743x_intr_register_isr(adapter, vector, flags,
							int_bit, lan743x_tx_isr,
							&adapter->tx[index]);
			if (ret)
				goto clean_up;
			intr->flags |= INTR_FLAG_IRQ_REQUESTED(vector);
			if (!(flags &
			    LAN743X_VECTOR_FLAG_VECTOR_ENABLE_AUTO_SET))
				lan743x_csr_write(adapter, INT_VEC_EN_SET,
						  INT_VEC_EN_(vector));
		}
	}
	if ((intr->number_of_vectors - LAN743X_USED_TX_CHANNELS) > 1) {
		int number_of_rx_vectors = intr->number_of_vectors -
					   LAN743X_USED_TX_CHANNELS - 1;

		if (number_of_rx_vectors > LAN743X_USED_RX_CHANNELS)
			number_of_rx_vectors = LAN743X_USED_RX_CHANNELS;

		flags = LAN743X_VECTOR_FLAG_SOURCE_STATUS_READ |
			LAN743X_VECTOR_FLAG_SOURCE_STATUS_W2C |
			LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CHECK |
			LAN743X_VECTOR_FLAG_SOURCE_ENABLE_CLEAR |
			LAN743X_VECTOR_FLAG_VECTOR_ENABLE_ISR_CLEAR |
			LAN743X_VECTOR_FLAG_VECTOR_ENABLE_ISR_SET;

		if (adapter->csr.flags &
		    LAN743X_CSR_FLAG_SUPPORTS_INTR_AUTO_SET_CLR) {
			flags = LAN743X_VECTOR_FLAG_VECTOR_ENABLE_AUTO_CLEAR |
				LAN743X_VECTOR_FLAG_VECTOR_ENABLE_AUTO_SET |
				LAN743X_VECTOR_FLAG_SOURCE_ENABLE_AUTO_SET |
				LAN743X_VECTOR_FLAG_SOURCE_ENABLE_AUTO_CLEAR |
				LAN743X_VECTOR_FLAG_SOURCE_STATUS_AUTO_CLEAR;
		}
		for (index = 0; index < number_of_rx_vectors; index++) {
			int vector = index + 1 + LAN743X_USED_TX_CHANNELS;
			u32 int_bit = INT_BIT_DMA_RX_(index);

			/* map RX interrupt to vector */
			int_vec_map0 |= INT_VEC_MAP0_RX_VEC_(index, vector);
			lan743x_csr_write(adapter, INT_VEC_MAP0, int_vec_map0);
			if (flags &
			    LAN743X_VECTOR_FLAG_VECTOR_ENABLE_AUTO_CLEAR) {
				int_vec_en_auto_clr |= INT_VEC_EN_(vector);
				lan743x_csr_write(adapter, INT_VEC_EN_AUTO_CLR,
						  int_vec_en_auto_clr);
			}

			/* Remove RX interrupt from shared mask */
			intr->vector_list[0].int_mask &= ~int_bit;
			ret = lan743x_intr_register_isr(adapter, vector, flags,
							int_bit, lan743x_rx_isr,
							&adapter->rx[index]);
			if (ret)
				goto clean_up;
			intr->flags |= INTR_FLAG_IRQ_REQUESTED(vector);

			lan743x_csr_write(adapter, INT_VEC_EN_SET,
					  INT_VEC_EN_(vector));
		}
	}
	return 0;

clean_up:
	lan743x_intr_close(adapter);
	return ret;
}

static int lan743x_dp_write(struct lan743x_adapter *adapter,
			    u32 select, u32 addr, u32 length, u32 *buf)
{
	u32 dp_sel;
	int i;

	if (lan743x_csr_wait_for_bit(adapter, DP_SEL, DP_SEL_DPRDY_,
				     1, 40, 100, 100))
		return -EIO;
	dp_sel = lan743x_csr_read(adapter, DP_SEL);
	dp_sel &= ~DP_SEL_MASK_;
	dp_sel |= select;
	lan743x_csr_write(adapter, DP_SEL, dp_sel);

	for (i = 0; i < length; i++) {
		lan743x_csr_write(adapter, DP_ADDR, addr + i);
		lan743x_csr_write(adapter, DP_DATA_0, buf[i]);
		lan743x_csr_write(adapter, DP_CMD, DP_CMD_WRITE_);
		if (lan743x_csr_wait_for_bit(adapter, DP_SEL, DP_SEL_DPRDY_,
					     1, 40, 100, 100))
			return -EIO;
	}

	return 0;
}

static u32 lan743x_mac_mii_access(u16 id, u16 index, int read)
{
	u32 ret;

	ret = (id << MAC_MII_ACC_PHY_ADDR_SHIFT_) &
		MAC_MII_ACC_PHY_ADDR_MASK_;
	ret |= (index << MAC_MII_ACC_MIIRINDA_SHIFT_) &
		MAC_MII_ACC_MIIRINDA_MASK_;

	if (read)
		ret |= MAC_MII_ACC_MII_READ_;
	else
		ret |= MAC_MII_ACC_MII_WRITE_;
	ret |= MAC_MII_ACC_MII_BUSY_;

	return ret;
}

static int lan743x_mac_mii_wait_till_not_busy(struct lan743x_adapter *adapter)
{
	u32 data;

	return readx_poll_timeout(LAN743X_CSR_READ_OP, MAC_MII_ACC, data,
				  !(data & MAC_MII_ACC_MII_BUSY_), 0, 1000000);
}

static int lan743x_mdiobus_read(struct mii_bus *bus, int phy_id, int index)
{
	struct lan743x_adapter *adapter = bus->priv;
	u32 val, mii_access;
	int ret;

	/* comfirm MII not busy */
	ret = lan743x_mac_mii_wait_till_not_busy(adapter);
	if (ret < 0)
		return ret;

	/* set the address, index & direction (read from PHY) */
	mii_access = lan743x_mac_mii_access(phy_id, index, MAC_MII_READ);
	lan743x_csr_write(adapter, MAC_MII_ACC, mii_access);
	ret = lan743x_mac_mii_wait_till_not_busy(adapter);
	if (ret < 0)
		return ret;

	val = lan743x_csr_read(adapter, MAC_MII_DATA);
	return (int)(val & 0xFFFF);
}

static int lan743x_mdiobus_write(struct mii_bus *bus,
				 int phy_id, int index, u16 regval)
{
	struct lan743x_adapter *adapter = bus->priv;
	u32 val, mii_access;
	int ret;

	/* confirm MII not busy */
	ret = lan743x_mac_mii_wait_till_not_busy(adapter);
	if (ret < 0)
		return ret;
	val = (u32)regval;
	lan743x_csr_write(adapter, MAC_MII_DATA, val);

	/* set the address, index & direction (write to PHY) */
	mii_access = lan743x_mac_mii_access(phy_id, index, MAC_MII_WRITE);
	lan743x_csr_write(adapter, MAC_MII_ACC, mii_access);
	ret = lan743x_mac_mii_wait_till_not_busy(adapter);
	return ret;
}

static void lan743x_mac_set_address(struct lan743x_adapter *adapter,
				    u8 *addr)
{
	u32 addr_lo, addr_hi;

	addr_lo = addr[0] |
		addr[1] << 8 |
		addr[2] << 16 |
		addr[3] << 24;
	addr_hi = addr[4] |
		addr[5] << 8;
	lan743x_csr_write(adapter, MAC_RX_ADDRL, addr_lo);
	lan743x_csr_write(adapter, MAC_RX_ADDRH, addr_hi);

	ether_addr_copy(adapter->mac_address, addr);
	netif_info(adapter, drv, adapter->netdev,
		   "MAC address set to %pM\n", addr);
}

static int lan743x_mac_init(struct lan743x_adapter *adapter)
{
	bool mac_address_valid = true;
	struct net_device *netdev;
	u32 mac_addr_hi = 0;
	u32 mac_addr_lo = 0;
	u32 data;

	netdev = adapter->netdev;

	/* disable auto duplex, and speed detection. Phylib does that */
	data = lan743x_csr_read(adapter, MAC_CR);
	data &= ~(MAC_CR_ADD_ | MAC_CR_ASD_);
	data |= MAC_CR_CNTR_RST_;
	lan743x_csr_write(adapter, MAC_CR, data);

	if (!is_valid_ether_addr(adapter->mac_address)) {
		mac_addr_hi = lan743x_csr_read(adapter, MAC_RX_ADDRH);
		mac_addr_lo = lan743x_csr_read(adapter, MAC_RX_ADDRL);
		adapter->mac_address[0] = mac_addr_lo & 0xFF;
		adapter->mac_address[1] = (mac_addr_lo >> 8) & 0xFF;
		adapter->mac_address[2] = (mac_addr_lo >> 16) & 0xFF;
		adapter->mac_address[3] = (mac_addr_lo >> 24) & 0xFF;
		adapter->mac_address[4] = mac_addr_hi & 0xFF;
		adapter->mac_address[5] = (mac_addr_hi >> 8) & 0xFF;

		if (((mac_addr_hi & 0x0000FFFF) == 0x0000FFFF) &&
		    mac_addr_lo == 0xFFFFFFFF) {
			mac_address_valid = false;
		} else if (!is_valid_ether_addr(adapter->mac_address)) {
			mac_address_valid = false;
		}

		if (!mac_address_valid)
			eth_random_addr(adapter->mac_address);
	}
	lan743x_mac_set_address(adapter, adapter->mac_address);
	eth_hw_addr_set(netdev, adapter->mac_address);

	return 0;
}

static int lan743x_mac_open(struct lan743x_adapter *adapter)
{
	u32 temp;

	temp = lan743x_csr_read(adapter, MAC_RX);
	lan743x_csr_write(adapter, MAC_RX, temp | MAC_RX_RXEN_);
	temp = lan743x_csr_read(adapter, MAC_TX);
	lan743x_csr_write(adapter, MAC_TX, temp | MAC_TX_TXEN_);
	return 0;
}

static void lan743x_mac_close(struct lan743x_adapter *adapter)
{
	u32 temp;

	temp = lan743x_csr_read(adapter, MAC_TX);
	temp &= ~MAC_TX_TXEN_;
	lan743x_csr_write(adapter, MAC_TX, temp);
	lan743x_csr_wait_for_bit(adapter, MAC_TX, MAC_TX_TXD_,
				 1, 1000, 20000, 100);

	temp = lan743x_csr_read(adapter, MAC_RX);
	temp &= ~MAC_RX_RXEN_;
	lan743x_csr_write(adapter, MAC_RX, temp);
	lan743x_csr_wait_for_bit(adapter, MAC_RX, MAC_RX_RXD_,
				 1, 1000, 20000, 100);
}

static void lan743x_mac_flow_ctrl_set_enables(struct lan743x_adapter *adapter,
					      bool tx_enable, bool rx_enable)
{
	u32 flow_setting = 0;

	/* set maximum pause time because when fifo space frees
	 * up a zero value pause frame will be sent to release the pause
	 */
	flow_setting = MAC_FLOW_CR_FCPT_MASK_;
	if (tx_enable)
		flow_setting |= MAC_FLOW_CR_TX_FCEN_;
	if (rx_enable)
		flow_setting |= MAC_FLOW_CR_RX_FCEN_;
	lan743x_csr_write(adapter, MAC_FLOW, flow_setting);
}

static int lan743x_mac_set_mtu(struct lan743x_adapter *adapter, int new_mtu)
{
	int enabled = 0;
	u32 mac_rx = 0;

	mac_rx = lan743x_csr_read(adapter, MAC_RX);
	if (mac_rx & MAC_RX_RXEN_) {
		enabled = 1;
		if (mac_rx & MAC_RX_RXD_) {
			lan743x_csr_write(adapter, MAC_RX, mac_rx);
			mac_rx &= ~MAC_RX_RXD_;
		}
		mac_rx &= ~MAC_RX_RXEN_;
		lan743x_csr_write(adapter, MAC_RX, mac_rx);
		lan743x_csr_wait_for_bit(adapter, MAC_RX, MAC_RX_RXD_,
					 1, 1000, 20000, 100);
		lan743x_csr_write(adapter, MAC_RX, mac_rx | MAC_RX_RXD_);
	}

	mac_rx &= ~(MAC_RX_MAX_SIZE_MASK_);
	mac_rx |= (((new_mtu + ETH_HLEN + ETH_FCS_LEN)
		  << MAC_RX_MAX_SIZE_SHIFT_) & MAC_RX_MAX_SIZE_MASK_);
	lan743x_csr_write(adapter, MAC_RX, mac_rx);

	if (enabled) {
		mac_rx |= MAC_RX_RXEN_;
		lan743x_csr_write(adapter, MAC_RX, mac_rx);
	}
	return 0;
}

/* PHY */
static int lan743x_phy_reset(struct lan743x_adapter *adapter)
{
	u32 data;

	/* Only called with in probe, and before mdiobus_register */

	data = lan743x_csr_read(adapter, PMT_CTL);
	data |= PMT_CTL_ETH_PHY_RST_;
	lan743x_csr_write(adapter, PMT_CTL, data);

	return readx_poll_timeout(LAN743X_CSR_READ_OP, PMT_CTL, data,
				  (!(data & PMT_CTL_ETH_PHY_RST_) &&
				  (data & PMT_CTL_READY_)),
				  50000, 1000000);
}

static void lan743x_phy_update_flowcontrol(struct lan743x_adapter *adapter,
					   u16 local_adv, u16 remote_adv)
{
	struct lan743x_phy *phy = &adapter->phy;
	u8 cap;

	if (phy->fc_autoneg)
		cap = mii_resolve_flowctrl_fdx(local_adv, remote_adv);
	else
		cap = phy->fc_request_control;

	lan743x_mac_flow_ctrl_set_enables(adapter,
					  cap & FLOW_CTRL_TX,
					  cap & FLOW_CTRL_RX);
}

static int lan743x_phy_init(struct lan743x_adapter *adapter)
{
	return lan743x_phy_reset(adapter);
}

static void lan743x_phy_link_status_change(struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	struct phy_device *phydev = netdev->phydev;
	u32 data;

	phy_print_status(phydev);
	if (phydev->state == PHY_RUNNING) {
		int remote_advertisement = 0;
		int local_advertisement = 0;

		data = lan743x_csr_read(adapter, MAC_CR);

		/* set interface mode */
		if (phy_interface_is_rgmii(phydev))
			/* RGMII */
			data &= ~MAC_CR_MII_EN_;
		else
			/* GMII */
			data |= MAC_CR_MII_EN_;

		/* set duplex mode */
		if (phydev->duplex)
			data |= MAC_CR_DPX_;
		else
			data &= ~MAC_CR_DPX_;

		/* set bus speed */
		switch (phydev->speed) {
		case SPEED_10:
			data &= ~MAC_CR_CFG_H_;
			data &= ~MAC_CR_CFG_L_;
		break;
		case SPEED_100:
			data &= ~MAC_CR_CFG_H_;
			data |= MAC_CR_CFG_L_;
		break;
		case SPEED_1000:
			data |= MAC_CR_CFG_H_;
			data &= ~MAC_CR_CFG_L_;
		break;
		}
		lan743x_csr_write(adapter, MAC_CR, data);

		local_advertisement =
			linkmode_adv_to_mii_adv_t(phydev->advertising);
		remote_advertisement =
			linkmode_adv_to_mii_adv_t(phydev->lp_advertising);

		lan743x_phy_update_flowcontrol(adapter, local_advertisement,
					       remote_advertisement);
		lan743x_ptp_update_latency(adapter, phydev->speed);
	}
}

static void lan743x_phy_close(struct lan743x_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;

	phy_stop(netdev->phydev);
	phy_disconnect(netdev->phydev);
	netdev->phydev = NULL;
}

static int lan743x_phy_open(struct lan743x_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	struct lan743x_phy *phy = &adapter->phy;
	struct phy_device *phydev;
	int ret = -EIO;

	/* try devicetree phy, or fixed link */
	phydev = of_phy_get_and_connect(netdev, adapter->pdev->dev.of_node,
					lan743x_phy_link_status_change);

	if (!phydev) {
		/* try internal phy */
		phydev = phy_find_first(adapter->mdiobus);
		if (!phydev)
			goto return_error;

		ret = phy_connect_direct(netdev, phydev,
					 lan743x_phy_link_status_change,
					 PHY_INTERFACE_MODE_GMII);
		if (ret)
			goto return_error;
	}

	/* MAC doesn't support 1000T Half */
	phy_remove_link_mode(phydev, ETHTOOL_LINK_MODE_1000baseT_Half_BIT);

	/* support both flow controls */
	phy_support_asym_pause(phydev);
	phy->fc_request_control = (FLOW_CTRL_RX | FLOW_CTRL_TX);
	phy->fc_autoneg = phydev->autoneg;

	phy_start(phydev);
	phy_start_aneg(phydev);
	phy_attached_info(phydev);
	return 0;

return_error:
	return ret;
}

static void lan743x_rfe_open(struct lan743x_adapter *adapter)
{
	lan743x_csr_write(adapter, RFE_RSS_CFG,
		RFE_RSS_CFG_UDP_IPV6_EX_ |
		RFE_RSS_CFG_TCP_IPV6_EX_ |
		RFE_RSS_CFG_IPV6_EX_ |
		RFE_RSS_CFG_UDP_IPV6_ |
		RFE_RSS_CFG_TCP_IPV6_ |
		RFE_RSS_CFG_IPV6_ |
		RFE_RSS_CFG_UDP_IPV4_ |
		RFE_RSS_CFG_TCP_IPV4_ |
		RFE_RSS_CFG_IPV4_ |
		RFE_RSS_CFG_VALID_HASH_BITS_ |
		RFE_RSS_CFG_RSS_QUEUE_ENABLE_ |
		RFE_RSS_CFG_RSS_HASH_STORE_ |
		RFE_RSS_CFG_RSS_ENABLE_);
}

static void lan743x_rfe_update_mac_address(struct lan743x_adapter *adapter)
{
	u8 *mac_addr;
	u32 mac_addr_hi = 0;
	u32 mac_addr_lo = 0;

	/* Add mac address to perfect Filter */
	mac_addr = adapter->mac_address;
	mac_addr_lo = ((((u32)(mac_addr[0])) << 0) |
		      (((u32)(mac_addr[1])) << 8) |
		      (((u32)(mac_addr[2])) << 16) |
		      (((u32)(mac_addr[3])) << 24));
	mac_addr_hi = ((((u32)(mac_addr[4])) << 0) |
		      (((u32)(mac_addr[5])) << 8));

	lan743x_csr_write(adapter, RFE_ADDR_FILT_LO(0), mac_addr_lo);
	lan743x_csr_write(adapter, RFE_ADDR_FILT_HI(0),
			  mac_addr_hi | RFE_ADDR_FILT_HI_VALID_);
}

static void lan743x_rfe_set_multicast(struct lan743x_adapter *adapter)
{
	struct net_device *netdev = adapter->netdev;
	u32 hash_table[DP_SEL_VHF_HASH_LEN];
	u32 rfctl;
	u32 data;

	rfctl = lan743x_csr_read(adapter, RFE_CTL);
	rfctl &= ~(RFE_CTL_AU_ | RFE_CTL_AM_ |
		 RFE_CTL_DA_PERFECT_ | RFE_CTL_MCAST_HASH_);
	rfctl |= RFE_CTL_AB_;
	if (netdev->flags & IFF_PROMISC) {
		rfctl |= RFE_CTL_AM_ | RFE_CTL_AU_;
	} else {
		if (netdev->flags & IFF_ALLMULTI)
			rfctl |= RFE_CTL_AM_;
	}

	memset(hash_table, 0, DP_SEL_VHF_HASH_LEN * sizeof(u32));
	if (netdev_mc_count(netdev)) {
		struct netdev_hw_addr *ha;
		int i;

		rfctl |= RFE_CTL_DA_PERFECT_;
		i = 1;
		netdev_for_each_mc_addr(ha, netdev) {
			/* set first 32 into Perfect Filter */
			if (i < 33) {
				lan743x_csr_write(adapter,
						  RFE_ADDR_FILT_HI(i), 0);
				data = ha->addr[3];
				data = ha->addr[2] | (data << 8);
				data = ha->addr[1] | (data << 8);
				data = ha->addr[0] | (data << 8);
				lan743x_csr_write(adapter,
						  RFE_ADDR_FILT_LO(i), data);
				data = ha->addr[5];
				data = ha->addr[4] | (data << 8);
				data |= RFE_ADDR_FILT_HI_VALID_;
				lan743x_csr_write(adapter,
						  RFE_ADDR_FILT_HI(i), data);
			} else {
				u32 bitnum = (ether_crc(ETH_ALEN, ha->addr) >>
					     23) & 0x1FF;
				hash_table[bitnum / 32] |= (1 << (bitnum % 32));
				rfctl |= RFE_CTL_MCAST_HASH_;
			}
			i++;
		}
	}

	lan743x_dp_write(adapter, DP_SEL_RFE_RAM,
			 DP_SEL_VHF_VLAN_LEN,
			 DP_SEL_VHF_HASH_LEN, hash_table);
	lan743x_csr_write(adapter, RFE_CTL, rfctl);
}

static int lan743x_dmac_init(struct lan743x_adapter *adapter)
{
	u32 data = 0;

	lan743x_csr_write(adapter, DMAC_CMD, DMAC_CMD_SWR_);
	lan743x_csr_wait_for_bit(adapter, DMAC_CMD, DMAC_CMD_SWR_,
				 0, 1000, 20000, 100);
	switch (DEFAULT_DMA_DESCRIPTOR_SPACING) {
	case DMA_DESCRIPTOR_SPACING_16:
		data = DMAC_CFG_MAX_DSPACE_16_;
		break;
	case DMA_DESCRIPTOR_SPACING_32:
		data = DMAC_CFG_MAX_DSPACE_32_;
		break;
	case DMA_DESCRIPTOR_SPACING_64:
		data = DMAC_CFG_MAX_DSPACE_64_;
		break;
	case DMA_DESCRIPTOR_SPACING_128:
		data = DMAC_CFG_MAX_DSPACE_128_;
		break;
	default:
		return -EPERM;
	}
	if (!(adapter->csr.flags & LAN743X_CSR_FLAG_IS_A0))
		data |= DMAC_CFG_COAL_EN_;
	data |= DMAC_CFG_CH_ARB_SEL_RX_HIGH_;
	data |= DMAC_CFG_MAX_READ_REQ_SET_(6);
	lan743x_csr_write(adapter, DMAC_CFG, data);
	data = DMAC_COAL_CFG_TIMER_LIMIT_SET_(1);
	data |= DMAC_COAL_CFG_TIMER_TX_START_;
	data |= DMAC_COAL_CFG_FLUSH_INTS_;
	data |= DMAC_COAL_CFG_INT_EXIT_COAL_;
	data |= DMAC_COAL_CFG_CSR_EXIT_COAL_;
	data |= DMAC_COAL_CFG_TX_THRES_SET_(0x0A);
	data |= DMAC_COAL_CFG_RX_THRES_SET_(0x0C);
	lan743x_csr_write(adapter, DMAC_COAL_CFG, data);
	data = DMAC_OBFF_TX_THRES_SET_(0x08);
	data |= DMAC_OBFF_RX_THRES_SET_(0x0A);
	lan743x_csr_write(adapter, DMAC_OBFF_CFG, data);
	return 0;
}

static int lan743x_dmac_tx_get_state(struct lan743x_adapter *adapter,
				     int tx_channel)
{
	u32 dmac_cmd = 0;

	dmac_cmd = lan743x_csr_read(adapter, DMAC_CMD);
	return DMAC_CHANNEL_STATE_SET((dmac_cmd &
				      DMAC_CMD_START_T_(tx_channel)),
				      (dmac_cmd &
				      DMAC_CMD_STOP_T_(tx_channel)));
}

static int lan743x_dmac_tx_wait_till_stopped(struct lan743x_adapter *adapter,
					     int tx_channel)
{
	int timeout = 100;
	int result = 0;

	while (timeout &&
	       ((result = lan743x_dmac_tx_get_state(adapter, tx_channel)) ==
	       DMAC_CHANNEL_STATE_STOP_PENDING)) {
		usleep_range(1000, 20000);
		timeout--;
	}
	if (result == DMAC_CHANNEL_STATE_STOP_PENDING)
		result = -ENODEV;
	return result;
}

static int lan743x_dmac_rx_get_state(struct lan743x_adapter *adapter,
				     int rx_channel)
{
	u32 dmac_cmd = 0;

	dmac_cmd = lan743x_csr_read(adapter, DMAC_CMD);
	return DMAC_CHANNEL_STATE_SET((dmac_cmd &
				      DMAC_CMD_START_R_(rx_channel)),
				      (dmac_cmd &
				      DMAC_CMD_STOP_R_(rx_channel)));
}

static int lan743x_dmac_rx_wait_till_stopped(struct lan743x_adapter *adapter,
					     int rx_channel)
{
	int timeout = 100;
	int result = 0;

	while (timeout &&
	       ((result = lan743x_dmac_rx_get_state(adapter, rx_channel)) ==
	       DMAC_CHANNEL_STATE_STOP_PENDING)) {
		usleep_range(1000, 20000);
		timeout--;
	}
	if (result == DMAC_CHANNEL_STATE_STOP_PENDING)
		result = -ENODEV;
	return result;
}

static void lan743x_tx_release_desc(struct lan743x_tx *tx,
				    int descriptor_index, bool cleanup)
{
	struct lan743x_tx_buffer_info *buffer_info = NULL;
	struct lan743x_tx_descriptor *descriptor = NULL;
	u32 descriptor_type = 0;
	bool ignore_sync;

	descriptor = &tx->ring_cpu_ptr[descriptor_index];
	buffer_info = &tx->buffer_info[descriptor_index];
	if (!(buffer_info->flags & TX_BUFFER_INFO_FLAG_ACTIVE))
		goto done;

	descriptor_type = le32_to_cpu(descriptor->data0) &
			  TX_DESC_DATA0_DTYPE_MASK_;
	if (descriptor_type == TX_DESC_DATA0_DTYPE_DATA_)
		goto clean_up_data_descriptor;
	else
		goto clear_active;

clean_up_data_descriptor:
	if (buffer_info->dma_ptr) {
		if (buffer_info->flags &
		    TX_BUFFER_INFO_FLAG_SKB_FRAGMENT) {
			dma_unmap_page(&tx->adapter->pdev->dev,
				       buffer_info->dma_ptr,
				       buffer_info->buffer_length,
				       DMA_TO_DEVICE);
		} else {
			dma_unmap_single(&tx->adapter->pdev->dev,
					 buffer_info->dma_ptr,
					 buffer_info->buffer_length,
					 DMA_TO_DEVICE);
		}
		buffer_info->dma_ptr = 0;
		buffer_info->buffer_length = 0;
	}
	if (!buffer_info->skb)
		goto clear_active;

	if (!(buffer_info->flags & TX_BUFFER_INFO_FLAG_TIMESTAMP_REQUESTED)) {
		dev_kfree_skb_any(buffer_info->skb);
		goto clear_skb;
	}

	if (cleanup) {
		lan743x_ptp_unrequest_tx_timestamp(tx->adapter);
		dev_kfree_skb_any(buffer_info->skb);
	} else {
		ignore_sync = (buffer_info->flags &
			       TX_BUFFER_INFO_FLAG_IGNORE_SYNC) != 0;
		lan743x_ptp_tx_timestamp_skb(tx->adapter,
					     buffer_info->skb, ignore_sync);
	}

clear_skb:
	buffer_info->skb = NULL;

clear_active:
	buffer_info->flags &= ~TX_BUFFER_INFO_FLAG_ACTIVE;

done:
	memset(buffer_info, 0, sizeof(*buffer_info));
	memset(descriptor, 0, sizeof(*descriptor));
}

static int lan743x_tx_next_index(struct lan743x_tx *tx, int index)
{
	return ((++index) % tx->ring_size);
}

static void lan743x_tx_release_completed_descriptors(struct lan743x_tx *tx)
{
	while (le32_to_cpu(*tx->head_cpu_ptr) != (tx->last_head)) {
		lan743x_tx_release_desc(tx, tx->last_head, false);
		tx->last_head = lan743x_tx_next_index(tx, tx->last_head);
	}
}

static void lan743x_tx_release_all_descriptors(struct lan743x_tx *tx)
{
	u32 original_head = 0;

	original_head = tx->last_head;
	do {
		lan743x_tx_release_desc(tx, tx->last_head, true);
		tx->last_head = lan743x_tx_next_index(tx, tx->last_head);
	} while (tx->last_head != original_head);
	memset(tx->ring_cpu_ptr, 0,
	       sizeof(*tx->ring_cpu_ptr) * (tx->ring_size));
	memset(tx->buffer_info, 0,
	       sizeof(*tx->buffer_info) * (tx->ring_size));
}

static int lan743x_tx_get_desc_cnt(struct lan743x_tx *tx,
				   struct sk_buff *skb)
{
	int result = 1; /* 1 for the main skb buffer */
	int nr_frags = 0;

	if (skb_is_gso(skb))
		result++; /* requires an extension descriptor */
	nr_frags = skb_shinfo(skb)->nr_frags;
	result += nr_frags; /* 1 for each fragment buffer */
	return result;
}

static int lan743x_tx_get_avail_desc(struct lan743x_tx *tx)
{
	int last_head = tx->last_head;
	int last_tail = tx->last_tail;

	if (last_tail >= last_head)
		return tx->ring_size - last_tail + last_head - 1;
	else
		return last_head - last_tail - 1;
}

void lan743x_tx_set_timestamping_mode(struct lan743x_tx *tx,
				      bool enable_timestamping,
				      bool enable_onestep_sync)
{
	if (enable_timestamping)
		tx->ts_flags |= TX_TS_FLAG_TIMESTAMPING_ENABLED;
	else
		tx->ts_flags &= ~TX_TS_FLAG_TIMESTAMPING_ENABLED;
	if (enable_onestep_sync)
		tx->ts_flags |= TX_TS_FLAG_ONE_STEP_SYNC;
	else
		tx->ts_flags &= ~TX_TS_FLAG_ONE_STEP_SYNC;
}

static int lan743x_tx_frame_start(struct lan743x_tx *tx,
				  unsigned char *first_buffer,
				  unsigned int first_buffer_length,
				  unsigned int frame_length,
				  bool time_stamp,
				  bool check_sum)
{
	/* called only from within lan743x_tx_xmit_frame.
	 * assuming tx->ring_lock has already been acquired.
	 */
	struct lan743x_tx_descriptor *tx_descriptor = NULL;
	struct lan743x_tx_buffer_info *buffer_info = NULL;
	struct lan743x_adapter *adapter = tx->adapter;
	struct device *dev = &adapter->pdev->dev;
	dma_addr_t dma_ptr;

	tx->frame_flags |= TX_FRAME_FLAG_IN_PROGRESS;
	tx->frame_first = tx->last_tail;
	tx->frame_tail = tx->frame_first;

	tx_descriptor = &tx->ring_cpu_ptr[tx->frame_tail];
	buffer_info = &tx->buffer_info[tx->frame_tail];
	dma_ptr = dma_map_single(dev, first_buffer, first_buffer_length,
				 DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_ptr))
		return -ENOMEM;

	tx_descriptor->data1 = cpu_to_le32(DMA_ADDR_LOW32(dma_ptr));
	tx_descriptor->data2 = cpu_to_le32(DMA_ADDR_HIGH32(dma_ptr));
	tx_descriptor->data3 = cpu_to_le32((frame_length << 16) &
		TX_DESC_DATA3_FRAME_LENGTH_MSS_MASK_);

	buffer_info->skb = NULL;
	buffer_info->dma_ptr = dma_ptr;
	buffer_info->buffer_length = first_buffer_length;
	buffer_info->flags |= TX_BUFFER_INFO_FLAG_ACTIVE;

	tx->frame_data0 = (first_buffer_length &
		TX_DESC_DATA0_BUF_LENGTH_MASK_) |
		TX_DESC_DATA0_DTYPE_DATA_ |
		TX_DESC_DATA0_FS_ |
		TX_DESC_DATA0_FCS_;
	if (time_stamp)
		tx->frame_data0 |= TX_DESC_DATA0_TSE_;

	if (check_sum)
		tx->frame_data0 |= TX_DESC_DATA0_ICE_ |
				   TX_DESC_DATA0_IPE_ |
				   TX_DESC_DATA0_TPE_;

	/* data0 will be programmed in one of other frame assembler functions */
	return 0;
}

static void lan743x_tx_frame_add_lso(struct lan743x_tx *tx,
				     unsigned int frame_length,
				     int nr_frags)
{
	/* called only from within lan743x_tx_xmit_frame.
	 * assuming tx->ring_lock has already been acquired.
	 */
	struct lan743x_tx_descriptor *tx_descriptor = NULL;
	struct lan743x_tx_buffer_info *buffer_info = NULL;

	/* wrap up previous descriptor */
	tx->frame_data0 |= TX_DESC_DATA0_EXT_;
	if (nr_frags <= 0) {
		tx->frame_data0 |= TX_DESC_DATA0_LS_;
		tx->frame_data0 |= TX_DESC_DATA0_IOC_;
	}
	tx_descriptor = &tx->ring_cpu_ptr[tx->frame_tail];
	tx_descriptor->data0 = cpu_to_le32(tx->frame_data0);

	/* move to next descriptor */
	tx->frame_tail = lan743x_tx_next_index(tx, tx->frame_tail);
	tx_descriptor = &tx->ring_cpu_ptr[tx->frame_tail];
	buffer_info = &tx->buffer_info[tx->frame_tail];

	/* add extension descriptor */
	tx_descriptor->data1 = 0;
	tx_descriptor->data2 = 0;
	tx_descriptor->data3 = 0;

	buffer_info->skb = NULL;
	buffer_info->dma_ptr = 0;
	buffer_info->buffer_length = 0;
	buffer_info->flags |= TX_BUFFER_INFO_FLAG_ACTIVE;

	tx->frame_data0 = (frame_length & TX_DESC_DATA0_EXT_PAY_LENGTH_MASK_) |
			  TX_DESC_DATA0_DTYPE_EXT_ |
			  TX_DESC_DATA0_EXT_LSO_;

	/* data0 will be programmed in one of other frame assembler functions */
}

static int lan743x_tx_frame_add_fragment(struct lan743x_tx *tx,
					 const skb_frag_t *fragment,
					 unsigned int frame_length)
{
	/* called only from within lan743x_tx_xmit_frame
	 * assuming tx->ring_lock has already been acquired
	 */
	struct lan743x_tx_descriptor *tx_descriptor = NULL;
	struct lan743x_tx_buffer_info *buffer_info = NULL;
	struct lan743x_adapter *adapter = tx->adapter;
	struct device *dev = &adapter->pdev->dev;
	unsigned int fragment_length = 0;
	dma_addr_t dma_ptr;

	fragment_length = skb_frag_size(fragment);
	if (!fragment_length)
		return 0;

	/* wrap up previous descriptor */
	tx_descriptor = &tx->ring_cpu_ptr[tx->frame_tail];
	tx_descriptor->data0 = cpu_to_le32(tx->frame_data0);

	/* move to next descriptor */
	tx->frame_tail = lan743x_tx_next_index(tx, tx->frame_tail);
	tx_descriptor = &tx->ring_cpu_ptr[tx->frame_tail];
	buffer_info = &tx->buffer_info[tx->frame_tail];
	dma_ptr = skb_frag_dma_map(dev, fragment,
				   0, fragment_length,
				   DMA_TO_DEVICE);
	if (dma_mapping_error(dev, dma_ptr)) {
		int desc_index;

		/* cleanup all previously setup descriptors */
		desc_index = tx->frame_first;
		while (desc_index != tx->frame_tail) {
			lan743x_tx_release_desc(tx, desc_index, true);
			desc_index = lan743x_tx_next_index(tx, desc_index);
		}
		dma_wmb();
		tx->frame_flags &= ~TX_FRAME_FLAG_IN_PROGRESS;
		tx->frame_first = 0;
		tx->frame_data0 = 0;
		tx->frame_tail = 0;
		return -ENOMEM;
	}

	tx_descriptor->data1 = cpu_to_le32(DMA_ADDR_LOW32(dma_ptr));
	tx_descriptor->data2 = cpu_to_le32(DMA_ADDR_HIGH32(dma_ptr));
	tx_descriptor->data3 = cpu_to_le32((frame_length << 16) &
			       TX_DESC_DATA3_FRAME_LENGTH_MSS_MASK_);

	buffer_info->skb = NULL;
	buffer_info->dma_ptr = dma_ptr;
	buffer_info->buffer_length = fragment_length;
	buffer_info->flags |= TX_BUFFER_INFO_FLAG_ACTIVE;
	buffer_info->flags |= TX_BUFFER_INFO_FLAG_SKB_FRAGMENT;

	tx->frame_data0 = (fragment_length & TX_DESC_DATA0_BUF_LENGTH_MASK_) |
			  TX_DESC_DATA0_DTYPE_DATA_ |
			  TX_DESC_DATA0_FCS_;

	/* data0 will be programmed in one of other frame assembler functions */
	return 0;
}

static void lan743x_tx_frame_end(struct lan743x_tx *tx,
				 struct sk_buff *skb,
				 bool time_stamp,
				 bool ignore_sync)
{
	/* called only from within lan743x_tx_xmit_frame
	 * assuming tx->ring_lock has already been acquired
	 */
	struct lan743x_tx_descriptor *tx_descriptor = NULL;
	struct lan743x_tx_buffer_info *buffer_info = NULL;
	struct lan743x_adapter *adapter = tx->adapter;
	u32 tx_tail_flags = 0;

	/* wrap up previous descriptor */
	if ((tx->frame_data0 & TX_DESC_DATA0_DTYPE_MASK_) ==
	    TX_DESC_DATA0_DTYPE_DATA_) {
		tx->frame_data0 |= TX_DESC_DATA0_LS_;
		tx->frame_data0 |= TX_DESC_DATA0_IOC_;
	}

	tx_descriptor = &tx->ring_cpu_ptr[tx->frame_tail];
	buffer_info = &tx->buffer_info[tx->frame_tail];
	buffer_info->skb = skb;
	if (time_stamp)
		buffer_info->flags |= TX_BUFFER_INFO_FLAG_TIMESTAMP_REQUESTED;
	if (ignore_sync)
		buffer_info->flags |= TX_BUFFER_INFO_FLAG_IGNORE_SYNC;

	tx_descriptor->data0 = cpu_to_le32(tx->frame_data0);
	tx->frame_tail = lan743x_tx_next_index(tx, tx->frame_tail);
	tx->last_tail = tx->frame_tail;

	dma_wmb();

	if (tx->vector_flags & LAN743X_VECTOR_FLAG_VECTOR_ENABLE_AUTO_SET)
		tx_tail_flags |= TX_TAIL_SET_TOP_INT_VEC_EN_;
	if (tx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_AUTO_SET)
		tx_tail_flags |= TX_TAIL_SET_DMAC_INT_EN_ |
		TX_TAIL_SET_TOP_INT_EN_;

	lan743x_csr_write(adapter, TX_TAIL(tx->channel_number),
			  tx_tail_flags | tx->frame_tail);
	tx->frame_flags &= ~TX_FRAME_FLAG_IN_PROGRESS;
}

static netdev_tx_t lan743x_tx_xmit_frame(struct lan743x_tx *tx,
					 struct sk_buff *skb)
{
	int required_number_of_descriptors = 0;
	unsigned int start_frame_length = 0;
	unsigned int frame_length = 0;
	unsigned int head_length = 0;
	unsigned long irq_flags = 0;
	bool do_timestamp = false;
	bool ignore_sync = false;
	int nr_frags = 0;
	bool gso = false;
	int j;

	required_number_of_descriptors = lan743x_tx_get_desc_cnt(tx, skb);

	spin_lock_irqsave(&tx->ring_lock, irq_flags);
	if (required_number_of_descriptors >
		lan743x_tx_get_avail_desc(tx)) {
		if (required_number_of_descriptors > (tx->ring_size - 1)) {
			dev_kfree_skb_irq(skb);
		} else {
			/* save to overflow buffer */
			tx->overflow_skb = skb;
			netif_stop_queue(tx->adapter->netdev);
		}
		goto unlock;
	}

	/* space available, transmit skb  */
	if ((skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
	    (tx->ts_flags & TX_TS_FLAG_TIMESTAMPING_ENABLED) &&
	    (lan743x_ptp_request_tx_timestamp(tx->adapter))) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		do_timestamp = true;
		if (tx->ts_flags & TX_TS_FLAG_ONE_STEP_SYNC)
			ignore_sync = true;
	}
	head_length = skb_headlen(skb);
	frame_length = skb_pagelen(skb);
	nr_frags = skb_shinfo(skb)->nr_frags;
	start_frame_length = frame_length;
	gso = skb_is_gso(skb);
	if (gso) {
		start_frame_length = max(skb_shinfo(skb)->gso_size,
					 (unsigned short)8);
	}

	if (lan743x_tx_frame_start(tx,
				   skb->data, head_length,
				   start_frame_length,
				   do_timestamp,
				   skb->ip_summed == CHECKSUM_PARTIAL)) {
		dev_kfree_skb_irq(skb);
		goto unlock;
	}

	if (gso)
		lan743x_tx_frame_add_lso(tx, frame_length, nr_frags);

	if (nr_frags <= 0)
		goto finish;

	for (j = 0; j < nr_frags; j++) {
		const skb_frag_t *frag = &(skb_shinfo(skb)->frags[j]);

		if (lan743x_tx_frame_add_fragment(tx, frag, frame_length)) {
			/* upon error no need to call
			 *	lan743x_tx_frame_end
			 * frame assembler clean up was performed inside
			 *	lan743x_tx_frame_add_fragment
			 */
			dev_kfree_skb_irq(skb);
			goto unlock;
		}
	}

finish:
	lan743x_tx_frame_end(tx, skb, do_timestamp, ignore_sync);

unlock:
	spin_unlock_irqrestore(&tx->ring_lock, irq_flags);
	return NETDEV_TX_OK;
}

static int lan743x_tx_napi_poll(struct napi_struct *napi, int weight)
{
	struct lan743x_tx *tx = container_of(napi, struct lan743x_tx, napi);
	struct lan743x_adapter *adapter = tx->adapter;
	bool start_transmitter = false;
	unsigned long irq_flags = 0;
	u32 ioc_bit = 0;

	ioc_bit = DMAC_INT_BIT_TX_IOC_(tx->channel_number);
	lan743x_csr_read(adapter, DMAC_INT_STS);
	if (tx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_STATUS_W2C)
		lan743x_csr_write(adapter, DMAC_INT_STS, ioc_bit);
	spin_lock_irqsave(&tx->ring_lock, irq_flags);

	/* clean up tx ring */
	lan743x_tx_release_completed_descriptors(tx);
	if (netif_queue_stopped(adapter->netdev)) {
		if (tx->overflow_skb) {
			if (lan743x_tx_get_desc_cnt(tx, tx->overflow_skb) <=
				lan743x_tx_get_avail_desc(tx))
				start_transmitter = true;
		} else {
			netif_wake_queue(adapter->netdev);
		}
	}
	spin_unlock_irqrestore(&tx->ring_lock, irq_flags);

	if (start_transmitter) {
		/* space is now available, transmit overflow skb */
		lan743x_tx_xmit_frame(tx, tx->overflow_skb);
		tx->overflow_skb = NULL;
		netif_wake_queue(adapter->netdev);
	}

	if (!napi_complete(napi))
		goto done;

	/* enable isr */
	lan743x_csr_write(adapter, INT_EN_SET,
			  INT_BIT_DMA_TX_(tx->channel_number));
	lan743x_csr_read(adapter, INT_STS);

done:
	return 0;
}

static void lan743x_tx_ring_cleanup(struct lan743x_tx *tx)
{
	if (tx->head_cpu_ptr) {
		dma_free_coherent(&tx->adapter->pdev->dev,
				  sizeof(*tx->head_cpu_ptr), tx->head_cpu_ptr,
				  tx->head_dma_ptr);
		tx->head_cpu_ptr = NULL;
		tx->head_dma_ptr = 0;
	}
	kfree(tx->buffer_info);
	tx->buffer_info = NULL;

	if (tx->ring_cpu_ptr) {
		dma_free_coherent(&tx->adapter->pdev->dev,
				  tx->ring_allocation_size, tx->ring_cpu_ptr,
				  tx->ring_dma_ptr);
		tx->ring_allocation_size = 0;
		tx->ring_cpu_ptr = NULL;
		tx->ring_dma_ptr = 0;
	}
	tx->ring_size = 0;
}

static int lan743x_tx_ring_init(struct lan743x_tx *tx)
{
	size_t ring_allocation_size = 0;
	void *cpu_ptr = NULL;
	dma_addr_t dma_ptr;
	int ret = -ENOMEM;

	tx->ring_size = LAN743X_TX_RING_SIZE;
	if (tx->ring_size & ~TX_CFG_B_TX_RING_LEN_MASK_) {
		ret = -EINVAL;
		goto cleanup;
	}
	if (dma_set_mask_and_coherent(&tx->adapter->pdev->dev,
				      DMA_BIT_MASK(64))) {
		dev_warn(&tx->adapter->pdev->dev,
			 "lan743x_: No suitable DMA available\n");
		ret = -ENOMEM;
		goto cleanup;
	}
	ring_allocation_size = ALIGN(tx->ring_size *
				     sizeof(struct lan743x_tx_descriptor),
				     PAGE_SIZE);
	dma_ptr = 0;
	cpu_ptr = dma_alloc_coherent(&tx->adapter->pdev->dev,
				     ring_allocation_size, &dma_ptr, GFP_KERNEL);
	if (!cpu_ptr) {
		ret = -ENOMEM;
		goto cleanup;
	}

	tx->ring_allocation_size = ring_allocation_size;
	tx->ring_cpu_ptr = (struct lan743x_tx_descriptor *)cpu_ptr;
	tx->ring_dma_ptr = dma_ptr;

	cpu_ptr = kcalloc(tx->ring_size, sizeof(*tx->buffer_info), GFP_KERNEL);
	if (!cpu_ptr) {
		ret = -ENOMEM;
		goto cleanup;
	}
	tx->buffer_info = (struct lan743x_tx_buffer_info *)cpu_ptr;
	dma_ptr = 0;
	cpu_ptr = dma_alloc_coherent(&tx->adapter->pdev->dev,
				     sizeof(*tx->head_cpu_ptr), &dma_ptr,
				     GFP_KERNEL);
	if (!cpu_ptr) {
		ret = -ENOMEM;
		goto cleanup;
	}

	tx->head_cpu_ptr = cpu_ptr;
	tx->head_dma_ptr = dma_ptr;
	if (tx->head_dma_ptr & 0x3) {
		ret = -ENOMEM;
		goto cleanup;
	}

	return 0;

cleanup:
	lan743x_tx_ring_cleanup(tx);
	return ret;
}

static void lan743x_tx_close(struct lan743x_tx *tx)
{
	struct lan743x_adapter *adapter = tx->adapter;

	lan743x_csr_write(adapter,
			  DMAC_CMD,
			  DMAC_CMD_STOP_T_(tx->channel_number));
	lan743x_dmac_tx_wait_till_stopped(adapter, tx->channel_number);

	lan743x_csr_write(adapter,
			  DMAC_INT_EN_CLR,
			  DMAC_INT_BIT_TX_IOC_(tx->channel_number));
	lan743x_csr_write(adapter, INT_EN_CLR,
			  INT_BIT_DMA_TX_(tx->channel_number));
	napi_disable(&tx->napi);
	netif_napi_del(&tx->napi);

	lan743x_csr_write(adapter, FCT_TX_CTL,
			  FCT_TX_CTL_DIS_(tx->channel_number));
	lan743x_csr_wait_for_bit(adapter, FCT_TX_CTL,
				 FCT_TX_CTL_EN_(tx->channel_number),
				 0, 1000, 20000, 100);

	lan743x_tx_release_all_descriptors(tx);

	if (tx->overflow_skb) {
		dev_kfree_skb(tx->overflow_skb);
		tx->overflow_skb = NULL;
	}

	lan743x_tx_ring_cleanup(tx);
}

static int lan743x_tx_open(struct lan743x_tx *tx)
{
	struct lan743x_adapter *adapter = NULL;
	u32 data = 0;
	int ret;

	adapter = tx->adapter;
	ret = lan743x_tx_ring_init(tx);
	if (ret)
		return ret;

	/* initialize fifo */
	lan743x_csr_write(adapter, FCT_TX_CTL,
			  FCT_TX_CTL_RESET_(tx->channel_number));
	lan743x_csr_wait_for_bit(adapter, FCT_TX_CTL,
				 FCT_TX_CTL_RESET_(tx->channel_number),
				 0, 1000, 20000, 100);

	/* enable fifo */
	lan743x_csr_write(adapter, FCT_TX_CTL,
			  FCT_TX_CTL_EN_(tx->channel_number));

	/* reset tx channel */
	lan743x_csr_write(adapter, DMAC_CMD,
			  DMAC_CMD_TX_SWR_(tx->channel_number));
	lan743x_csr_wait_for_bit(adapter, DMAC_CMD,
				 DMAC_CMD_TX_SWR_(tx->channel_number),
				 0, 1000, 20000, 100);

	/* Write TX_BASE_ADDR */
	lan743x_csr_write(adapter,
			  TX_BASE_ADDRH(tx->channel_number),
			  DMA_ADDR_HIGH32(tx->ring_dma_ptr));
	lan743x_csr_write(adapter,
			  TX_BASE_ADDRL(tx->channel_number),
			  DMA_ADDR_LOW32(tx->ring_dma_ptr));

	/* Write TX_CFG_B */
	data = lan743x_csr_read(adapter, TX_CFG_B(tx->channel_number));
	data &= ~TX_CFG_B_TX_RING_LEN_MASK_;
	data |= ((tx->ring_size) & TX_CFG_B_TX_RING_LEN_MASK_);
	if (!(adapter->csr.flags & LAN743X_CSR_FLAG_IS_A0))
		data |= TX_CFG_B_TDMABL_512_;
	lan743x_csr_write(adapter, TX_CFG_B(tx->channel_number), data);

	/* Write TX_CFG_A */
	data = TX_CFG_A_TX_TMR_HPWB_SEL_IOC_ | TX_CFG_A_TX_HP_WB_EN_;
	if (!(adapter->csr.flags & LAN743X_CSR_FLAG_IS_A0)) {
		data |= TX_CFG_A_TX_HP_WB_ON_INT_TMR_;
		data |= TX_CFG_A_TX_PF_THRES_SET_(0x10);
		data |= TX_CFG_A_TX_PF_PRI_THRES_SET_(0x04);
		data |= TX_CFG_A_TX_HP_WB_THRES_SET_(0x07);
	}
	lan743x_csr_write(adapter, TX_CFG_A(tx->channel_number), data);

	/* Write TX_HEAD_WRITEBACK_ADDR */
	lan743x_csr_write(adapter,
			  TX_HEAD_WRITEBACK_ADDRH(tx->channel_number),
			  DMA_ADDR_HIGH32(tx->head_dma_ptr));
	lan743x_csr_write(adapter,
			  TX_HEAD_WRITEBACK_ADDRL(tx->channel_number),
			  DMA_ADDR_LOW32(tx->head_dma_ptr));

	/* set last head */
	tx->last_head = lan743x_csr_read(adapter, TX_HEAD(tx->channel_number));

	/* write TX_TAIL */
	tx->last_tail = 0;
	lan743x_csr_write(adapter, TX_TAIL(tx->channel_number),
			  (u32)(tx->last_tail));
	tx->vector_flags = lan743x_intr_get_vector_flags(adapter,
							 INT_BIT_DMA_TX_
							 (tx->channel_number));
	netif_tx_napi_add(adapter->netdev,
			  &tx->napi, lan743x_tx_napi_poll,
			  tx->ring_size - 1);
	napi_enable(&tx->napi);

	data = 0;
	if (tx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_AUTO_CLEAR)
		data |= TX_CFG_C_TX_TOP_INT_EN_AUTO_CLR_;
	if (tx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_STATUS_AUTO_CLEAR)
		data |= TX_CFG_C_TX_DMA_INT_STS_AUTO_CLR_;
	if (tx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_STATUS_R2C)
		data |= TX_CFG_C_TX_INT_STS_R2C_MODE_MASK_;
	if (tx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_R2C)
		data |= TX_CFG_C_TX_INT_EN_R2C_;
	lan743x_csr_write(adapter, TX_CFG_C(tx->channel_number), data);

	if (!(tx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_AUTO_SET))
		lan743x_csr_write(adapter, INT_EN_SET,
				  INT_BIT_DMA_TX_(tx->channel_number));
	lan743x_csr_write(adapter, DMAC_INT_EN_SET,
			  DMAC_INT_BIT_TX_IOC_(tx->channel_number));

	/*  start dmac channel */
	lan743x_csr_write(adapter, DMAC_CMD,
			  DMAC_CMD_START_T_(tx->channel_number));
	return 0;
}

static int lan743x_rx_next_index(struct lan743x_rx *rx, int index)
{
	return ((++index) % rx->ring_size);
}

static void lan743x_rx_update_tail(struct lan743x_rx *rx, int index)
{
	/* update the tail once per 8 descriptors */
	if ((index & 7) == 7)
		lan743x_csr_write(rx->adapter, RX_TAIL(rx->channel_number),
				  index);
}

static int lan743x_rx_init_ring_element(struct lan743x_rx *rx, int index,
					gfp_t gfp)
{
	struct net_device *netdev = rx->adapter->netdev;
	struct device *dev = &rx->adapter->pdev->dev;
	struct lan743x_rx_buffer_info *buffer_info;
	unsigned int buffer_length, used_length;
	struct lan743x_rx_descriptor *descriptor;
	struct sk_buff *skb;
	dma_addr_t dma_ptr;

	buffer_length = netdev->mtu + ETH_HLEN + ETH_FCS_LEN + RX_HEAD_PADDING;

	descriptor = &rx->ring_cpu_ptr[index];
	buffer_info = &rx->buffer_info[index];
	skb = __netdev_alloc_skb(netdev, buffer_length, gfp);
	if (!skb)
		return -ENOMEM;
	dma_ptr = dma_map_single(dev, skb->data, buffer_length, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dma_ptr)) {
		dev_kfree_skb_any(skb);
		return -ENOMEM;
	}
	if (buffer_info->dma_ptr) {
		/* sync used area of buffer only */
		if (le32_to_cpu(descriptor->data0) & RX_DESC_DATA0_LS_)
			/* frame length is valid only if LS bit is set.
			 * it's a safe upper bound for the used area in this
			 * buffer.
			 */
			used_length = min(RX_DESC_DATA0_FRAME_LENGTH_GET_
					  (le32_to_cpu(descriptor->data0)),
					  buffer_info->buffer_length);
		else
			used_length = buffer_info->buffer_length;
		dma_sync_single_for_cpu(dev, buffer_info->dma_ptr,
					used_length,
					DMA_FROM_DEVICE);
		dma_unmap_single_attrs(dev, buffer_info->dma_ptr,
				       buffer_info->buffer_length,
				       DMA_FROM_DEVICE,
				       DMA_ATTR_SKIP_CPU_SYNC);
	}

	buffer_info->skb = skb;
	buffer_info->dma_ptr = dma_ptr;
	buffer_info->buffer_length = buffer_length;
	descriptor->data1 = cpu_to_le32(DMA_ADDR_LOW32(buffer_info->dma_ptr));
	descriptor->data2 = cpu_to_le32(DMA_ADDR_HIGH32(buffer_info->dma_ptr));
	descriptor->data3 = 0;
	descriptor->data0 = cpu_to_le32((RX_DESC_DATA0_OWN_ |
			    (buffer_length & RX_DESC_DATA0_BUF_LENGTH_MASK_)));
	lan743x_rx_update_tail(rx, index);

	return 0;
}

static void lan743x_rx_reuse_ring_element(struct lan743x_rx *rx, int index)
{
	struct lan743x_rx_buffer_info *buffer_info;
	struct lan743x_rx_descriptor *descriptor;

	descriptor = &rx->ring_cpu_ptr[index];
	buffer_info = &rx->buffer_info[index];

	descriptor->data1 = cpu_to_le32(DMA_ADDR_LOW32(buffer_info->dma_ptr));
	descriptor->data2 = cpu_to_le32(DMA_ADDR_HIGH32(buffer_info->dma_ptr));
	descriptor->data3 = 0;
	descriptor->data0 = cpu_to_le32((RX_DESC_DATA0_OWN_ |
			    ((buffer_info->buffer_length) &
			    RX_DESC_DATA0_BUF_LENGTH_MASK_)));
	lan743x_rx_update_tail(rx, index);
}

static void lan743x_rx_release_ring_element(struct lan743x_rx *rx, int index)
{
	struct lan743x_rx_buffer_info *buffer_info;
	struct lan743x_rx_descriptor *descriptor;

	descriptor = &rx->ring_cpu_ptr[index];
	buffer_info = &rx->buffer_info[index];

	memset(descriptor, 0, sizeof(*descriptor));

	if (buffer_info->dma_ptr) {
		dma_unmap_single(&rx->adapter->pdev->dev,
				 buffer_info->dma_ptr,
				 buffer_info->buffer_length,
				 DMA_FROM_DEVICE);
		buffer_info->dma_ptr = 0;
	}

	if (buffer_info->skb) {
		dev_kfree_skb(buffer_info->skb);
		buffer_info->skb = NULL;
	}

	memset(buffer_info, 0, sizeof(*buffer_info));
}

static struct sk_buff *
lan743x_rx_trim_skb(struct sk_buff *skb, int frame_length)
{
	if (skb_linearize(skb)) {
		dev_kfree_skb_irq(skb);
		return NULL;
	}
	frame_length = max_t(int, 0, frame_length - ETH_FCS_LEN);
	if (skb->len > frame_length) {
		skb->tail -= skb->len - frame_length;
		skb->len = frame_length;
	}
	return skb;
}

static int lan743x_rx_process_buffer(struct lan743x_rx *rx)
{
	int current_head_index = le32_to_cpu(*rx->head_cpu_ptr);
	struct lan743x_rx_descriptor *descriptor, *desc_ext;
	struct net_device *netdev = rx->adapter->netdev;
	int result = RX_PROCESS_RESULT_NOTHING_TO_DO;
	struct lan743x_rx_buffer_info *buffer_info;
	int frame_length, buffer_length;
	int extension_index = -1;
	bool is_last, is_first;
	struct sk_buff *skb;

	if (current_head_index < 0 || current_head_index >= rx->ring_size)
		goto done;

	if (rx->last_head < 0 || rx->last_head >= rx->ring_size)
		goto done;

	if (rx->last_head == current_head_index)
		goto done;

	descriptor = &rx->ring_cpu_ptr[rx->last_head];
	if (le32_to_cpu(descriptor->data0) & RX_DESC_DATA0_OWN_)
		goto done;
	buffer_info = &rx->buffer_info[rx->last_head];

	is_last = le32_to_cpu(descriptor->data0) & RX_DESC_DATA0_LS_;
	is_first = le32_to_cpu(descriptor->data0) & RX_DESC_DATA0_FS_;

	if (is_last && le32_to_cpu(descriptor->data0) & RX_DESC_DATA0_EXT_) {
		/* extension is expected to follow */
		int index = lan743x_rx_next_index(rx, rx->last_head);

		if (index == current_head_index)
			/* extension not yet available */
			goto done;
		desc_ext = &rx->ring_cpu_ptr[index];
		if (le32_to_cpu(desc_ext->data0) & RX_DESC_DATA0_OWN_)
			/* extension not yet available */
			goto done;
		if (!(le32_to_cpu(desc_ext->data0) & RX_DESC_DATA0_EXT_))
			goto move_forward;
		extension_index = index;
	}

	/* Only the last buffer in a multi-buffer frame contains the total frame
	 * length. The chip occasionally sends more buffers than strictly
	 * required to reach the total frame length.
	 * Handle this by adding all buffers to the skb in their entirety.
	 * Once the real frame length is known, trim the skb.
	 */
	frame_length =
		RX_DESC_DATA0_FRAME_LENGTH_GET_(le32_to_cpu(descriptor->data0));
	buffer_length = buffer_info->buffer_length;

	netdev_dbg(netdev, "%s%schunk: %d/%d",
		   is_first ? "first " : "      ",
		   is_last  ? "last  " : "      ",
		   frame_length, buffer_length);

	/* save existing skb, allocate new skb and map to dma */
	skb = buffer_info->skb;
	if (lan743x_rx_init_ring_element(rx, rx->last_head,
					 GFP_ATOMIC | GFP_DMA)) {
		/* failed to allocate next skb.
		 * Memory is very low.
		 * Drop this packet and reuse buffer.
		 */
		lan743x_rx_reuse_ring_element(rx, rx->last_head);
		/* drop packet that was being assembled */
		dev_kfree_skb_irq(rx->skb_head);
		rx->skb_head = NULL;
		goto process_extension;
	}

	/* add buffers to skb via skb->frag_list */
	if (is_first) {
		skb_reserve(skb, RX_HEAD_PADDING);
		skb_put(skb, buffer_length - RX_HEAD_PADDING);
		if (rx->skb_head)
			dev_kfree_skb_irq(rx->skb_head);
		rx->skb_head = skb;
	} else if (rx->skb_head) {
		skb_put(skb, buffer_length);
		if (skb_shinfo(rx->skb_head)->frag_list)
			rx->skb_tail->next = skb;
		else
			skb_shinfo(rx->skb_head)->frag_list = skb;
		rx->skb_tail = skb;
		rx->skb_head->len += skb->len;
		rx->skb_head->data_len += skb->len;
		rx->skb_head->truesize += skb->truesize;
	} else {
		/* packet to assemble has already been dropped because one or
		 * more of its buffers could not be allocated
		 */
		netdev_dbg(netdev, "drop buffer intended for dropped packet");
		dev_kfree_skb_irq(skb);
	}

process_extension:
	if (extension_index >= 0) {
		u32 ts_sec;
		u32 ts_nsec;

		ts_sec = le32_to_cpu(desc_ext->data1);
		ts_nsec = (le32_to_cpu(desc_ext->data2) &
			  RX_DESC_DATA2_TS_NS_MASK_);
		if (rx->skb_head)
			skb_hwtstamps(rx->skb_head)->hwtstamp =
				ktime_set(ts_sec, ts_nsec);
		lan743x_rx_reuse_ring_element(rx, extension_index);
		rx->last_head = extension_index;
		netdev_dbg(netdev, "process extension");
	}

	if (is_last && rx->skb_head)
		rx->skb_head = lan743x_rx_trim_skb(rx->skb_head, frame_length);

	if (is_last && rx->skb_head) {
		rx->skb_head->protocol = eth_type_trans(rx->skb_head,
							rx->adapter->netdev);
		netdev_dbg(netdev, "sending %d byte frame to OS",
			   rx->skb_head->len);
		napi_gro_receive(&rx->napi, rx->skb_head);
		rx->skb_head = NULL;
	}

move_forward:
	/* push tail and head forward */
	rx->last_tail = rx->last_head;
	rx->last_head = lan743x_rx_next_index(rx, rx->last_head);
	result = RX_PROCESS_RESULT_BUFFER_RECEIVED;
done:
	return result;
}

static int lan743x_rx_napi_poll(struct napi_struct *napi, int weight)
{
	struct lan743x_rx *rx = container_of(napi, struct lan743x_rx, napi);
	struct lan743x_adapter *adapter = rx->adapter;
	int result = RX_PROCESS_RESULT_NOTHING_TO_DO;
	u32 rx_tail_flags = 0;
	int count;

	if (rx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_STATUS_W2C) {
		/* clear int status bit before reading packet */
		lan743x_csr_write(adapter, DMAC_INT_STS,
				  DMAC_INT_BIT_RXFRM_(rx->channel_number));
	}
	for (count = 0; count < weight; count++) {
		result = lan743x_rx_process_buffer(rx);
		if (result == RX_PROCESS_RESULT_NOTHING_TO_DO)
			break;
	}
	rx->frame_count += count;
	if (count == weight || result == RX_PROCESS_RESULT_BUFFER_RECEIVED)
		return weight;

	if (!napi_complete_done(napi, count))
		return count;

	/* re-arm interrupts, must write to rx tail on some chip variants */
	if (rx->vector_flags & LAN743X_VECTOR_FLAG_VECTOR_ENABLE_AUTO_SET)
		rx_tail_flags |= RX_TAIL_SET_TOP_INT_VEC_EN_;
	if (rx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_AUTO_SET) {
		rx_tail_flags |= RX_TAIL_SET_TOP_INT_EN_;
	} else {
		lan743x_csr_write(adapter, INT_EN_SET,
				  INT_BIT_DMA_RX_(rx->channel_number));
	}

	if (rx_tail_flags)
		lan743x_csr_write(adapter, RX_TAIL(rx->channel_number),
				  rx_tail_flags | rx->last_tail);

	return count;
}

static void lan743x_rx_ring_cleanup(struct lan743x_rx *rx)
{
	if (rx->buffer_info && rx->ring_cpu_ptr) {
		int index;

		for (index = 0; index < rx->ring_size; index++)
			lan743x_rx_release_ring_element(rx, index);
	}

	if (rx->head_cpu_ptr) {
		dma_free_coherent(&rx->adapter->pdev->dev,
				  sizeof(*rx->head_cpu_ptr), rx->head_cpu_ptr,
				  rx->head_dma_ptr);
		rx->head_cpu_ptr = NULL;
		rx->head_dma_ptr = 0;
	}

	kfree(rx->buffer_info);
	rx->buffer_info = NULL;

	if (rx->ring_cpu_ptr) {
		dma_free_coherent(&rx->adapter->pdev->dev,
				  rx->ring_allocation_size, rx->ring_cpu_ptr,
				  rx->ring_dma_ptr);
		rx->ring_allocation_size = 0;
		rx->ring_cpu_ptr = NULL;
		rx->ring_dma_ptr = 0;
	}

	rx->ring_size = 0;
	rx->last_head = 0;
}

static int lan743x_rx_ring_init(struct lan743x_rx *rx)
{
	size_t ring_allocation_size = 0;
	dma_addr_t dma_ptr = 0;
	void *cpu_ptr = NULL;
	int ret = -ENOMEM;
	int index = 0;

	rx->ring_size = LAN743X_RX_RING_SIZE;
	if (rx->ring_size <= 1) {
		ret = -EINVAL;
		goto cleanup;
	}
	if (rx->ring_size & ~RX_CFG_B_RX_RING_LEN_MASK_) {
		ret = -EINVAL;
		goto cleanup;
	}
	if (dma_set_mask_and_coherent(&rx->adapter->pdev->dev,
				      DMA_BIT_MASK(64))) {
		dev_warn(&rx->adapter->pdev->dev,
			 "lan743x_: No suitable DMA available\n");
		ret = -ENOMEM;
		goto cleanup;
	}
	ring_allocation_size = ALIGN(rx->ring_size *
				     sizeof(struct lan743x_rx_descriptor),
				     PAGE_SIZE);
	dma_ptr = 0;
	cpu_ptr = dma_alloc_coherent(&rx->adapter->pdev->dev,
				     ring_allocation_size, &dma_ptr, GFP_KERNEL);
	if (!cpu_ptr) {
		ret = -ENOMEM;
		goto cleanup;
	}
	rx->ring_allocation_size = ring_allocation_size;
	rx->ring_cpu_ptr = (struct lan743x_rx_descriptor *)cpu_ptr;
	rx->ring_dma_ptr = dma_ptr;

	cpu_ptr = kcalloc(rx->ring_size, sizeof(*rx->buffer_info),
			  GFP_KERNEL);
	if (!cpu_ptr) {
		ret = -ENOMEM;
		goto cleanup;
	}
	rx->buffer_info = (struct lan743x_rx_buffer_info *)cpu_ptr;
	dma_ptr = 0;
	cpu_ptr = dma_alloc_coherent(&rx->adapter->pdev->dev,
				     sizeof(*rx->head_cpu_ptr), &dma_ptr,
				     GFP_KERNEL);
	if (!cpu_ptr) {
		ret = -ENOMEM;
		goto cleanup;
	}

	rx->head_cpu_ptr = cpu_ptr;
	rx->head_dma_ptr = dma_ptr;
	if (rx->head_dma_ptr & 0x3) {
		ret = -ENOMEM;
		goto cleanup;
	}

	rx->last_head = 0;
	for (index = 0; index < rx->ring_size; index++) {
		ret = lan743x_rx_init_ring_element(rx, index, GFP_KERNEL);
		if (ret)
			goto cleanup;
	}
	return 0;

cleanup:
	netif_warn(rx->adapter, ifup, rx->adapter->netdev,
		   "Error allocating memory for LAN743x\n");

	lan743x_rx_ring_cleanup(rx);
	return ret;
}

static void lan743x_rx_close(struct lan743x_rx *rx)
{
	struct lan743x_adapter *adapter = rx->adapter;

	lan743x_csr_write(adapter, FCT_RX_CTL,
			  FCT_RX_CTL_DIS_(rx->channel_number));
	lan743x_csr_wait_for_bit(adapter, FCT_RX_CTL,
				 FCT_RX_CTL_EN_(rx->channel_number),
				 0, 1000, 20000, 100);

	lan743x_csr_write(adapter, DMAC_CMD,
			  DMAC_CMD_STOP_R_(rx->channel_number));
	lan743x_dmac_rx_wait_till_stopped(adapter, rx->channel_number);

	lan743x_csr_write(adapter, DMAC_INT_EN_CLR,
			  DMAC_INT_BIT_RXFRM_(rx->channel_number));
	lan743x_csr_write(adapter, INT_EN_CLR,
			  INT_BIT_DMA_RX_(rx->channel_number));
	napi_disable(&rx->napi);

	netif_napi_del(&rx->napi);

	lan743x_rx_ring_cleanup(rx);
}

static int lan743x_rx_open(struct lan743x_rx *rx)
{
	struct lan743x_adapter *adapter = rx->adapter;
	u32 data = 0;
	int ret;

	rx->frame_count = 0;
	ret = lan743x_rx_ring_init(rx);
	if (ret)
		goto return_error;

	netif_napi_add(adapter->netdev,
		       &rx->napi, lan743x_rx_napi_poll,
		       NAPI_POLL_WEIGHT);

	lan743x_csr_write(adapter, DMAC_CMD,
			  DMAC_CMD_RX_SWR_(rx->channel_number));
	lan743x_csr_wait_for_bit(adapter, DMAC_CMD,
				 DMAC_CMD_RX_SWR_(rx->channel_number),
				 0, 1000, 20000, 100);

	/* set ring base address */
	lan743x_csr_write(adapter,
			  RX_BASE_ADDRH(rx->channel_number),
			  DMA_ADDR_HIGH32(rx->ring_dma_ptr));
	lan743x_csr_write(adapter,
			  RX_BASE_ADDRL(rx->channel_number),
			  DMA_ADDR_LOW32(rx->ring_dma_ptr));

	/* set rx write back address */
	lan743x_csr_write(adapter,
			  RX_HEAD_WRITEBACK_ADDRH(rx->channel_number),
			  DMA_ADDR_HIGH32(rx->head_dma_ptr));
	lan743x_csr_write(adapter,
			  RX_HEAD_WRITEBACK_ADDRL(rx->channel_number),
			  DMA_ADDR_LOW32(rx->head_dma_ptr));
	data = RX_CFG_A_RX_HP_WB_EN_;
	if (!(adapter->csr.flags & LAN743X_CSR_FLAG_IS_A0)) {
		data |= (RX_CFG_A_RX_WB_ON_INT_TMR_ |
			RX_CFG_A_RX_WB_THRES_SET_(0x7) |
			RX_CFG_A_RX_PF_THRES_SET_(16) |
			RX_CFG_A_RX_PF_PRI_THRES_SET_(4));
	}

	/* set RX_CFG_A */
	lan743x_csr_write(adapter,
			  RX_CFG_A(rx->channel_number), data);

	/* set RX_CFG_B */
	data = lan743x_csr_read(adapter, RX_CFG_B(rx->channel_number));
	data &= ~RX_CFG_B_RX_PAD_MASK_;
	if (!RX_HEAD_PADDING)
		data |= RX_CFG_B_RX_PAD_0_;
	else
		data |= RX_CFG_B_RX_PAD_2_;
	data &= ~RX_CFG_B_RX_RING_LEN_MASK_;
	data |= ((rx->ring_size) & RX_CFG_B_RX_RING_LEN_MASK_);
	data |= RX_CFG_B_TS_ALL_RX_;
	if (!(adapter->csr.flags & LAN743X_CSR_FLAG_IS_A0))
		data |= RX_CFG_B_RDMABL_512_;

	lan743x_csr_write(adapter, RX_CFG_B(rx->channel_number), data);
	rx->vector_flags = lan743x_intr_get_vector_flags(adapter,
							 INT_BIT_DMA_RX_
							 (rx->channel_number));

	/* set RX_CFG_C */
	data = 0;
	if (rx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_AUTO_CLEAR)
		data |= RX_CFG_C_RX_TOP_INT_EN_AUTO_CLR_;
	if (rx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_STATUS_AUTO_CLEAR)
		data |= RX_CFG_C_RX_DMA_INT_STS_AUTO_CLR_;
	if (rx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_STATUS_R2C)
		data |= RX_CFG_C_RX_INT_STS_R2C_MODE_MASK_;
	if (rx->vector_flags & LAN743X_VECTOR_FLAG_SOURCE_ENABLE_R2C)
		data |= RX_CFG_C_RX_INT_EN_R2C_;
	lan743x_csr_write(adapter, RX_CFG_C(rx->channel_number), data);

	rx->last_tail = ((u32)(rx->ring_size - 1));
	lan743x_csr_write(adapter, RX_TAIL(rx->channel_number),
			  rx->last_tail);
	rx->last_head = lan743x_csr_read(adapter, RX_HEAD(rx->channel_number));
	if (rx->last_head) {
		ret = -EIO;
		goto napi_delete;
	}

	napi_enable(&rx->napi);

	lan743x_csr_write(adapter, INT_EN_SET,
			  INT_BIT_DMA_RX_(rx->channel_number));
	lan743x_csr_write(adapter, DMAC_INT_STS,
			  DMAC_INT_BIT_RXFRM_(rx->channel_number));
	lan743x_csr_write(adapter, DMAC_INT_EN_SET,
			  DMAC_INT_BIT_RXFRM_(rx->channel_number));
	lan743x_csr_write(adapter, DMAC_CMD,
			  DMAC_CMD_START_R_(rx->channel_number));

	/* initialize fifo */
	lan743x_csr_write(adapter, FCT_RX_CTL,
			  FCT_RX_CTL_RESET_(rx->channel_number));
	lan743x_csr_wait_for_bit(adapter, FCT_RX_CTL,
				 FCT_RX_CTL_RESET_(rx->channel_number),
				 0, 1000, 20000, 100);
	lan743x_csr_write(adapter, FCT_FLOW(rx->channel_number),
			  FCT_FLOW_CTL_REQ_EN_ |
			  FCT_FLOW_CTL_ON_THRESHOLD_SET_(0x2A) |
			  FCT_FLOW_CTL_OFF_THRESHOLD_SET_(0xA));

	/* enable fifo */
	lan743x_csr_write(adapter, FCT_RX_CTL,
			  FCT_RX_CTL_EN_(rx->channel_number));
	return 0;

napi_delete:
	netif_napi_del(&rx->napi);
	lan743x_rx_ring_cleanup(rx);

return_error:
	return ret;
}

static int lan743x_netdev_close(struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	int index;

	lan743x_tx_close(&adapter->tx[0]);

	for (index = 0; index < LAN743X_USED_RX_CHANNELS; index++)
		lan743x_rx_close(&adapter->rx[index]);

	lan743x_ptp_close(adapter);

	lan743x_phy_close(adapter);

	lan743x_mac_close(adapter);

	lan743x_intr_close(adapter);

	return 0;
}

static int lan743x_netdev_open(struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	int index;
	int ret;

	ret = lan743x_intr_open(adapter);
	if (ret)
		goto return_error;

	ret = lan743x_mac_open(adapter);
	if (ret)
		goto close_intr;

	ret = lan743x_phy_open(adapter);
	if (ret)
		goto close_mac;

	ret = lan743x_ptp_open(adapter);
	if (ret)
		goto close_phy;

	lan743x_rfe_open(adapter);

	for (index = 0; index < LAN743X_USED_RX_CHANNELS; index++) {
		ret = lan743x_rx_open(&adapter->rx[index]);
		if (ret)
			goto close_rx;
	}

	ret = lan743x_tx_open(&adapter->tx[0]);
	if (ret)
		goto close_rx;

	return 0;

close_rx:
	for (index = 0; index < LAN743X_USED_RX_CHANNELS; index++) {
		if (adapter->rx[index].ring_cpu_ptr)
			lan743x_rx_close(&adapter->rx[index]);
	}
	lan743x_ptp_close(adapter);

close_phy:
	lan743x_phy_close(adapter);

close_mac:
	lan743x_mac_close(adapter);

close_intr:
	lan743x_intr_close(adapter);

return_error:
	netif_warn(adapter, ifup, adapter->netdev,
		   "Error opening LAN743x\n");
	return ret;
}

static netdev_tx_t lan743x_netdev_xmit_frame(struct sk_buff *skb,
					     struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return lan743x_tx_xmit_frame(&adapter->tx[0], skb);
}

static int lan743x_netdev_ioctl(struct net_device *netdev,
				struct ifreq *ifr, int cmd)
{
	if (!netif_running(netdev))
		return -EINVAL;
	if (cmd == SIOCSHWTSTAMP)
		return lan743x_ptp_ioctl(netdev, ifr, cmd);
	return phy_mii_ioctl(netdev->phydev, ifr, cmd);
}

static void lan743x_netdev_set_multicast(struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	lan743x_rfe_set_multicast(adapter);
}

static int lan743x_netdev_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	int ret = 0;

	ret = lan743x_mac_set_mtu(adapter, new_mtu);
	if (!ret)
		netdev->mtu = new_mtu;
	return ret;
}

static void lan743x_netdev_get_stats64(struct net_device *netdev,
				       struct rtnl_link_stats64 *stats)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	stats->rx_packets = lan743x_csr_read(adapter, STAT_RX_TOTAL_FRAMES);
	stats->tx_packets = lan743x_csr_read(adapter, STAT_TX_TOTAL_FRAMES);
	stats->rx_bytes = lan743x_csr_read(adapter,
					   STAT_RX_UNICAST_BYTE_COUNT) +
			  lan743x_csr_read(adapter,
					   STAT_RX_BROADCAST_BYTE_COUNT) +
			  lan743x_csr_read(adapter,
					   STAT_RX_MULTICAST_BYTE_COUNT);
	stats->tx_bytes = lan743x_csr_read(adapter,
					   STAT_TX_UNICAST_BYTE_COUNT) +
			  lan743x_csr_read(adapter,
					   STAT_TX_BROADCAST_BYTE_COUNT) +
			  lan743x_csr_read(adapter,
					   STAT_TX_MULTICAST_BYTE_COUNT);
	stats->rx_errors = lan743x_csr_read(adapter, STAT_RX_FCS_ERRORS) +
			   lan743x_csr_read(adapter,
					    STAT_RX_ALIGNMENT_ERRORS) +
			   lan743x_csr_read(adapter, STAT_RX_JABBER_ERRORS) +
			   lan743x_csr_read(adapter,
					    STAT_RX_UNDERSIZE_FRAME_ERRORS) +
			   lan743x_csr_read(adapter,
					    STAT_RX_OVERSIZE_FRAME_ERRORS);
	stats->tx_errors = lan743x_csr_read(adapter, STAT_TX_FCS_ERRORS) +
			   lan743x_csr_read(adapter,
					    STAT_TX_EXCESS_DEFERRAL_ERRORS) +
			   lan743x_csr_read(adapter, STAT_TX_CARRIER_ERRORS);
	stats->rx_dropped = lan743x_csr_read(adapter,
					     STAT_RX_DROPPED_FRAMES);
	stats->tx_dropped = lan743x_csr_read(adapter,
					     STAT_TX_EXCESSIVE_COLLISION);
	stats->multicast = lan743x_csr_read(adapter,
					    STAT_RX_MULTICAST_FRAMES) +
			   lan743x_csr_read(adapter,
					    STAT_TX_MULTICAST_FRAMES);
	stats->collisions = lan743x_csr_read(adapter,
					     STAT_TX_SINGLE_COLLISIONS) +
			    lan743x_csr_read(adapter,
					     STAT_TX_MULTIPLE_COLLISIONS) +
			    lan743x_csr_read(adapter,
					     STAT_TX_LATE_COLLISIONS);
}

static int lan743x_netdev_set_mac_address(struct net_device *netdev,
					  void *addr)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	struct sockaddr *sock_addr = addr;
	int ret;

	ret = eth_prepare_mac_addr_change(netdev, sock_addr);
	if (ret)
		return ret;
	eth_hw_addr_set(netdev, sock_addr->sa_data);
	lan743x_mac_set_address(adapter, sock_addr->sa_data);
	lan743x_rfe_update_mac_address(adapter);
	return 0;
}

static const struct net_device_ops lan743x_netdev_ops = {
	.ndo_open		= lan743x_netdev_open,
	.ndo_stop		= lan743x_netdev_close,
	.ndo_start_xmit		= lan743x_netdev_xmit_frame,
	.ndo_eth_ioctl		= lan743x_netdev_ioctl,
	.ndo_set_rx_mode	= lan743x_netdev_set_multicast,
	.ndo_change_mtu		= lan743x_netdev_change_mtu,
	.ndo_get_stats64	= lan743x_netdev_get_stats64,
	.ndo_set_mac_address	= lan743x_netdev_set_mac_address,
};

static void lan743x_hardware_cleanup(struct lan743x_adapter *adapter)
{
	lan743x_csr_write(adapter, INT_EN_CLR, 0xFFFFFFFF);
}

static void lan743x_mdiobus_cleanup(struct lan743x_adapter *adapter)
{
	mdiobus_unregister(adapter->mdiobus);
}

static void lan743x_full_cleanup(struct lan743x_adapter *adapter)
{
	unregister_netdev(adapter->netdev);

	lan743x_mdiobus_cleanup(adapter);
	lan743x_hardware_cleanup(adapter);
	lan743x_pci_cleanup(adapter);
}

static int lan743x_hardware_init(struct lan743x_adapter *adapter,
				 struct pci_dev *pdev)
{
	struct lan743x_tx *tx;
	int index;
	int ret;

	adapter->intr.irq = adapter->pdev->irq;
	lan743x_csr_write(adapter, INT_EN_CLR, 0xFFFFFFFF);

	ret = lan743x_gpio_init(adapter);
	if (ret)
		return ret;

	ret = lan743x_mac_init(adapter);
	if (ret)
		return ret;

	ret = lan743x_phy_init(adapter);
	if (ret)
		return ret;

	ret = lan743x_ptp_init(adapter);
	if (ret)
		return ret;

	lan743x_rfe_update_mac_address(adapter);

	ret = lan743x_dmac_init(adapter);
	if (ret)
		return ret;

	for (index = 0; index < LAN743X_USED_RX_CHANNELS; index++) {
		adapter->rx[index].adapter = adapter;
		adapter->rx[index].channel_number = index;
	}

	tx = &adapter->tx[0];
	tx->adapter = adapter;
	tx->channel_number = 0;
	spin_lock_init(&tx->ring_lock);
	return 0;
}

static int lan743x_mdiobus_init(struct lan743x_adapter *adapter)
{
	int ret;

	adapter->mdiobus = devm_mdiobus_alloc(&adapter->pdev->dev);
	if (!(adapter->mdiobus)) {
		ret = -ENOMEM;
		goto return_error;
	}

	adapter->mdiobus->priv = (void *)adapter;
	adapter->mdiobus->read = lan743x_mdiobus_read;
	adapter->mdiobus->write = lan743x_mdiobus_write;
	adapter->mdiobus->name = "lan743x-mdiobus";
	snprintf(adapter->mdiobus->id, MII_BUS_ID_SIZE,
		 "pci-%s", pci_name(adapter->pdev));

	if ((adapter->csr.id_rev & ID_REV_ID_MASK_) == ID_REV_ID_LAN7430_)
		/* LAN7430 uses internal phy at address 1 */
		adapter->mdiobus->phy_mask = ~(u32)BIT(1);

	/* register mdiobus */
	ret = mdiobus_register(adapter->mdiobus);
	if (ret < 0)
		goto return_error;
	return 0;

return_error:
	return ret;
}

/* lan743x_pcidev_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @id: entry in lan743x_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int lan743x_pcidev_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct lan743x_adapter *adapter = NULL;
	struct net_device *netdev = NULL;
	int ret = -ENODEV;

	netdev = devm_alloc_etherdev(&pdev->dev,
				     sizeof(struct lan743x_adapter));
	if (!netdev)
		goto return_error;

	SET_NETDEV_DEV(netdev, &pdev->dev);
	pci_set_drvdata(pdev, netdev);
	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->msg_enable = NETIF_MSG_DRV | NETIF_MSG_PROBE |
			      NETIF_MSG_LINK | NETIF_MSG_IFUP |
			      NETIF_MSG_IFDOWN | NETIF_MSG_TX_QUEUED;
	netdev->max_mtu = LAN743X_MAX_FRAME_SIZE;

	of_get_mac_address(pdev->dev.of_node, adapter->mac_address);

	ret = lan743x_pci_init(adapter, pdev);
	if (ret)
		goto return_error;

	ret = lan743x_csr_init(adapter);
	if (ret)
		goto cleanup_pci;

	ret = lan743x_hardware_init(adapter, pdev);
	if (ret)
		goto cleanup_pci;

	ret = lan743x_mdiobus_init(adapter);
	if (ret)
		goto cleanup_hardware;

	adapter->netdev->netdev_ops = &lan743x_netdev_ops;
	adapter->netdev->ethtool_ops = &lan743x_ethtool_ops;
	adapter->netdev->features = NETIF_F_SG | NETIF_F_TSO | NETIF_F_HW_CSUM;
	adapter->netdev->hw_features = adapter->netdev->features;

	/* carrier off reporting is important to ethtool even BEFORE open */
	netif_carrier_off(netdev);

	ret = register_netdev(adapter->netdev);
	if (ret < 0)
		goto cleanup_mdiobus;
	return 0;

cleanup_mdiobus:
	lan743x_mdiobus_cleanup(adapter);

cleanup_hardware:
	lan743x_hardware_cleanup(adapter);

cleanup_pci:
	lan743x_pci_cleanup(adapter);

return_error:
	pr_warn("Initialization failed\n");
	return ret;
}

/**
 * lan743x_pcidev_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * this is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  This could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void lan743x_pcidev_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	lan743x_full_cleanup(adapter);
}

static void lan743x_pcidev_shutdown(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	rtnl_lock();
	netif_device_detach(netdev);

	/* close netdev when netdev is at running state.
	 * For instance, it is true when system goes to sleep by pm-suspend
	 * However, it is false when system goes to sleep by suspend GUI menu
	 */
	if (netif_running(netdev))
		lan743x_netdev_close(netdev);
	rtnl_unlock();

#ifdef CONFIG_PM
	pci_save_state(pdev);
#endif

	/* clean up lan743x portion */
	lan743x_hardware_cleanup(adapter);
}

#ifdef CONFIG_PM_SLEEP
static u16 lan743x_pm_wakeframe_crc16(const u8 *buf, int len)
{
	return bitrev16(crc16(0xFFFF, buf, len));
}

static void lan743x_pm_set_wol(struct lan743x_adapter *adapter)
{
	const u8 ipv4_multicast[3] = { 0x01, 0x00, 0x5E };
	const u8 ipv6_multicast[3] = { 0x33, 0x33 };
	const u8 arp_type[2] = { 0x08, 0x06 };
	int mask_index;
	u32 pmtctl;
	u32 wucsr;
	u32 macrx;
	u16 crc;

	for (mask_index = 0; mask_index < MAC_NUM_OF_WUF_CFG; mask_index++)
		lan743x_csr_write(adapter, MAC_WUF_CFG(mask_index), 0);

	/* clear wake settings */
	pmtctl = lan743x_csr_read(adapter, PMT_CTL);
	pmtctl |= PMT_CTL_WUPS_MASK_;
	pmtctl &= ~(PMT_CTL_GPIO_WAKEUP_EN_ | PMT_CTL_EEE_WAKEUP_EN_ |
		PMT_CTL_WOL_EN_ | PMT_CTL_MAC_D3_RX_CLK_OVR_ |
		PMT_CTL_RX_FCT_RFE_D3_CLK_OVR_ | PMT_CTL_ETH_PHY_WAKE_EN_);

	macrx = lan743x_csr_read(adapter, MAC_RX);

	wucsr = 0;
	mask_index = 0;

	pmtctl |= PMT_CTL_ETH_PHY_D3_COLD_OVR_ | PMT_CTL_ETH_PHY_D3_OVR_;

	if (adapter->wolopts & WAKE_PHY) {
		pmtctl |= PMT_CTL_ETH_PHY_EDPD_PLL_CTL_;
		pmtctl |= PMT_CTL_ETH_PHY_WAKE_EN_;
	}
	if (adapter->wolopts & WAKE_MAGIC) {
		wucsr |= MAC_WUCSR_MPEN_;
		macrx |= MAC_RX_RXEN_;
		pmtctl |= PMT_CTL_WOL_EN_ | PMT_CTL_MAC_D3_RX_CLK_OVR_;
	}
	if (adapter->wolopts & WAKE_UCAST) {
		wucsr |= MAC_WUCSR_RFE_WAKE_EN_ | MAC_WUCSR_PFDA_EN_;
		macrx |= MAC_RX_RXEN_;
		pmtctl |= PMT_CTL_WOL_EN_ | PMT_CTL_MAC_D3_RX_CLK_OVR_;
		pmtctl |= PMT_CTL_RX_FCT_RFE_D3_CLK_OVR_;
	}
	if (adapter->wolopts & WAKE_BCAST) {
		wucsr |= MAC_WUCSR_RFE_WAKE_EN_ | MAC_WUCSR_BCST_EN_;
		macrx |= MAC_RX_RXEN_;
		pmtctl |= PMT_CTL_WOL_EN_ | PMT_CTL_MAC_D3_RX_CLK_OVR_;
		pmtctl |= PMT_CTL_RX_FCT_RFE_D3_CLK_OVR_;
	}
	if (adapter->wolopts & WAKE_MCAST) {
		/* IPv4 multicast */
		crc = lan743x_pm_wakeframe_crc16(ipv4_multicast, 3);
		lan743x_csr_write(adapter, MAC_WUF_CFG(mask_index),
				  MAC_WUF_CFG_EN_ | MAC_WUF_CFG_TYPE_MCAST_ |
				  (0 << MAC_WUF_CFG_OFFSET_SHIFT_) |
				  (crc & MAC_WUF_CFG_CRC16_MASK_));
		lan743x_csr_write(adapter, MAC_WUF_MASK0(mask_index), 7);
		lan743x_csr_write(adapter, MAC_WUF_MASK1(mask_index), 0);
		lan743x_csr_write(adapter, MAC_WUF_MASK2(mask_index), 0);
		lan743x_csr_write(adapter, MAC_WUF_MASK3(mask_index), 0);
		mask_index++;

		/* IPv6 multicast */
		crc = lan743x_pm_wakeframe_crc16(ipv6_multicast, 2);
		lan743x_csr_write(adapter, MAC_WUF_CFG(mask_index),
				  MAC_WUF_CFG_EN_ | MAC_WUF_CFG_TYPE_MCAST_ |
				  (0 << MAC_WUF_CFG_OFFSET_SHIFT_) |
				  (crc & MAC_WUF_CFG_CRC16_MASK_));
		lan743x_csr_write(adapter, MAC_WUF_MASK0(mask_index), 3);
		lan743x_csr_write(adapter, MAC_WUF_MASK1(mask_index), 0);
		lan743x_csr_write(adapter, MAC_WUF_MASK2(mask_index), 0);
		lan743x_csr_write(adapter, MAC_WUF_MASK3(mask_index), 0);
		mask_index++;

		wucsr |= MAC_WUCSR_RFE_WAKE_EN_ | MAC_WUCSR_WAKE_EN_;
		macrx |= MAC_RX_RXEN_;
		pmtctl |= PMT_CTL_WOL_EN_ | PMT_CTL_MAC_D3_RX_CLK_OVR_;
		pmtctl |= PMT_CTL_RX_FCT_RFE_D3_CLK_OVR_;
	}
	if (adapter->wolopts & WAKE_ARP) {
		/* set MAC_WUF_CFG & WUF_MASK
		 * for packettype (offset 12,13) = ARP (0x0806)
		 */
		crc = lan743x_pm_wakeframe_crc16(arp_type, 2);
		lan743x_csr_write(adapter, MAC_WUF_CFG(mask_index),
				  MAC_WUF_CFG_EN_ | MAC_WUF_CFG_TYPE_ALL_ |
				  (0 << MAC_WUF_CFG_OFFSET_SHIFT_) |
				  (crc & MAC_WUF_CFG_CRC16_MASK_));
		lan743x_csr_write(adapter, MAC_WUF_MASK0(mask_index), 0x3000);
		lan743x_csr_write(adapter, MAC_WUF_MASK1(mask_index), 0);
		lan743x_csr_write(adapter, MAC_WUF_MASK2(mask_index), 0);
		lan743x_csr_write(adapter, MAC_WUF_MASK3(mask_index), 0);
		mask_index++;

		wucsr |= MAC_WUCSR_RFE_WAKE_EN_ | MAC_WUCSR_WAKE_EN_;
		macrx |= MAC_RX_RXEN_;
		pmtctl |= PMT_CTL_WOL_EN_ | PMT_CTL_MAC_D3_RX_CLK_OVR_;
		pmtctl |= PMT_CTL_RX_FCT_RFE_D3_CLK_OVR_;
	}

	lan743x_csr_write(adapter, MAC_WUCSR, wucsr);
	lan743x_csr_write(adapter, PMT_CTL, pmtctl);
	lan743x_csr_write(adapter, MAC_RX, macrx);
}

static int lan743x_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	lan743x_pcidev_shutdown(pdev);

	/* clear all wakes */
	lan743x_csr_write(adapter, MAC_WUCSR, 0);
	lan743x_csr_write(adapter, MAC_WUCSR2, 0);
	lan743x_csr_write(adapter, MAC_WK_SRC, 0xFFFFFFFF);

	if (adapter->wolopts)
		lan743x_pm_set_wol(adapter);

	/* Host sets PME_En, put D3hot */
	return pci_prepare_to_sleep(pdev);
}

static int lan743x_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	int ret;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	ret = lan743x_hardware_init(adapter, pdev);
	if (ret) {
		netif_err(adapter, probe, adapter->netdev,
			  "lan743x_hardware_init returned %d\n", ret);
		lan743x_pci_cleanup(adapter);
		return ret;
	}

	/* open netdev when netdev is at running state while resume.
	 * For instance, it is true when system wakesup after pm-suspend
	 * However, it is false when system wakes up after suspend GUI menu
	 */
	if (netif_running(netdev))
		lan743x_netdev_open(netdev);

	netif_device_attach(netdev);

	return 0;
}

static const struct dev_pm_ops lan743x_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(lan743x_pm_suspend, lan743x_pm_resume)
};
#endif /* CONFIG_PM_SLEEP */

static const struct pci_device_id lan743x_pcidev_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_SMSC, PCI_DEVICE_ID_SMSC_LAN7430) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SMSC, PCI_DEVICE_ID_SMSC_LAN7431) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SMSC, PCI_DEVICE_ID_SMSC_A011) },
	{ PCI_DEVICE(PCI_VENDOR_ID_SMSC, PCI_DEVICE_ID_SMSC_A041) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, lan743x_pcidev_tbl);

static struct pci_driver lan743x_pcidev_driver = {
	.name     = DRIVER_NAME,
	.id_table = lan743x_pcidev_tbl,
	.probe    = lan743x_pcidev_probe,
	.remove   = lan743x_pcidev_remove,
#ifdef CONFIG_PM_SLEEP
	.driver.pm = &lan743x_pm_ops,
#endif
	.shutdown = lan743x_pcidev_shutdown,
};

module_pci_driver(lan743x_pcidev_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
