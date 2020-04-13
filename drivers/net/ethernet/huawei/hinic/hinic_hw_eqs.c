// SPDX-License-Identifier: GPL-2.0-only
/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/log2.h>
#include <asm/byteorder.h>
#include <asm/barrier.h>

#include "hinic_hw_csr.h"
#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"

#define HINIC_EQS_WQ_NAME                       "hinic_eqs"

#define GET_EQ_NUM_PAGES(eq, pg_size)           \
		(ALIGN((eq)->q_len * (eq)->elem_size, pg_size) / (pg_size))

#define GET_EQ_NUM_ELEMS_IN_PG(eq, pg_size)     ((pg_size) / (eq)->elem_size)

#define EQ_CONS_IDX_REG_ADDR(eq)        (((eq)->type == HINIC_AEQ) ? \
			HINIC_CSR_AEQ_CONS_IDX_ADDR((eq)->q_id) : \
			HINIC_CSR_CEQ_CONS_IDX_ADDR((eq)->q_id))

#define EQ_PROD_IDX_REG_ADDR(eq)        (((eq)->type == HINIC_AEQ) ? \
			HINIC_CSR_AEQ_PROD_IDX_ADDR((eq)->q_id) : \
			HINIC_CSR_CEQ_PROD_IDX_ADDR((eq)->q_id))

#define EQ_HI_PHYS_ADDR_REG(eq, pg_num) (((eq)->type == HINIC_AEQ) ? \
			HINIC_CSR_AEQ_HI_PHYS_ADDR_REG((eq)->q_id, pg_num) : \
			HINIC_CSR_CEQ_HI_PHYS_ADDR_REG((eq)->q_id, pg_num))

#define EQ_LO_PHYS_ADDR_REG(eq, pg_num) (((eq)->type == HINIC_AEQ) ? \
			HINIC_CSR_AEQ_LO_PHYS_ADDR_REG((eq)->q_id, pg_num) : \
			HINIC_CSR_CEQ_LO_PHYS_ADDR_REG((eq)->q_id, pg_num))

#define GET_EQ_ELEMENT(eq, idx)         \
		((eq)->virt_addr[(idx) / (eq)->num_elem_in_pg] + \
		 (((idx) & ((eq)->num_elem_in_pg - 1)) * (eq)->elem_size))

#define GET_AEQ_ELEM(eq, idx)           ((struct hinic_aeq_elem *) \
					GET_EQ_ELEMENT(eq, idx))

#define GET_CEQ_ELEM(eq, idx)           ((u32 *) \
					 GET_EQ_ELEMENT(eq, idx))

#define GET_CURR_AEQ_ELEM(eq)           GET_AEQ_ELEM(eq, (eq)->cons_idx)

#define GET_CURR_CEQ_ELEM(eq)           GET_CEQ_ELEM(eq, (eq)->cons_idx)

#define PAGE_IN_4K(page_size)           ((page_size) >> 12)
#define EQ_SET_HW_PAGE_SIZE_VAL(eq)     (ilog2(PAGE_IN_4K((eq)->page_size)))

#define ELEMENT_SIZE_IN_32B(eq)         (((eq)->elem_size) >> 5)
#define EQ_SET_HW_ELEM_SIZE_VAL(eq)     (ilog2(ELEMENT_SIZE_IN_32B(eq)))

#define EQ_MAX_PAGES                    8

#define CEQE_TYPE_SHIFT                 23
#define CEQE_TYPE_MASK                  0x7

#define CEQE_TYPE(ceqe)                 (((ceqe) >> CEQE_TYPE_SHIFT) &  \
					 CEQE_TYPE_MASK)

#define CEQE_DATA_MASK                  0x3FFFFFF
#define CEQE_DATA(ceqe)                 ((ceqe) & CEQE_DATA_MASK)

#define aeq_to_aeqs(eq)                 \
		container_of((eq) - (eq)->q_id, struct hinic_aeqs, aeq[0])

#define ceq_to_ceqs(eq)                 \
		container_of((eq) - (eq)->q_id, struct hinic_ceqs, ceq[0])

#define work_to_aeq_work(work)          \
		container_of(work, struct hinic_eq_work, work)

#define DMA_ATTR_AEQ_DEFAULT            0
#define DMA_ATTR_CEQ_DEFAULT            0

