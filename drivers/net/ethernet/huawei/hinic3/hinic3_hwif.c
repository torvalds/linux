// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/io.h>

#include "hinic3_common.h"
#include "hinic3_csr.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"

#define HINIC3_HWIF_READY_TIMEOUT          10000
#define HINIC3_DB_AND_OUTBOUND_EN_TIMEOUT  60000
#define HINIC3_PCIE_LINK_DOWN              0xFFFFFFFF

/* config BAR4/5 4MB, DB & DWQE both 2MB */
#define HINIC3_DB_DWQE_SIZE    0x00400000

/* db/dwqe page size: 4K */
#define HINIC3_DB_PAGE_SIZE    0x00001000
#define HINIC3_DWQE_OFFSET     0x00000800
#define HINIC3_DB_MAX_AREAS    (HINIC3_DB_DWQE_SIZE / HINIC3_DB_PAGE_SIZE)

#define HINIC3_MAX_MSIX_ENTRY  2048

#define HINIC3_AF0_FUNC_GLOBAL_IDX_MASK  GENMASK(11, 0)
#define HINIC3_AF0_P2P_IDX_MASK          GENMASK(16, 12)
#define HINIC3_AF0_PCI_INTF_IDX_MASK     GENMASK(19, 17)
#define HINIC3_AF0_FUNC_TYPE_MASK        BIT(28)
#define HINIC3_AF0_GET(val, member) \
	FIELD_GET(HINIC3_AF0_##member##_MASK, val)

#define HINIC3_AF1_AEQS_PER_FUNC_MASK     GENMASK(9, 8)
#define HINIC3_AF1_MGMT_INIT_STATUS_MASK  BIT(30)
#define HINIC3_AF1_GET(val, member) \
	FIELD_GET(HINIC3_AF1_##member##_MASK, val)

#define HINIC3_AF2_CEQS_PER_FUNC_MASK      GENMASK(8, 0)
#define HINIC3_AF2_IRQS_PER_FUNC_MASK      GENMASK(26, 16)
#define HINIC3_AF2_GET(val, member) \
	FIELD_GET(HINIC3_AF2_##member##_MASK, val)

#define HINIC3_AF4_DOORBELL_CTRL_MASK  BIT(0)
#define HINIC3_AF4_GET(val, member) \
	FIELD_GET(HINIC3_AF4_##member##_MASK, val)
#define HINIC3_AF4_SET(val, member) \
	FIELD_PREP(HINIC3_AF4_##member##_MASK, val)

#define HINIC3_AF5_OUTBOUND_CTRL_MASK  BIT(0)
#define HINIC3_AF5_GET(val, member) \
	FIELD_GET(HINIC3_AF5_##member##_MASK, val)

#define HINIC3_AF6_PF_STATUS_MASK     GENMASK(15, 0)
#define HINIC3_AF6_FUNC_MAX_SQ_MASK   GENMASK(31, 23)
#define HINIC3_AF6_MSIX_FLEX_EN_MASK  BIT(22)
#define HINIC3_AF6_GET(val, member) \
	FIELD_GET(HINIC3_AF6_##member##_MASK, val)

#define HINIC3_GET_REG_ADDR(reg)  ((reg) & (HINIC3_REGS_FLAG_MASK))

static void __iomem *hinic3_reg_addr(struct hinic3_hwif *hwif, u32 reg)
{
	return hwif->cfg_regs_base + HINIC3_GET_REG_ADDR(reg);
}

u32 hinic3_hwif_read_reg(struct hinic3_hwif *hwif, u32 reg)
{
	void __iomem *addr = hinic3_reg_addr(hwif, reg);

	return ioread32be(addr);
}

void hinic3_hwif_write_reg(struct hinic3_hwif *hwif, u32 reg, u32 val)
{
	void __iomem *addr = hinic3_reg_addr(hwif, reg);

	iowrite32be(val, addr);
}

static enum hinic3_wait_return check_hwif_ready_handler(void *priv_data)
{
	struct hinic3_hwdev *hwdev = priv_data;
	u32 attr1;

	attr1 = hinic3_hwif_read_reg(hwdev->hwif, HINIC3_CSR_FUNC_ATTR1_ADDR);

	return HINIC3_AF1_GET(attr1, MGMT_INIT_STATUS) ?
	       HINIC3_WAIT_PROCESS_CPL : HINIC3_WAIT_PROCESS_WAITING;
}

static int wait_hwif_ready(struct hinic3_hwdev *hwdev)
{
	return hinic3_wait_for_timeout(hwdev, check_hwif_ready_handler,
				       HINIC3_HWIF_READY_TIMEOUT,
				       USEC_PER_MSEC);
}

/* Set attr struct from HW attr values. */
static void set_hwif_attr(struct hinic3_func_attr *attr, u32 attr0, u32 attr1,
			  u32 attr2, u32 attr3, u32 attr6)
{
	attr->func_global_idx = HINIC3_AF0_GET(attr0, FUNC_GLOBAL_IDX);
	attr->port_to_port_idx = HINIC3_AF0_GET(attr0, P2P_IDX);
	attr->pci_intf_idx = HINIC3_AF0_GET(attr0, PCI_INTF_IDX);
	attr->func_type = HINIC3_AF0_GET(attr0, FUNC_TYPE);

	attr->num_aeqs = BIT(HINIC3_AF1_GET(attr1, AEQS_PER_FUNC));
	attr->num_ceqs = HINIC3_AF2_GET(attr2, CEQS_PER_FUNC);
	attr->num_irqs = HINIC3_AF2_GET(attr2, IRQS_PER_FUNC);
	if (attr->num_irqs > HINIC3_MAX_MSIX_ENTRY)
		attr->num_irqs = HINIC3_MAX_MSIX_ENTRY;

	attr->num_sq = HINIC3_AF6_GET(attr6, FUNC_MAX_SQ);
	attr->msix_flex_en = HINIC3_AF6_GET(attr6, MSIX_FLEX_EN);
}

/* Read attributes from HW and set attribute struct. */
static int init_hwif_attr(struct hinic3_hwdev *hwdev)
{
	u32 attr0, attr1, attr2, attr3, attr6;
	struct hinic3_hwif *hwif;

	hwif = hwdev->hwif;
	attr0 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR0_ADDR);
	if (attr0 == HINIC3_PCIE_LINK_DOWN)
		return -EFAULT;

	attr1 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR1_ADDR);
	if (attr1 == HINIC3_PCIE_LINK_DOWN)
		return -EFAULT;

	attr2 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR2_ADDR);
	if (attr2 == HINIC3_PCIE_LINK_DOWN)
		return -EFAULT;

	attr3 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR3_ADDR);
	if (attr3 == HINIC3_PCIE_LINK_DOWN)
		return -EFAULT;

	attr6 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR6_ADDR);
	if (attr6 == HINIC3_PCIE_LINK_DOWN)
		return -EFAULT;

	set_hwif_attr(&hwif->attr, attr0, attr1, attr2, attr3, attr6);

	if (!hwif->attr.num_ceqs) {
		dev_err(hwdev->dev, "Ceq num cfg in fw is zero\n");
		return -EFAULT;
	}

	if (!hwif->attr.num_irqs) {
		dev_err(hwdev->dev,
			"Irq num cfg in fw is zero, msix_flex_en %d\n",
			hwif->attr.msix_flex_en);
		return -EFAULT;
	}

	return 0;
}

static enum hinic3_doorbell_ctrl hinic3_get_doorbell_ctrl_status(struct hinic3_hwif *hwif)
{
	u32 attr4 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR4_ADDR);

	return HINIC3_AF4_GET(attr4, DOORBELL_CTRL);
}

static enum hinic3_outbound_ctrl hinic3_get_outbound_ctrl_status(struct hinic3_hwif *hwif)
{
	u32 attr5 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR5_ADDR);

	return HINIC3_AF5_GET(attr5, OUTBOUND_CTRL);
}

