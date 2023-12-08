// SPDX-License-Identifier: GPL-2.0
/*
 * mtu3_qmu.c - Queue Management Unit driver for device controller
 *
 * Copyright (C) 2016 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

/*
 * Queue Management Unit (QMU) is designed to unload SW effort
 * to serve DMA interrupts.
 * By preparing General Purpose Descriptor (GPD) and Buffer Descriptor (BD),
 * SW links data buffers and triggers QMU to send / receive data to
 * host / from device at a time.
 * And now only GPD is supported.
 *
 * For more detailed information, please refer to QMU Programming Guide
 */

#include <linux/dmapool.h>
#include <linux/iopoll.h>

#include "mtu3.h"
#include "mtu3_trace.h"

#define QMU_CHECKSUM_LEN	16

#define GPD_FLAGS_HWO	BIT(0)
#define GPD_FLAGS_BDP	BIT(1)
#define GPD_FLAGS_BPS	BIT(2)
#define GPD_FLAGS_ZLP	BIT(6)
#define GPD_FLAGS_IOC	BIT(7)
#define GET_GPD_HWO(gpd)	(le32_to_cpu((gpd)->dw0_info) & GPD_FLAGS_HWO)

#define GPD_RX_BUF_LEN_OG(x)	(((x) & 0xffff) << 16)
#define GPD_RX_BUF_LEN_EL(x)	(((x) & 0xfffff) << 12)
#define GPD_RX_BUF_LEN(mtu, x)	\
({				\
	typeof(x) x_ = (x);	\
	((mtu)->gen2cp) ? GPD_RX_BUF_LEN_EL(x_) : GPD_RX_BUF_LEN_OG(x_); \
})

#define GPD_DATA_LEN_OG(x)	((x) & 0xffff)
#define GPD_DATA_LEN_EL(x)	((x) & 0xfffff)
#define GPD_DATA_LEN(mtu, x)	\
({				\
	typeof(x) x_ = (x);	\
	((mtu)->gen2cp) ? GPD_DATA_LEN_EL(x_) : GPD_DATA_LEN_OG(x_); \
})

#define GPD_EXT_FLAG_ZLP	BIT(29)
#define GPD_EXT_NGP_OG(x)	(((x) & 0xf) << 20)
#define GPD_EXT_BUF_OG(x)	(((x) & 0xf) << 16)
#define GPD_EXT_NGP_EL(x)	(((x) & 0xf) << 28)
#define GPD_EXT_BUF_EL(x)	(((x) & 0xf) << 24)
#define GPD_EXT_NGP(mtu, x)	\
({				\
	typeof(x) x_ = (x);	\
	((mtu)->gen2cp) ? GPD_EXT_NGP_EL(x_) : GPD_EXT_NGP_OG(x_); \
})

#define GPD_EXT_BUF(mtu, x)	\
({				\
	typeof(x) x_ = (x);	\
	((mtu)->gen2cp) ? GPD_EXT_BUF_EL(x_) : GPD_EXT_BUF_OG(x_); \
})

#define HILO_GEN64(hi, lo) (((u64)(hi) << 32) + (lo))
#define HILO_DMA(hi, lo)	\
	((dma_addr_t)HILO_GEN64((le32_to_cpu(hi)), (le32_to_cpu(lo))))

static dma_addr_t read_txq_cur_addr(void __iomem *mbase, u8 epnum)
{
	u32 txcpr;
	u32 txhiar;

	txcpr = mtu3_readl(mbase, USB_QMU_TQCPR(epnum));
	txhiar = mtu3_readl(mbase, USB_QMU_TQHIAR(epnum));

	return HILO_DMA(QMU_CUR_GPD_ADDR_HI(txhiar), txcpr);
}