/* No coalescence */
#define THRESH_CEQ_DEFAULT              0

enum eq_int_mode {
	EQ_INT_MODE_ARMED,
	EQ_INT_MODE_ALWAYS
};

enum eq_arm_state {
	EQ_NOT_ARMED,
	EQ_ARMED
};

/**
 * hinic_aeq_register_hw_cb - register AEQ callback for specific event
 * @aeqs: pointer to Async eqs of the chip
 * @event: aeq event to register callback for it
 * @handle: private data will be used by the callback
 * @hw_handler: callback function
 **/
void hinic_aeq_register_hw_cb(struct hinic_aeqs *aeqs,
			      enum hinic_aeq_type event, void *handle,
			      void (*hwe_handler)(void *handle, void *data,
						  u8 size))
{
	struct hinic_hw_event_cb *hwe_cb = &aeqs->hwe_cb[event];

	hwe_cb->hwe_handler = hwe_handler;
	hwe_cb->handle = handle;
	hwe_cb->hwe_state = HINIC_EQE_ENABLED;
}

/**
 * hinic_aeq_unregister_hw_cb - unregister the AEQ callback for specific event
 * @aeqs: pointer to Async eqs of the chip
 * @event: aeq event to unregister callback for it
 **/
void hinic_aeq_unregister_hw_cb(struct hinic_aeqs *aeqs,
				enum hinic_aeq_type event)
{
	struct hinic_hw_event_cb *hwe_cb = &aeqs->hwe_cb[event];

	hwe_cb->hwe_state &= ~HINIC_EQE_ENABLED;

	while (hwe_cb->hwe_state & HINIC_EQE_RUNNING)
		schedule();

	hwe_cb->hwe_handler = NULL;
}

/**
 * hinic_ceq_register_cb - register CEQ callback for specific event
 * @ceqs: pointer to Completion eqs part of the chip
 * @event: ceq event to register callback for it
 * @handle: private data will be used by the callback
 * @handler: callback function
 **/
void hinic_ceq_register_cb(struct hinic_ceqs *ceqs,
			   enum hinic_ceq_type event, void *handle,
			   void (*handler)(void *handle, u32 ceqe_data))
{
	struct hinic_ceq_cb *ceq_cb = &ceqs->ceq_cb[event];

	ceq_cb->handler = handler;
	ceq_cb->handle = handle;
	ceq_cb->ceqe_state = HINIC_EQE_ENABLED;
}

/**
 * hinic_ceq_unregister_cb - unregister the CEQ callback for specific event
 * @ceqs: pointer to Completion eqs part of the chip
 * @event: ceq event to unregister callback for it
 **/
void hinic_ceq_unregister_cb(struct hinic_ceqs *ceqs,
			     enum hinic_ceq_type event)
{
	struct hinic_ceq_cb *ceq_cb = &ceqs->ceq_cb[event];

	ceq_cb->ceqe_state &= ~HINIC_EQE_ENABLED;

	while (ceq_cb->ceqe_state & HINIC_EQE_RUNNING)
		schedule();

	ceq_cb->handler = NULL;
}

static u8 eq_cons_idx_checksum_set(u32 val)
{
	u8 checksum = 0;
	int idx;

	for (idx = 0; idx < 32; idx += 4)
		checksum ^= ((val >> idx) & 0xF);

	return (checksum & 0xF);
}

/**
 * eq_update_ci - update the HW cons idx of event queue
 * @eq: the event queue to update the cons idx for
 **/
static void eq_update_ci(struct hinic_eq *eq, u32 arm_state)
{
	u32 val, addr = EQ_CONS_IDX_REG_ADDR(eq);

	/* Read Modify Write */
	val = hinic_hwif_read_reg(eq->hwif, addr);

	val = HINIC_EQ_CI_CLEAR(val, IDX)       &
	      HINIC_EQ_CI_CLEAR(val, WRAPPED)   &
	      HINIC_EQ_CI_CLEAR(val, INT_ARMED) &
	      HINIC_EQ_CI_CLEAR(val, XOR_CHKSUM);

	val |= HINIC_EQ_CI_SET(eq->cons_idx, IDX)    |
	       HINIC_EQ_CI_SET(eq->wrapped, WRAPPED) |
	       HINIC_EQ_CI_SET(arm_state, INT_ARMED);

	val |= HINIC_EQ_CI_SET(eq_cons_idx_checksum_set(val), XOR_CHKSUM);

	hinic_hwif_write_reg(eq->hwif, addr, val);
}