void hinic3_toggle_doorbell(struct hinic3_hwif *hwif,
			    enum hinic3_doorbell_ctrl flag)
{
	u32 addr, attr4;

	addr = HINIC3_CSR_FUNC_ATTR4_ADDR;
	attr4 = hinic3_hwif_read_reg(hwif, addr);

	attr4 &= ~HINIC3_AF4_DOORBELL_CTRL_MASK;
	attr4 |= HINIC3_AF4_SET(flag, DOORBELL_CTRL);

	hinic3_hwif_write_reg(hwif, addr, attr4);
}

static int db_area_idx_init(struct hinic3_hwif *hwif, u64 db_base_phy,
			    u8 __iomem *db_base, u64 db_dwqe_len)
{
	struct hinic3_db_area *db_area = &hwif->db_area;
	u32 db_max_areas;

	hwif->db_base_phy = db_base_phy;
	hwif->db_base = db_base;
	hwif->db_dwqe_len = db_dwqe_len;

	db_max_areas = db_dwqe_len > HINIC3_DB_DWQE_SIZE ?
		       HINIC3_DB_MAX_AREAS : db_dwqe_len / HINIC3_DB_PAGE_SIZE;
	db_area->db_bitmap_array = bitmap_zalloc(db_max_areas, GFP_KERNEL);
	if (!db_area->db_bitmap_array)
		return -ENOMEM;

	db_area->db_max_areas = db_max_areas;
	spin_lock_init(&db_area->idx_lock);

	return 0;
}