static dma_addr_t read_rxq_cur_addr(void __iomem *mbase, u8 epnum)
{
	u32 rxcpr;
	u32 rxhiar;

	rxcpr = mtu3_readl(mbase, USB_QMU_RQCPR(epnum));
	rxhiar = mtu3_readl(mbase, USB_QMU_RQHIAR(epnum));

	return HILO_DMA(QMU_CUR_GPD_ADDR_HI(rxhiar), rxcpr);
}

static void write_txq_start_addr(void __iomem *mbase, u8 epnum, dma_addr_t dma)
{
	u32 tqhiar;

	mtu3_writel(mbase, USB_QMU_TQSAR(epnum),
		    cpu_to_le32(lower_32_bits(dma)));
	tqhiar = mtu3_readl(mbase, USB_QMU_TQHIAR(epnum));
	tqhiar &= ~QMU_START_ADDR_HI_MSK;
	tqhiar |= QMU_START_ADDR_HI(upper_32_bits(dma));
	mtu3_writel(mbase, USB_QMU_TQHIAR(epnum), tqhiar);
}

static void write_rxq_start_addr(void __iomem *mbase, u8 epnum, dma_addr_t dma)
{
	u32 rqhiar;

	mtu3_writel(mbase, USB_QMU_RQSAR(epnum),
		    cpu_to_le32(lower_32_bits(dma)));
	rqhiar = mtu3_readl(mbase, USB_QMU_RQHIAR(epnum));
	rqhiar &= ~QMU_START_ADDR_HI_MSK;
	rqhiar |= QMU_START_ADDR_HI(upper_32_bits(dma));
	mtu3_writel(mbase, USB_QMU_RQHIAR(epnum), rqhiar);
}

static struct qmu_gpd *gpd_dma_to_virt(struct mtu3_gpd_ring *ring,
		dma_addr_t dma_addr)
{
	dma_addr_t dma_base = ring->dma;
	struct qmu_gpd *gpd_head = ring->start;
	u32 offset = (dma_addr - dma_base) / sizeof(*gpd_head);

	if (offset >= MAX_GPD_NUM)
		return NULL;

	return gpd_head + offset;
}

static dma_addr_t gpd_virt_to_dma(struct mtu3_gpd_ring *ring,
		struct qmu_gpd *gpd)
{
	dma_addr_t dma_base = ring->dma;
	struct qmu_gpd *gpd_head = ring->start;
	u32 offset;

	offset = gpd - gpd_head;
	if (offset >= MAX_GPD_NUM)
		return 0;

	return dma_base + (offset * sizeof(*gpd));
}

static void gpd_ring_init(struct mtu3_gpd_ring *ring, struct qmu_gpd *gpd)
{
	ring->start = gpd;
	ring->enqueue = gpd;
	ring->dequeue = gpd;
	ring->end = gpd + MAX_GPD_NUM - 1;
}

static void reset_gpd_list(struct mtu3_ep *mep)
{
	struct mtu3_gpd_ring *ring = &mep->gpd_ring;
	struct qmu_gpd *gpd = ring->start;

	if (gpd) {
		gpd->dw0_info &= cpu_to_le32(~GPD_FLAGS_HWO);
		gpd_ring_init(ring, gpd);
	}
}

int mtu3_gpd_ring_alloc(struct mtu3_ep *mep)
{
	struct qmu_gpd *gpd;
	struct mtu3_gpd_ring *ring = &mep->gpd_ring;

	/* software own all gpds as default */
	gpd = dma_pool_zalloc(mep->mtu->qmu_gpd_pool, GFP_ATOMIC, &ring->dma);
	if (gpd == NULL)
		return -ENOMEM;

	gpd_ring_init(ring, gpd);

	return 0;
}

void mtu3_gpd_ring_free(struct mtu3_ep *mep)
{
	struct mtu3_gpd_ring *ring = &mep->gpd_ring;

	dma_pool_free(mep->mtu->qmu_gpd_pool,
			ring->start, ring->dma);
	memset(ring, 0, sizeof(*ring));
}