/**
 * aeq_irq_handler - handler for the AEQ event
 * @eq: the Async Event Queue that received the event
 **/
static void aeq_irq_handler(struct hinic_eq *eq)
{
	struct hinic_aeqs *aeqs = aeq_to_aeqs(eq);
	struct hinic_hwif *hwif = aeqs->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_aeq_elem *aeqe_curr;
	struct hinic_hw_event_cb *hwe_cb;
	enum hinic_aeq_type event;
	unsigned long eqe_state;
	u32 aeqe_desc;
	int i, size;

	for (i = 0; i < eq->q_len; i++) {
		aeqe_curr = GET_CURR_AEQ_ELEM(eq);

		/* Data in HW is in Big endian Format */
		aeqe_desc = be32_to_cpu(aeqe_curr->desc);

		/* HW toggles the wrapped bit, when it adds eq element */
		if (HINIC_EQ_ELEM_DESC_GET(aeqe_desc, WRAPPED) == eq->wrapped)
			break;

		dma_rmb();

		event = HINIC_EQ_ELEM_DESC_GET(aeqe_desc, TYPE);
		if (event >= HINIC_MAX_AEQ_EVENTS) {
			dev_err(&pdev->dev, "Unknown AEQ Event %d\n", event);
			return;
		}

		if (!HINIC_EQ_ELEM_DESC_GET(aeqe_desc, SRC)) {
			hwe_cb = &aeqs->hwe_cb[event];

			size = HINIC_EQ_ELEM_DESC_GET(aeqe_desc, SIZE);

			eqe_state = cmpxchg(&hwe_cb->hwe_state,
					    HINIC_EQE_ENABLED,
					    HINIC_EQE_ENABLED |
					    HINIC_EQE_RUNNING);
			if ((eqe_state == HINIC_EQE_ENABLED) &&
			    (hwe_cb->hwe_handler))
				hwe_cb->hwe_handler(hwe_cb->handle,
						    aeqe_curr->data, size);
			else
				dev_err(&pdev->dev, "Unhandled AEQ Event %d\n",
					event);

			hwe_cb->hwe_state &= ~HINIC_EQE_RUNNING;
		}

		eq->cons_idx++;

		if (eq->cons_idx == eq->q_len) {
			eq->cons_idx = 0;
			eq->wrapped = !eq->wrapped;
		}
	}
}

/**
 * ceq_event_handler - handler for the ceq events
 * @ceqs: ceqs part of the chip
 * @ceqe: ceq element that describes the event
 **/
static void ceq_event_handler(struct hinic_ceqs *ceqs, u32 ceqe)
{
	struct hinic_hwif *hwif = ceqs->hwif;
	struct pci_dev *pdev = hwif->pdev;
	struct hinic_ceq_cb *ceq_cb;
	enum hinic_ceq_type event;
	unsigned long eqe_state;

	event = CEQE_TYPE(ceqe);
	if (event >= HINIC_MAX_CEQ_EVENTS) {
		dev_err(&pdev->dev, "Unknown CEQ event, event = %d\n", event);
		return;
	}

	ceq_cb = &ceqs->ceq_cb[event];

	eqe_state = cmpxchg(&ceq_cb->ceqe_state,
			    HINIC_EQE_ENABLED,
			    HINIC_EQE_ENABLED | HINIC_EQE_RUNNING);

	if ((eqe_state == HINIC_EQE_ENABLED) && (ceq_cb->handler))
		ceq_cb->handler(ceq_cb->handle, CEQE_DATA(ceqe));
	else
		dev_err(&pdev->dev, "Unhandled CEQ Event %d\n", event);

	ceq_cb->ceqe_state &= ~HINIC_EQE_RUNNING;
}

/**
 * ceq_irq_handler - handler for the CEQ event
 * @eq: the Completion Event Queue that received the event
 **/
