/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence USBHS-DEV Driver.
 * Debug header file.
 *
 * Copyright (C) 2023 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 */

#ifndef __LINUX_CDNS2_DEBUG
#define __LINUX_CDNS2_DEBUG

static inline const char *cdns2_decode_usb_irq(char *str, size_t size,
					       u8 usb_irq, u8 ext_irq)
{
	int ret;

	ret = snprintf(str, size, "usbirq: 0x%02x - ", usb_irq);

	if (usb_irq & USBIRQ_SOF)
		ret += snprintf(str + ret, size - ret, "SOF ");
	if (usb_irq & USBIRQ_SUTOK)
		ret += snprintf(str + ret, size - ret, "SUTOK ");
	if (usb_irq & USBIRQ_SUDAV)
		ret += snprintf(str + ret, size - ret, "SETUP ");
	if (usb_irq & USBIRQ_SUSPEND)
		ret += snprintf(str + ret, size - ret, "Suspend ");
	if (usb_irq & USBIRQ_URESET)
		ret += snprintf(str + ret, size - ret, "Reset ");
	if (usb_irq & USBIRQ_HSPEED)
		ret += snprintf(str + ret, size - ret, "HS ");
	if (usb_irq & USBIRQ_LPM)
		ret += snprintf(str + ret, size - ret, "LPM ");

	ret += snprintf(str + ret, size - ret, ", EXT: 0x%02x - ", ext_irq);

	if (ext_irq & EXTIRQ_WAKEUP)
		ret += snprintf(str + ret, size - ret, "Wakeup ");
	if (ext_irq & EXTIRQ_VBUSFAULT_FALL)
		ret += snprintf(str + ret, size - ret, "VBUS_FALL ");
	if (ext_irq & EXTIRQ_VBUSFAULT_RISE)
		ret += snprintf(str + ret, size - ret, "VBUS_RISE ");

	if (ret >= size)
		pr_info("CDNS2: buffer overflowed.\n");

	return str;
}

static inline const char *cdns2_decode_dma_irq(char *str, size_t size,
					       u32 ep_ists, u32 ep_sts,
					       const char *ep_name)
{
	int ret;

	ret = snprintf(str, size, "ISTS: %08x, %s: %08x ",
		       ep_ists, ep_name, ep_sts);

	if (ep_sts & DMA_EP_STS_IOC)
		ret += snprintf(str + ret, size - ret, "IOC ");
	if (ep_sts & DMA_EP_STS_ISP)
		ret += snprintf(str + ret, size - ret, "ISP ");
	if (ep_sts & DMA_EP_STS_DESCMIS)
		ret += snprintf(str + ret, size - ret, "DESCMIS ");
	if (ep_sts & DMA_EP_STS_TRBERR)
		ret += snprintf(str + ret, size - ret, "TRBERR ");
	if (ep_sts & DMA_EP_STS_OUTSMM)
		ret += snprintf(str + ret, size - ret, "OUTSMM ");
	if (ep_sts & DMA_EP_STS_ISOERR)
		ret += snprintf(str + ret, size - ret, "ISOERR ");
	if (ep_sts & DMA_EP_STS_DBUSY)
		ret += snprintf(str + ret, size - ret, "DBUSY ");
	if (DMA_EP_STS_CCS(ep_sts))
		ret += snprintf(str + ret, size - ret, "CCS ");

	if (ret >= size)
		pr_info("CDNS2: buffer overflowed.\n");

	return str;
}

static inline const char *cdns2_decode_epx_irq(char *str, size_t size,
					       char *ep_name, u32 ep_ists,
					       u32 ep_sts)
{
	return cdns2_decode_dma_irq(str, size, ep_ists, ep_sts, ep_name);
}

static inline const char *cdns2_decode_ep0_irq(char *str, size_t size,
					       u32 ep_ists, u32 ep_sts,
					       int dir)
{
	return cdns2_decode_dma_irq(str, size, ep_ists, ep_sts,
				    dir ? "ep0IN" : "ep0OUT");
}