static void db_area_idx_free(struct hinic3_db_area *db_area)
{
	bitmap_free(db_area->db_bitmap_array);
}

static int get_db_idx(struct hinic3_hwif *hwif, u32 *idx)
{
	struct hinic3_db_area *db_area = &hwif->db_area;
	u32 pg_idx;

	spin_lock(&db_area->idx_lock);
	pg_idx = find_first_zero_bit(db_area->db_bitmap_array,
				     db_area->db_max_areas);
	if (pg_idx == db_area->db_max_areas) {
		spin_unlock(&db_area->idx_lock);
		return -ENOMEM;
	}
	set_bit(pg_idx, db_area->db_bitmap_array);
	spin_unlock(&db_area->idx_lock);

	*idx = pg_idx;

	return 0;
}

static void free_db_idx(struct hinic3_hwif *hwif, u32 idx)
{
	struct hinic3_db_area *db_area = &hwif->db_area;

	spin_lock(&db_area->idx_lock);
	clear_bit(idx, db_area->db_bitmap_array);
	spin_unlock(&db_area->idx_lock);
}

void hinic3_free_db_addr(struct hinic3_hwdev *hwdev, const u8 __iomem *db_base)
{
	struct hinic3_hwif *hwif;
	uintptr_t distance;
	u32 idx;

	hwif = hwdev->hwif;
	distance = db_base - hwif->db_base;
	idx = distance / HINIC3_DB_PAGE_SIZE;

	free_db_idx(hwif, idx);
}

int hinic3_alloc_db_addr(struct hinic3_hwdev *hwdev, void __iomem **db_base,
			 void __iomem **dwqe_base)
{
	struct hinic3_hwif *hwif;
	u8 __iomem *addr;
	u32 idx;
	int err;

	hwif = hwdev->hwif;

	err = get_db_idx(hwif, &idx);
	if (err)
		return err;

	addr = hwif->db_base + idx * HINIC3_DB_PAGE_SIZE;
	*db_base = addr;

	if (dwqe_base)
		*dwqe_base = addr + HINIC3_DWQE_OFFSET;

	return 0;
}

void hinic3_set_msix_state(struct hinic3_hwdev *hwdev, u16 msix_idx,
			   enum hinic3_msix_state flag)
{
	struct hinic3_hwif *hwif;
	u8 int_msk = 1;
	u32 mask_bits;
	u32 addr;

	hwif = hwdev->hwif;

	if (flag)
		mask_bits = HINIC3_MSI_CLR_INDIR_SET(int_msk, INT_MSK_SET);
	else
		mask_bits = HINIC3_MSI_CLR_INDIR_SET(int_msk, INT_MSK_CLR);
	mask_bits = mask_bits |
		    HINIC3_MSI_CLR_INDIR_SET(msix_idx, SIMPLE_INDIR_IDX);

	addr = HINIC3_CSR_FUNC_MSI_CLR_WR_ADDR;
	hinic3_hwif_write_reg(hwif, addr, mask_bits);
}

static void disable_all_msix(struct hinic3_hwdev *hwdev)
{
	u16 num_irqs = hwdev->hwif->attr.num_irqs;
	u16 i;

	for (i = 0; i < num_irqs; i++)
		hinic3_set_msix_state(hwdev, i, HINIC3_MSIX_DISABLE);
}

void hinic3_msix_intr_clear_resend_bit(struct hinic3_hwdev *hwdev, u16 msix_idx,
				       u8 clear_resend_en)
{
	struct hinic3_hwif *hwif;
	u32 msix_ctrl, addr;

	hwif = hwdev->hwif;

	msix_ctrl = HINIC3_MSI_CLR_INDIR_SET(msix_idx, SIMPLE_INDIR_IDX) |
		    HINIC3_MSI_CLR_INDIR_SET(clear_resend_en, RESEND_TIMER_CLR);

	addr = HINIC3_CSR_FUNC_MSI_CLR_WR_ADDR;
	hinic3_hwif_write_reg(hwif, addr, msix_ctrl);
}