static void ceq_irq_handler(struct hinic_eq *eq)
{
	struct hinic_ceqs *ceqs = ceq_to_ceqs(eq);
	u32 ceqe;
	int i;

	for (i = 0; i < eq->q_len; i++) {
		ceqe = *(GET_CURR_CEQ_ELEM(eq));

		/* Data in HW is in Big endian Format */
		ceqe = be32_to_cpu(ceqe);

		/* HW toggles the wrapped bit, when it adds eq element event */
		if (HINIC_EQ_ELEM_DESC_GET(ceqe, WRAPPED) == eq->wrapped)
			break;

		ceq_event_handler(ceqs, ceqe);

		eq->cons_idx++;

		if (eq->cons_idx == eq->q_len) {
			eq->cons_idx = 0;
			eq->wrapped = !eq->wrapped;
		}
	}
}

/**
 * eq_irq_handler - handler for the EQ event
 * @data: the Event Queue that received the event
 **/
static void eq_irq_handler(void *data)
{
	struct hinic_eq *eq = data;

	if (eq->type == HINIC_AEQ)
		aeq_irq_handler(eq);
	else if (eq->type == HINIC_CEQ)
		ceq_irq_handler(eq);

	eq_update_ci(eq, EQ_ARMED);
}

/**
 * eq_irq_work - the work of the EQ that received the event
 * @work: the work struct that is associated with the EQ
 **/
static void eq_irq_work(struct work_struct *work)
{
	struct hinic_eq_work *aeq_work = work_to_aeq_work(work);
	struct hinic_eq *aeq;

	aeq = aeq_work->data;
	eq_irq_handler(aeq);
}

/**
 * ceq_tasklet - the tasklet of the EQ that received the event
 * @ceq_data: the eq
 **/
static void ceq_tasklet(unsigned long ceq_data)
{
	struct hinic_eq *ceq = (struct hinic_eq *)ceq_data;

	eq_irq_handler(ceq);
}

/**
 * aeq_interrupt - aeq interrupt handler
 * @irq: irq number
 * @data: the Async Event Queue that collected the event
 **/
static irqreturn_t aeq_interrupt(int irq, void *data)
{
	struct hinic_eq_work *aeq_work;
	struct hinic_eq *aeq = data;
	struct hinic_aeqs *aeqs;

	/* clear resend timer cnt register */
	hinic_msix_attr_cnt_clear(aeq->hwif, aeq->msix_entry.entry);

	aeq_work = &aeq->aeq_work;
	aeq_work->data = aeq;

	aeqs = aeq_to_aeqs(aeq);
	queue_work(aeqs->workq, &aeq_work->work);

	return IRQ_HANDLED;
}

/**
 * ceq_interrupt - ceq interrupt handler
 * @irq: irq number
 * @data: the Completion Event Queue that collected the event
 **/
static irqreturn_t ceq_interrupt(int irq, void *data)
{
	struct hinic_eq *ceq = data;

	/* clear resend timer cnt register */
	hinic_msix_attr_cnt_clear(ceq->hwif, ceq->msix_entry.entry);

	tasklet_schedule(&ceq->ceq_tasklet);

	return IRQ_HANDLED;
}