void mtu3_qmu_resume(struct mtu3_ep *mep)
{
	struct mtu3 *mtu = mep->mtu;
	void __iomem *mbase = mtu->mac_base;
	int epnum = mep->epnum;
	u32 offset;

	offset = mep->is_in ? USB_QMU_TQCSR(epnum) : USB_QMU_RQCSR(epnum);

	mtu3_writel(mbase, offset, QMU_Q_RESUME);
	if (!(mtu3_readl(mbase, offset) & QMU_Q_ACTIVE))
		mtu3_writel(mbase, offset, QMU_Q_RESUME);
}

static struct qmu_gpd *advance_enq_gpd(struct mtu3_gpd_ring *ring)
{
	if (ring->enqueue < ring->end)
		ring->enqueue++;
	else
		ring->enqueue = ring->start;

	return ring->enqueue;
}

/* @dequeue may be NULL if ring is unallocated or freed */
static struct qmu_gpd *advance_deq_gpd(struct mtu3_gpd_ring *ring)
{
	if (ring->dequeue < ring->end)
		ring->dequeue++;
	else
		ring->dequeue = ring->start;

	return ring->dequeue;
}

/* check if a ring is emtpy */
static int gpd_ring_empty(struct mtu3_gpd_ring *ring)
{
	struct qmu_gpd *enq = ring->enqueue;
	struct qmu_gpd *next;

	if (ring->enqueue < ring->end)
		next = enq + 1;
	else
		next = ring->start;

	/* one gpd is reserved to simplify gpd preparation */
	return next == ring->dequeue;
}

int mtu3_prepare_transfer(struct mtu3_ep *mep)
{
	return gpd_ring_empty(&mep->gpd_ring);
}

static int mtu3_prepare_tx_gpd(struct mtu3_ep *mep, struct mtu3_request *mreq)
{
	struct qmu_gpd *enq;
	struct mtu3_gpd_ring *ring = &mep->gpd_ring;
	struct qmu_gpd *gpd = ring->enqueue;
	struct usb_request *req = &mreq->request;
	struct mtu3 *mtu = mep->mtu;
	dma_addr_t enq_dma;
	u32 ext_addr;

	gpd->dw0_info = 0;	/* SW own it */
	gpd->buffer = cpu_to_le32(lower_32_bits(req->dma));
	ext_addr = GPD_EXT_BUF(mtu, upper_32_bits(req->dma));
	gpd->dw3_info = cpu_to_le32(GPD_DATA_LEN(mtu, req->length));

	/* get the next GPD */
	enq = advance_enq_gpd(ring);
	enq_dma = gpd_virt_to_dma(ring, enq);
	dev_dbg(mep->mtu->dev, "TX-EP%d queue gpd=%p, enq=%p, qdma=%pad\n",
		mep->epnum, gpd, enq, &enq_dma);

	enq->dw0_info &= cpu_to_le32(~GPD_FLAGS_HWO);
	gpd->next_gpd = cpu_to_le32(lower_32_bits(enq_dma));
	ext_addr |= GPD_EXT_NGP(mtu, upper_32_bits(enq_dma));
	gpd->dw0_info = cpu_to_le32(ext_addr);

	if (req->zero) {
		if (mtu->gen2cp)
			gpd->dw0_info |= cpu_to_le32(GPD_FLAGS_ZLP);
		else
			gpd->dw3_info |= cpu_to_le32(GPD_EXT_FLAG_ZLP);
	}

	/* prevent reorder, make sure GPD's HWO is set last */
	mb();
	gpd->dw0_info |= cpu_to_le32(GPD_FLAGS_IOC | GPD_FLAGS_HWO);

	mreq->gpd = gpd;
	trace_mtu3_prepare_gpd(mep, gpd);

	return 0;
}