void hinic3_set_msix_auto_mask_state(struct hinic3_hwdev *hwdev, u16 msix_idx,
				     enum hinic3_msix_auto_mask flag)
{
	struct hinic3_hwif *hwif;
	u32 mask_bits;
	u32 addr;

	hwif = hwdev->hwif;

	if (flag)
		mask_bits = HINIC3_MSI_CLR_INDIR_SET(1, AUTO_MSK_SET);
	else
		mask_bits = HINIC3_MSI_CLR_INDIR_SET(1, AUTO_MSK_CLR);

	mask_bits = mask_bits |
		    HINIC3_MSI_CLR_INDIR_SET(msix_idx, SIMPLE_INDIR_IDX);

	addr = HINIC3_CSR_FUNC_MSI_CLR_WR_ADDR;
	hinic3_hwif_write_reg(hwif, addr, mask_bits);
}

static enum hinic3_wait_return check_db_outbound_enable_handler(void *priv_data)
{
	enum hinic3_outbound_ctrl outbound_ctrl;
	struct hinic3_hwif *hwif = priv_data;
	enum hinic3_doorbell_ctrl db_ctrl;

	db_ctrl = hinic3_get_doorbell_ctrl_status(hwif);
	outbound_ctrl = hinic3_get_outbound_ctrl_status(hwif);
	if (outbound_ctrl == ENABLE_OUTBOUND && db_ctrl == ENABLE_DOORBELL)
		return HINIC3_WAIT_PROCESS_CPL;

	return HINIC3_WAIT_PROCESS_WAITING;
}

static int wait_until_doorbell_and_outbound_enabled(struct hinic3_hwif *hwif)
{
	return hinic3_wait_for_timeout(hwif, check_db_outbound_enable_handler,
				       HINIC3_DB_AND_OUTBOUND_EN_TIMEOUT,
				       USEC_PER_MSEC);
}

int hinic3_init_hwif(struct hinic3_hwdev *hwdev)
{
	struct hinic3_pcidev *pci_adapter = hwdev->adapter;
	struct hinic3_hwif *hwif;
	u32 attr1, attr4, attr5;
	int err;

	hwif = kzalloc(sizeof(*hwif), GFP_KERNEL);
	if (!hwif)
		return -ENOMEM;

	hwdev->hwif = hwif;
	hwif->cfg_regs_base = (u8 __iomem *)pci_adapter->cfg_reg_base +
			      HINIC3_VF_CFG_REG_OFFSET;

	err = db_area_idx_init(hwif, pci_adapter->db_base_phy,
			       pci_adapter->db_base,
			       pci_adapter->db_dwqe_len);
	if (err) {
		dev_err(hwdev->dev, "Failed to init db area.\n");
		goto err_free_hwif;
	}

	err = wait_hwif_ready(hwdev);
	if (err) {
		attr1 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR1_ADDR);
		dev_err(hwdev->dev, "Chip status is not ready, attr1:0x%x\n",
			attr1);
		goto err_free_db_area_idx;
	}

	err = init_hwif_attr(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Init hwif attr failed\n");
		goto err_free_db_area_idx;
	}

	err = wait_until_doorbell_and_outbound_enabled(hwif);
	if (err) {
		attr4 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR4_ADDR);
		attr5 = hinic3_hwif_read_reg(hwif, HINIC3_CSR_FUNC_ATTR5_ADDR);
		dev_err(hwdev->dev, "HW doorbell/outbound is disabled, attr4 0x%x attr5 0x%x\n",
			attr4, attr5);
		goto err_free_db_area_idx;
	}

	disable_all_msix(hwdev);

	return 0;

err_free_db_area_idx:
	db_area_idx_free(&hwif->db_area);
err_free_hwif:
	kfree(hwif);

	return err;
}

void hinic3_free_hwif(struct hinic3_hwdev *hwdev)
{
	db_area_idx_free(&hwdev->hwif->db_area);
	kfree(hwdev->hwif);
}

u16 hinic3_global_func_id(struct hinic3_hwdev *hwdev)
{
	return hwdev->hwif->attr.func_global_idx;
}