static void set_ctrl0(struct hinic_eq *eq)
{
	struct msix_entry *msix_entry = &eq->msix_entry;
	enum hinic_eq_type type = eq->type;
	u32 addr, val, ctrl0;

	if (type == HINIC_AEQ) {
		/* RMW Ctrl0 */
		addr = HINIC_CSR_AEQ_CTRL_0_ADDR(eq->q_id);

		val = hinic_hwif_read_reg(eq->hwif, addr);

		val = HINIC_AEQ_CTRL_0_CLEAR(val, INT_IDX)      &
		      HINIC_AEQ_CTRL_0_CLEAR(val, DMA_ATTR)     &
		      HINIC_AEQ_CTRL_0_CLEAR(val, PCI_INTF_IDX) &
		      HINIC_AEQ_CTRL_0_CLEAR(val, INT_MODE);

		ctrl0 = HINIC_AEQ_CTRL_0_SET(msix_entry->entry, INT_IDX)     |
			HINIC_AEQ_CTRL_0_SET(DMA_ATTR_AEQ_DEFAULT, DMA_ATTR) |
			HINIC_AEQ_CTRL_0_SET(HINIC_HWIF_PCI_INTF(eq->hwif),
					     PCI_INTF_IDX)                   |
			HINIC_AEQ_CTRL_0_SET(EQ_INT_MODE_ARMED, INT_MODE);

		val |= ctrl0;

		hinic_hwif_write_reg(eq->hwif, addr, val);
	} else if (type == HINIC_CEQ) {
		/* RMW Ctrl0 */
		addr = HINIC_CSR_CEQ_CTRL_0_ADDR(eq->q_id);

		val = hinic_hwif_read_reg(eq->hwif, addr);

		val = HINIC_CEQ_CTRL_0_CLEAR(val, INTR_IDX)     &
		      HINIC_CEQ_CTRL_0_CLEAR(val, DMA_ATTR)     &
		      HINIC_CEQ_CTRL_0_CLEAR(val, KICK_THRESH)  &
		      HINIC_CEQ_CTRL_0_CLEAR(val, PCI_INTF_IDX) &
		      HINIC_CEQ_CTRL_0_CLEAR(val, INTR_MODE);

		ctrl0 = HINIC_CEQ_CTRL_0_SET(msix_entry->entry, INTR_IDX)     |
			HINIC_CEQ_CTRL_0_SET(DMA_ATTR_CEQ_DEFAULT, DMA_ATTR)  |
			HINIC_CEQ_CTRL_0_SET(THRESH_CEQ_DEFAULT, KICK_THRESH) |
			HINIC_CEQ_CTRL_0_SET(HINIC_HWIF_PCI_INTF(eq->hwif),
					     PCI_INTF_IDX)                    |
			HINIC_CEQ_CTRL_0_SET(EQ_INT_MODE_ARMED, INTR_MODE);

		val |= ctrl0;

		hinic_hwif_write_reg(eq->hwif, addr, val);
	}
}

static void set_ctrl1(struct hinic_eq *eq)
{
	enum hinic_eq_type type = eq->type;
	u32 page_size_val, elem_size;
	u32 addr, val, ctrl1;

	if (type == HINIC_AEQ) {
		/* RMW Ctrl1 */
		addr = HINIC_CSR_AEQ_CTRL_1_ADDR(eq->q_id);

		page_size_val = EQ_SET_HW_PAGE_SIZE_VAL(eq);
		elem_size = EQ_SET_HW_ELEM_SIZE_VAL(eq);

		val = hinic_hwif_read_reg(eq->hwif, addr);

		val = HINIC_AEQ_CTRL_1_CLEAR(val, LEN)          &
		      HINIC_AEQ_CTRL_1_CLEAR(val, ELEM_SIZE)    &
		      HINIC_AEQ_CTRL_1_CLEAR(val, PAGE_SIZE);

		ctrl1 = HINIC_AEQ_CTRL_1_SET(eq->q_len, LEN)            |
			HINIC_AEQ_CTRL_1_SET(elem_size, ELEM_SIZE)      |
			HINIC_AEQ_CTRL_1_SET(page_size_val, PAGE_SIZE);

		val |= ctrl1;

		hinic_hwif_write_reg(eq->hwif, addr, val);
	} else if (type == HINIC_CEQ) {
		/* RMW Ctrl1 */
		addr = HINIC_CSR_CEQ_CTRL_1_ADDR(eq->q_id);

		page_size_val = EQ_SET_HW_PAGE_SIZE_VAL(eq);

		val = hinic_hwif_read_reg(eq->hwif, addr);

		val = HINIC_CEQ_CTRL_1_CLEAR(val, LEN) &
		      HINIC_CEQ_CTRL_1_CLEAR(val, PAGE_SIZE);

		ctrl1 = HINIC_CEQ_CTRL_1_SET(eq->q_len, LEN) |
			HINIC_CEQ_CTRL_1_SET(page_size_val, PAGE_SIZE);

		val |= ctrl1;

		hinic_hwif_write_reg(eq->hwif, addr, val);
	}
}

/**
 * set_eq_ctrls - setting eq's ctrl registers
 * @eq: the Event Queue for setting
 **/
static void set_eq_ctrls(struct hinic_eq *eq)
{
	set_ctrl0(eq);
	set_ctrl1(eq);
}