static int mtu3_prepare_rx_gpd(struct mtu3_ep *mep, struct mtu3_request *mreq)
{
	struct qmu_gpd *enq;
	struct mtu3_gpd_ring *ring = &mep->gpd_ring;
	struct qmu_gpd *gpd = ring->enqueue;
	struct usb_request *req = &mreq->request;
	struct mtu3 *mtu = mep->mtu;
	dma_addr_t enq_dma;
	u32 ext_addr;

	gpd->dw0_info = 0;	/* SW own it */
	gpd->buffer = cpu_to_le32(lower_32_bits(req->dma));
	ext_addr = GPD_EXT_BUF(mtu, upper_32_bits(req->dma));
	gpd->dw0_info = cpu_to_le32(GPD_RX_BUF_LEN(mtu, req->length));

	/* get the next GPD */
	enq = advance_enq_gpd(ring);
	enq_dma = gpd_virt_to_dma(ring, enq);
	dev_dbg(mep->mtu->dev, "RX-EP%d queue gpd=%p, enq=%p, qdma=%pad\n",
		mep->epnum, gpd, enq, &enq_dma);

	enq->dw0_info &= cpu_to_le32(~GPD_FLAGS_HWO);
	gpd->next_gpd = cpu_to_le32(lower_32_bits(enq_dma));
	ext_addr |= GPD_EXT_NGP(mtu, upper_32_bits(enq_dma));
	gpd->dw3_info = cpu_to_le32(ext_addr);
	/* prevent reorder, make sure GPD's HWO is set last */
	mb();
	gpd->dw0_info |= cpu_to_le32(GPD_FLAGS_IOC | GPD_FLAGS_HWO);

	mreq->gpd = gpd;
	trace_mtu3_prepare_gpd(mep, gpd);

	return 0;
}

void mtu3_insert_gpd(struct mtu3_ep *mep, struct mtu3_request *mreq)
{

	if (mep->is_in)
		mtu3_prepare_tx_gpd(mep, mreq);
	else
		mtu3_prepare_rx_gpd(mep, mreq);
}

int mtu3_qmu_start(struct mtu3_ep *mep)
{
	struct mtu3 *mtu = mep->mtu;
	void __iomem *mbase = mtu->mac_base;
	struct mtu3_gpd_ring *ring = &mep->gpd_ring;
	u8 epnum = mep->epnum;

	if (mep->is_in) {
		/* set QMU start address */
		write_txq_start_addr(mbase, epnum, ring->dma);
		mtu3_setbits(mbase, MU3D_EP_TXCR0(epnum), TX_DMAREQEN);
		/* send zero length packet according to ZLP flag in GPD */
		mtu3_setbits(mbase, U3D_QCR1, QMU_TX_ZLP(epnum));
		mtu3_writel(mbase, U3D_TQERRIESR0,
				QMU_TX_LEN_ERR(epnum) | QMU_TX_CS_ERR(epnum));

		if (mtu3_readl(mbase, USB_QMU_TQCSR(epnum)) & QMU_Q_ACTIVE) {
			dev_warn(mtu->dev, "Tx %d Active Now!\n", epnum);
			return 0;
		}
		mtu3_writel(mbase, USB_QMU_TQCSR(epnum), QMU_Q_START);

	} else {
		write_rxq_start_addr(mbase, epnum, ring->dma);
		mtu3_setbits(mbase, MU3D_EP_RXCR0(epnum), RX_DMAREQEN);
		/* don't expect ZLP */
		mtu3_clrbits(mbase, U3D_QCR3, QMU_RX_ZLP(epnum));
		/* move to next GPD when receive ZLP */
		mtu3_setbits(mbase, U3D_QCR3, QMU_RX_COZ(epnum));
		mtu3_writel(mbase, U3D_RQERRIESR0,
				QMU_RX_LEN_ERR(epnum) | QMU_RX_CS_ERR(epnum));
		mtu3_writel(mbase, U3D_RQERRIESR1, QMU_RX_ZLP_ERR(epnum));

		if (mtu3_readl(mbase, USB_QMU_RQCSR(epnum)) & QMU_Q_ACTIVE) {
			dev_warn(mtu->dev, "Rx %d Active Now!\n", epnum);
			return 0;
		}
		mtu3_writel(mbase, USB_QMU_RQCSR(epnum), QMU_Q_START);
	}

	return 0;
}