static inline const char *cdns2_raw_ring(struct cdns2_endpoint *pep,
					 struct cdns2_trb *trbs,
					 char *str, size_t size)
{
	struct cdns2_ring *ring = &pep->ring;
	struct cdns2_trb *trb;
	dma_addr_t dma;
	int ret;
	int i;

	ret = snprintf(str, size, "\n\t\tTR for %s:", pep->name);

	trb = &trbs[ring->dequeue];
	dma = cdns2_trb_virt_to_dma(pep, trb);
	ret += snprintf(str + ret, size - ret,
			"\n\t\tRing deq index: %d, trb: V=%p, P=0x%pad\n",
			ring->dequeue, trb, &dma);

	trb = &trbs[ring->enqueue];
	dma = cdns2_trb_virt_to_dma(pep, trb);
	ret += snprintf(str + ret, size - ret,
			"\t\tRing enq index: %d, trb: V=%p, P=0x%pad\n",
			ring->enqueue, trb, &dma);

	ret += snprintf(str + ret, size - ret,
			"\t\tfree trbs: %d, CCS=%d, PCS=%d\n",
			ring->free_trbs, ring->ccs, ring->pcs);

	if (TRBS_PER_SEGMENT > 40) {
		ret += snprintf(str + ret, size - ret,
				"\t\tTransfer ring %d too big\n", TRBS_PER_SEGMENT);
		return str;
	}

	dma = ring->dma;
	for (i = 0; i < TRBS_PER_SEGMENT; ++i) {
		trb = &trbs[i];
		ret += snprintf(str + ret, size - ret,
				"\t\t@%pad %08x %08x %08x\n", &dma,
				le32_to_cpu(trb->buffer),
				le32_to_cpu(trb->length),
				le32_to_cpu(trb->control));
		dma += sizeof(*trb);
	}

	if (ret >= size)
		pr_info("CDNS2: buffer overflowed.\n");

	return str;
}

static inline const char *cdns2_trb_type_string(u8 type)
{
	switch (type) {
	case TRB_NORMAL:
		return "Normal";
	case TRB_LINK:
		return "Link";
	default:
		return "UNKNOWN";
	}
}

static inline const char *cdns2_decode_trb(char *str, size_t size, u32 flags,
					   u32 length, u32 buffer)
{
	int type = TRB_FIELD_TO_TYPE(flags);
	int ret;

	switch (type) {
	case TRB_LINK:
		ret = snprintf(str, size,
			       "LINK %08x type '%s' flags %c:%c:%c%c:%c",
			       buffer, cdns2_trb_type_string(type),
			       flags & TRB_CYCLE ? 'C' : 'c',
			       flags & TRB_TOGGLE ? 'T' : 't',
			       flags & TRB_CHAIN ? 'C' : 'c',
			       flags & TRB_CHAIN ? 'H' : 'h',
			       flags & TRB_IOC ? 'I' : 'i');
		break;
	case TRB_NORMAL:
		ret = snprintf(str, size,
			       "type: '%s', Buffer: %08x, length: %ld, burst len: %ld, "
			       "flags %c:%c:%c%c:%c",
			       cdns2_trb_type_string(type),
			       buffer, TRB_LEN(length),
			       TRB_FIELD_TO_BURST(length),
			       flags & TRB_CYCLE ? 'C' : 'c',
			       flags & TRB_ISP ? 'I' : 'i',
			       flags & TRB_CHAIN ? 'C' : 'c',
			       flags & TRB_CHAIN ? 'H' : 'h',
			       flags & TRB_IOC ? 'I' : 'i');
		break;
	default:
		ret = snprintf(str, size, "type '%s' -> raw %08x %08x %08x",
			       cdns2_trb_type_string(type),
			       buffer, length, flags);
	}

	if (ret >= size)
		pr_info("CDNS2: buffer overflowed.\n");

	return str;
}

#endif /*__LINUX_CDNS2_DEBUG*/