/**
 * aeq_elements_init - initialize all the elements in the aeq
 * @eq: the Async Event Queue
 * @init_val: value to initialize the elements with it
 **/
static void aeq_elements_init(struct hinic_eq *eq, u32 init_val)
{
	struct hinic_aeq_elem *aeqe;
	int i;

	for (i = 0; i < eq->q_len; i++) {
		aeqe = GET_AEQ_ELEM(eq, i);
		aeqe->desc = cpu_to_be32(init_val);
	}

	wmb();  /* Write the initilzation values */
}

/**
 * ceq_elements_init - Initialize all the elements in the ceq
 * @eq: the event queue
 * @init_val: value to init with it the elements
 **/
static void ceq_elements_init(struct hinic_eq *eq, u32 init_val)
{
	u32 *ceqe;
	int i;

	for (i = 0; i < eq->q_len; i++) {
		ceqe = GET_CEQ_ELEM(eq, i);
		*(ceqe) = cpu_to_be32(init_val);
	}

	wmb();  /* Write the initilzation values */
}

/**
 * alloc_eq_pages - allocate the pages for the queue
 * @eq: the event queue
 *
 * Return 0 - Success, Negative - Failure
 **/
static int alloc_eq_pages(struct hinic_eq *eq)
{
	struct hinic_hwif *hwif = eq->hwif;
	struct pci_dev *pdev = hwif->pdev;
	u32 init_val, addr, val;
	size_t addr_size;
	int err, pg;

	addr_size = eq->num_pages * sizeof(*eq->dma_addr);
	eq->dma_addr = devm_kzalloc(&pdev->dev, addr_size, GFP_KERNEL);
	if (!eq->dma_addr)
		return -ENOMEM;

	addr_size = eq->num_pages * sizeof(*eq->virt_addr);
	eq->virt_addr = devm_kzalloc(&pdev->dev, addr_size, GFP_KERNEL);
	if (!eq->virt_addr) {
		err = -ENOMEM;
		goto err_virt_addr_alloc;
	}

	for (pg = 0; pg < eq->num_pages; pg++) {
		eq->virt_addr[pg] = dma_alloc_coherent(&pdev->dev,
						       eq->page_size,
						       &eq->dma_addr[pg],
						       GFP_KERNEL);
		if (!eq->virt_addr[pg]) {
			err = -ENOMEM;
			goto err_dma_alloc;
		}

		addr = EQ_HI_PHYS_ADDR_REG(eq, pg);
		val = upper_32_bits(eq->dma_addr[pg]);

		hinic_hwif_write_reg(hwif, addr, val);

		addr = EQ_LO_PHYS_ADDR_REG(eq, pg);
		val = lower_32_bits(eq->dma_addr[pg]);

		hinic_hwif_write_reg(hwif, addr, val);
	}

	init_val = HINIC_EQ_ELEM_DESC_SET(eq->wrapped, WRAPPED);

	if (eq->type == HINIC_AEQ)
		aeq_elements_init(eq, init_val);
	else if (eq->type == HINIC_CEQ)
		ceq_elements_init(eq, init_val);

	return 0;

err_dma_alloc:
	while (--pg >= 0)
		dma_free_coherent(&pdev->dev, eq->page_size,
				  eq->virt_addr[pg],
				  eq->dma_addr[pg]);

	devm_kfree(&pdev->dev, eq->virt_addr);

err_virt_addr_alloc:
	devm_kfree(&pdev->dev, eq->dma_addr);
	return err;
}

/**
 * free_eq_pages - free the pages of the queue
 * @eq: the Event Queue
 **/
static void free_eq_pages(struct hinic_eq *eq)
{
	struct hinic_hwif *hwif = eq->hwif;
	struct pci_dev *pdev = hwif->pdev;
	int pg;

	for (pg = 0; pg < eq->num_pages; pg++)
		dma_free_coherent(&pdev->dev, eq->page_size,
				  eq->virt_addr[pg],
				  eq->dma_addr[pg]);

	devm_kfree(&pdev->dev, eq->virt_addr);
	devm_kfree(&pdev->dev, eq->dma_addr);
}