/* may called in atomic context */
void mtu3_qmu_stop(struct mtu3_ep *mep)
{
	struct mtu3 *mtu = mep->mtu;
	void __iomem *mbase = mtu->mac_base;
	int epnum = mep->epnum;
	u32 value = 0;
	u32 qcsr;
	int ret;

	qcsr = mep->is_in ? USB_QMU_TQCSR(epnum) : USB_QMU_RQCSR(epnum);

	if (!(mtu3_readl(mbase, qcsr) & QMU_Q_ACTIVE)) {
		dev_dbg(mtu->dev, "%s's qmu is inactive now!\n", mep->name);
		return;
	}
	mtu3_writel(mbase, qcsr, QMU_Q_STOP);

	ret = readl_poll_timeout_atomic(mbase + qcsr, value,
			!(value & QMU_Q_ACTIVE), 1, 1000);
	if (ret) {
		dev_err(mtu->dev, "stop %s's qmu failed\n", mep->name);
		return;
	}

	dev_dbg(mtu->dev, "%s's qmu stop now!\n", mep->name);
}

void mtu3_qmu_flush(struct mtu3_ep *mep)
{

	dev_dbg(mep->mtu->dev, "%s flush QMU %s\n", __func__,
		((mep->is_in) ? "TX" : "RX"));

	/*Stop QMU */
	mtu3_qmu_stop(mep);
	reset_gpd_list(mep);
}

/*
 * QMU can't transfer zero length packet directly (a hardware limit
 * on old SoCs), so when needs to send ZLP, we intentionally trigger
 * a length error interrupt, and in the ISR sends a ZLP by BMU.
 */
static void qmu_tx_zlp_error_handler(struct mtu3 *mtu, u8 epnum)
{
	struct mtu3_ep *mep = mtu->in_eps + epnum;
	struct mtu3_gpd_ring *ring = &mep->gpd_ring;
	void __iomem *mbase = mtu->mac_base;
	struct qmu_gpd *gpd_current = NULL;
	struct mtu3_request *mreq;
	dma_addr_t cur_gpd_dma;
	u32 txcsr = 0;
	int ret;

	mreq = next_request(mep);
	if (mreq && mreq->request.length != 0)
		return;

	cur_gpd_dma = read_txq_cur_addr(mbase, epnum);
	gpd_current = gpd_dma_to_virt(ring, cur_gpd_dma);

	if (GPD_DATA_LEN(mtu, le32_to_cpu(gpd_current->dw3_info)) != 0) {
		dev_err(mtu->dev, "TX EP%d buffer length error(!=0)\n", epnum);
		return;
	}

	dev_dbg(mtu->dev, "%s send ZLP for req=%p\n", __func__, mreq);
	trace_mtu3_zlp_exp_gpd(mep, gpd_current);

	mtu3_clrbits(mbase, MU3D_EP_TXCR0(mep->epnum), TX_DMAREQEN);

	ret = readl_poll_timeout_atomic(mbase + MU3D_EP_TXCR0(mep->epnum),
			txcsr, !(txcsr & TX_FIFOFULL), 1, 1000);
	if (ret) {
		dev_err(mtu->dev, "%s wait for fifo empty fail\n", __func__);
		return;
	}
	mtu3_setbits(mbase, MU3D_EP_TXCR0(mep->epnum), TX_TXPKTRDY);
	/* prevent reorder, make sure GPD's HWO is set last */
	mb();
	/* by pass the current GDP */
	gpd_current->dw0_info |= cpu_to_le32(GPD_FLAGS_BPS | GPD_FLAGS_HWO);

	/*enable DMAREQEN, switch back to QMU mode */
	mtu3_setbits(mbase, MU3D_EP_TXCR0(mep->epnum), TX_DMAREQEN);
	mtu3_qmu_resume(mep);
}