/**
 * init_eq - initialize Event Queue
 * @eq: the event queue
 * @hwif: the HW interface of a PCI function device
 * @type: the type of the event queue, aeq or ceq
 * @q_id: Queue id number
 * @q_len: the number of EQ elements
 * @page_size: the page size of the pages in the event queue
 * @entry: msix entry associated with the event queue
 *
 * Return 0 - Success, Negative - Failure
 **/
static int init_eq(struct hinic_eq *eq, struct hinic_hwif *hwif,
		   enum hinic_eq_type type, int q_id, u32 q_len, u32 page_size,
		   struct msix_entry entry)
{
	struct pci_dev *pdev = hwif->pdev;
	int err;

	eq->hwif = hwif;
	eq->type = type;
	eq->q_id = q_id;
	eq->q_len = q_len;
	eq->page_size = page_size;

	/* Clear PI and CI, also clear the ARM bit */
	hinic_hwif_write_reg(eq->hwif, EQ_CONS_IDX_REG_ADDR(eq), 0);
	hinic_hwif_write_reg(eq->hwif, EQ_PROD_IDX_REG_ADDR(eq), 0);

	eq->cons_idx = 0;
	eq->wrapped = 0;

	if (type == HINIC_AEQ) {
		eq->elem_size = HINIC_AEQE_SIZE;
	} else if (type == HINIC_CEQ) {
		eq->elem_size = HINIC_CEQE_SIZE;
	} else {
		dev_err(&pdev->dev, "Invalid EQ type\n");
		return -EINVAL;
	}

	eq->num_pages = GET_EQ_NUM_PAGES(eq, page_size);
	eq->num_elem_in_pg = GET_EQ_NUM_ELEMS_IN_PG(eq, page_size);

	eq->msix_entry = entry;

	if (eq->num_elem_in_pg & (eq->num_elem_in_pg - 1)) {
		dev_err(&pdev->dev, "num elements in eq page != power of 2\n");
		return -EINVAL;
	}

	if (eq->num_pages > EQ_MAX_PAGES) {
		dev_err(&pdev->dev, "too many pages for eq\n");
		return -EINVAL;
	}

	set_eq_ctrls(eq);
	eq_update_ci(eq, EQ_ARMED);

	err = alloc_eq_pages(eq);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate pages for eq\n");
		return err;
	}

	if (type == HINIC_AEQ) {
		struct hinic_eq_work *aeq_work = &eq->aeq_work;

		INIT_WORK(&aeq_work->work, eq_irq_work);
	} else if (type == HINIC_CEQ) {
		tasklet_init(&eq->ceq_tasklet, ceq_tasklet,
			     (unsigned long)eq);
	}

	/* set the attributes of the msix entry */
	hinic_msix_attr_set(eq->hwif, eq->msix_entry.entry,
			    HINIC_EQ_MSIX_PENDING_LIMIT_DEFAULT,
			    HINIC_EQ_MSIX_COALESC_TIMER_DEFAULT,
			    HINIC_EQ_MSIX_LLI_TIMER_DEFAULT,
			    HINIC_EQ_MSIX_LLI_CREDIT_LIMIT_DEFAULT,
			    HINIC_EQ_MSIX_RESEND_TIMER_DEFAULT);

	if (type == HINIC_AEQ)
		err = request_irq(entry.vector, aeq_interrupt, 0,
				  "hinic_aeq", eq);
	else if (type == HINIC_CEQ)
		err = request_irq(entry.vector, ceq_interrupt, 0,
				  "hinic_ceq", eq);

	if (err) {
		dev_err(&pdev->dev, "Failed to request irq for the EQ\n");
		goto err_req_irq;
	}

	return 0;

err_req_irq:
	free_eq_pages(eq);
	return err;
}

/**
 * remove_eq - remove Event Queue
 * @eq: the event queue
 **/
static void remove_eq(struct hinic_eq *eq)
{
	hinic_set_msix_state(eq->hwif, eq->msix_entry.entry,
			     HINIC_MSIX_DISABLE);
	free_irq(eq->msix_entry.vector, eq);

	if (eq->type == HINIC_AEQ) {
		struct hinic_eq_work *aeq_work = &eq->aeq_work;

		cancel_work_sync(&aeq_work->work);
		/* clear aeq_len to avoid hw access host memory */
		hinic_hwif_write_reg(eq->hwif,
				     HINIC_CSR_AEQ_CTRL_1_ADDR(eq->q_id), 0);
	} else if (eq->type == HINIC_CEQ) {
		tasklet_kill(&eq->ceq_tasklet);
		/* clear ceq_len to avoid hw access host memory */
		hinic_hwif_write_reg(eq->hwif,
				     HINIC_CSR_CEQ_CTRL_1_ADDR(eq->q_id), 0);
	}

	/* update cons_idx to avoid invalid interrupt */
	eq->cons_idx = hinic_hwif_read_reg(eq->hwif, EQ_PROD_IDX_REG_ADDR(eq));
	eq_update_ci(eq, EQ_NOT_ARMED);

	free_eq_pages(eq);
}

/**
 * hinic_aeqs_init - initialize all the aeqs
 * @aeqs: pointer to Async eqs of the chip
 * @hwif: the HW interface of a PCI function device
 * @num_aeqs: number of AEQs
 * @q_len: number of EQ elements
 * @page_size: the page size of the pages in the event queue
 * @msix_entries: msix entries associated with the event queues
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_aeqs_init(struct hinic_aeqs *aeqs, struct hinic_hwif *hwif,
		    int num_aeqs, u32 q_len, u32 page_size,
		    struct msix_entry *msix_entries)
{
	struct pci_dev *pdev = hwif->pdev;
	int err, i, q_id;

	aeqs->workq = create_singlethread_workqueue(HINIC_EQS_WQ_NAME);
	if (!aeqs->workq)
		return -ENOMEM;

	aeqs->hwif = hwif;
	aeqs->num_aeqs = num_aeqs;

	for (q_id = 0; q_id < num_aeqs; q_id++) {
		err = init_eq(&aeqs->aeq[q_id], hwif, HINIC_AEQ, q_id, q_len,
			      page_size, msix_entries[q_id]);
		if (err) {
			dev_err(&pdev->dev, "Failed to init aeq %d\n", q_id);
			goto err_init_aeq;
		}
	}

	return 0;

err_init_aeq:
	for (i = 0; i < q_id; i++)
		remove_eq(&aeqs->aeq[i]);

	destroy_workqueue(aeqs->workq);
	return err;
}

/**
 * hinic_aeqs_free - free all the aeqs
 * @aeqs: pointer to Async eqs of the chip
 **/
void hinic_aeqs_free(struct hinic_aeqs *aeqs)
{
	int q_id;

	for (q_id = 0; q_id < aeqs->num_aeqs ; q_id++)
		remove_eq(&aeqs->aeq[q_id]);

	destroy_workqueue(aeqs->workq);
}

/**
 * hinic_ceqs_init - init all the ceqs
 * @ceqs: ceqs part of the chip
 * @hwif: the hardware interface of a pci function device
 * @num_ceqs: number of CEQs
 * @q_len: number of EQ elements
 * @page_size: the page size of the event queue
 * @msix_entries: msix entries associated with the event queues
 *
 * Return 0 - Success, Negative - Failure
 **/
int hinic_ceqs_init(struct hinic_ceqs *ceqs, struct hinic_hwif *hwif,
		    int num_ceqs, u32 q_len, u32 page_size,
		    struct msix_entry *msix_entries)
{
	struct pci_dev *pdev = hwif->pdev;
	int i, q_id, err;

	ceqs->hwif = hwif;
	ceqs->num_ceqs = num_ceqs;

	for (q_id = 0; q_id < num_ceqs; q_id++) {
		err = init_eq(&ceqs->ceq[q_id], hwif, HINIC_CEQ, q_id, q_len,
			      page_size, msix_entries[q_id]);
		if (err) {
			dev_err(&pdev->dev, "Failed to init ceq %d\n", q_id);
			goto err_init_ceq;
		}
	}

	return 0;

err_init_ceq:
	for (i = 0; i < q_id; i++)
		remove_eq(&ceqs->ceq[i]);

	return err;
}

/**
 * hinic_ceqs_free - free all the ceqs
 * @ceqs: ceqs part of the chip
 **/
void hinic_ceqs_free(struct hinic_ceqs *ceqs)
{
	int q_id;

	for (q_id = 0; q_id < ceqs->num_ceqs; q_id++)
		remove_eq(&ceqs->ceq[q_id]);
}