/*
 * NOTE: request list maybe is already empty as following case:
 * queue_tx --> qmu_interrupt(clear interrupt pending, schedule tasklet)-->
 * queue_tx --> process_tasklet(meanwhile, the second one is transferred,
 * tasklet process both of them)-->qmu_interrupt for second one.
 * To avoid upper case, put qmu_done_tx in ISR directly to process it.
 */
static void qmu_done_tx(struct mtu3 *mtu, u8 epnum)
{
	struct mtu3_ep *mep = mtu->in_eps + epnum;
	struct mtu3_gpd_ring *ring = &mep->gpd_ring;
	void __iomem *mbase = mtu->mac_base;
	struct qmu_gpd *gpd = ring->dequeue;
	struct qmu_gpd *gpd_current = NULL;
	struct usb_request *request = NULL;
	struct mtu3_request *mreq;
	dma_addr_t cur_gpd_dma;

	/*transfer phy address got from QMU register to virtual address */
	cur_gpd_dma = read_txq_cur_addr(mbase, epnum);
	gpd_current = gpd_dma_to_virt(ring, cur_gpd_dma);

	dev_dbg(mtu->dev, "%s EP%d, last=%p, current=%p, enq=%p\n",
		__func__, epnum, gpd, gpd_current, ring->enqueue);

	while (gpd && gpd != gpd_current && !GET_GPD_HWO(gpd)) {

		mreq = next_request(mep);

		if (mreq == NULL || mreq->gpd != gpd) {
			dev_err(mtu->dev, "no correct TX req is found\n");
			break;
		}

		request = &mreq->request;
		request->actual = GPD_DATA_LEN(mtu, le32_to_cpu(gpd->dw3_info));
		trace_mtu3_complete_gpd(mep, gpd);
		mtu3_req_complete(mep, request, 0);

		gpd = advance_deq_gpd(ring);
	}

	dev_dbg(mtu->dev, "%s EP%d, deq=%p, enq=%p, complete\n",
		__func__, epnum, ring->dequeue, ring->enqueue);

}

static void qmu_done_rx(struct mtu3 *mtu, u8 epnum)
{
	struct mtu3_ep *mep = mtu->out_eps + epnum;
	struct mtu3_gpd_ring *ring = &mep->gpd_ring;
	void __iomem *mbase = mtu->mac_base;
	struct qmu_gpd *gpd = ring->dequeue;
	struct qmu_gpd *gpd_current = NULL;
	struct usb_request *req = NULL;
	struct mtu3_request *mreq;
	dma_addr_t cur_gpd_dma;

	cur_gpd_dma = read_rxq_cur_addr(mbase, epnum);
	gpd_current = gpd_dma_to_virt(ring, cur_gpd_dma);

	dev_dbg(mtu->dev, "%s EP%d, last=%p, current=%p, enq=%p\n",
		__func__, epnum, gpd, gpd_current, ring->enqueue);

	while (gpd && gpd != gpd_current && !GET_GPD_HWO(gpd)) {

		mreq = next_request(mep);

		if (mreq == NULL || mreq->gpd != gpd) {
			dev_err(mtu->dev, "no correct RX req is found\n");
			break;
		}
		req = &mreq->request;

		req->actual = GPD_DATA_LEN(mtu, le32_to_cpu(gpd->dw3_info));
		trace_mtu3_complete_gpd(mep, gpd);
		mtu3_req_complete(mep, req, 0);

		gpd = advance_deq_gpd(ring);
	}

	dev_dbg(mtu->dev, "%s EP%d, deq=%p, enq=%p, complete\n",
		__func__, epnum, ring->dequeue, ring->enqueue);
}

static void qmu_done_isr(struct mtu3 *mtu, u32 done_status)
{
	int i;

	for (i = 1; i < mtu->num_eps; i++) {
		if (done_status & QMU_RX_DONE_INT(i))
			qmu_done_rx(mtu, i);
		if (done_status & QMU_TX_DONE_INT(i))
			qmu_done_tx(mtu, i);
	}
}

static void qmu_exception_isr(struct mtu3 *mtu, u32 qmu_status)
{
	void __iomem *mbase = mtu->mac_base;
	u32 errval;
	int i;

	if ((qmu_status & RXQ_CSERR_INT) || (qmu_status & RXQ_LENERR_INT)) {
		errval = mtu3_readl(mbase, U3D_RQERRIR0);
		for (i = 1; i < mtu->num_eps; i++) {
			if (errval & QMU_RX_CS_ERR(i))
				dev_err(mtu->dev, "Rx %d CS error!\n", i);

			if (errval & QMU_RX_LEN_ERR(i))
				dev_err(mtu->dev, "RX %d Length error\n", i);
		}
		mtu3_writel(mbase, U3D_RQERRIR0, errval);
	}

	if (qmu_status & RXQ_ZLPERR_INT) {
		errval = mtu3_readl(mbase, U3D_RQERRIR1);
		for (i = 1; i < mtu->num_eps; i++) {
			if (errval & QMU_RX_ZLP_ERR(i))
				dev_dbg(mtu->dev, "RX EP%d Recv ZLP\n", i);
		}
		mtu3_writel(mbase, U3D_RQERRIR1, errval);
	}

	if ((qmu_status & TXQ_CSERR_INT) || (qmu_status & TXQ_LENERR_INT)) {
		errval = mtu3_readl(mbase, U3D_TQERRIR0);
		for (i = 1; i < mtu->num_eps; i++) {
			if (errval & QMU_TX_CS_ERR(i))
				dev_err(mtu->dev, "Tx %d checksum error!\n", i);

			if (errval & QMU_TX_LEN_ERR(i))
				qmu_tx_zlp_error_handler(mtu, i);
		}
		mtu3_writel(mbase, U3D_TQERRIR0, errval);
	}
}

irqreturn_t mtu3_qmu_isr(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;
	u32 qmu_status;
	u32 qmu_done_status;

	/* U3D_QISAR1 is read update */
	qmu_status = mtu3_readl(mbase, U3D_QISAR1);
	qmu_status &= mtu3_readl(mbase, U3D_QIER1);

	qmu_done_status = mtu3_readl(mbase, U3D_QISAR0);
	qmu_done_status &= mtu3_readl(mbase, U3D_QIER0);
	mtu3_writel(mbase, U3D_QISAR0, qmu_done_status); /* W1C */
	dev_dbg(mtu->dev, "=== QMUdone[tx=%x, rx=%x] QMUexp[%x] ===\n",
		(qmu_done_status & 0xFFFF), qmu_done_status >> 16,
		qmu_status);
	trace_mtu3_qmu_isr(qmu_done_status, qmu_status);

	if (qmu_done_status)
		qmu_done_isr(mtu, qmu_done_status);

	if (qmu_status)
		qmu_exception_isr(mtu, qmu_status);

	return IRQ_HANDLED;
}

int mtu3_qmu_init(struct mtu3 *mtu)
{

	compiletime_assert(QMU_GPD_SIZE == 16, "QMU_GPD size SHOULD be 16B");

	mtu->qmu_gpd_pool = dma_pool_create("QMU_GPD", mtu->dev,
			QMU_GPD_RING_SIZE, QMU_GPD_SIZE, 0);

	if (!mtu->qmu_gpd_pool)
		return -ENOMEM;

	return 0;
}

void mtu3_qmu_exit(struct mtu3 *mtu)
{
	dma_pool_destroy(mtu->qmu_gpd_pool);
}
