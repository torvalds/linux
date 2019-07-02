/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence USBSS DRD Driver.
 * Debug header file.
 *
 * Copyright (C) 2018-2019 Cadence.
 *
 * Author: Pawel Laszczak <pawell@cadence.com>
 */
#ifndef __LINUX_CDNS3_DEBUG
#define __LINUX_CDNS3_DEBUG

#include "core.h"

static inline char *cdns3_decode_usb_irq(char *str,
					 enum usb_device_speed speed,
					 u32 usb_ists)
{
	int ret;

	ret = sprintf(str, "IRQ %08x = ", usb_ists);

	if (usb_ists & (USB_ISTS_CON2I | USB_ISTS_CONI)) {
		ret += sprintf(str + ret, "Connection %s\n",
			       usb_speed_string(speed));
	}
	if (usb_ists & USB_ISTS_DIS2I || usb_ists & USB_ISTS_DISI)
		ret += sprintf(str + ret, "Disconnection ");
	if (usb_ists & USB_ISTS_L2ENTI)
		ret += sprintf(str + ret, "suspended ");
	if (usb_ists & USB_ISTS_L1ENTI)
		ret += sprintf(str + ret, "L1 enter ");
	if (usb_ists & USB_ISTS_L1EXTI)
		ret += sprintf(str + ret, "L1 exit ");
	if (usb_ists & USB_ISTS_L2ENTI)
		ret += sprintf(str + ret, "L2 enter ");
	if (usb_ists & USB_ISTS_L2EXTI)
		ret += sprintf(str + ret, "L2 exit ");
	if (usb_ists & USB_ISTS_U3EXTI)
		ret += sprintf(str + ret, "U3 exit ");
	if (usb_ists & USB_ISTS_UWRESI)
		ret += sprintf(str + ret, "Warm Reset ");
	if (usb_ists & USB_ISTS_UHRESI)
		ret += sprintf(str + ret, "Hot Reset ");
	if (usb_ists & USB_ISTS_U2RESI)
		ret += sprintf(str + ret, "Reset");

	return str;
}

static inline  char *cdns3_decode_ep_irq(char *str,
					 u32 ep_sts,
					 const char *ep_name)
{
	int ret;

	ret = sprintf(str, "IRQ for %s: %08x ", ep_name, ep_sts);

	if (ep_sts & EP_STS_SETUP)
		ret += sprintf(str + ret, "SETUP ");
	if (ep_sts & EP_STS_IOC)
		ret += sprintf(str + ret, "IOC ");
	if (ep_sts & EP_STS_ISP)
		ret += sprintf(str + ret, "ISP ");
	if (ep_sts & EP_STS_DESCMIS)
		ret += sprintf(str + ret, "DESCMIS ");
	if (ep_sts & EP_STS_STREAMR)
		ret += sprintf(str + ret, "STREAMR ");
	if (ep_sts & EP_STS_MD_EXIT)
		ret += sprintf(str + ret, "MD_EXIT ");
	if (ep_sts & EP_STS_TRBERR)
		ret += sprintf(str + ret, "TRBERR ");
	if (ep_sts & EP_STS_NRDY)
		ret += sprintf(str + ret, "NRDY ");
	if (ep_sts & EP_STS_PRIME)
		ret += sprintf(str + ret, "PRIME ");
	if (ep_sts & EP_STS_SIDERR)
		ret += sprintf(str + ret, "SIDERRT ");
	if (ep_sts & EP_STS_OUTSMM)
		ret += sprintf(str + ret, "OUTSMM ");
	if (ep_sts & EP_STS_ISOERR)
		ret += sprintf(str + ret, "ISOERR ");
	if (ep_sts & EP_STS_IOT)
		ret += sprintf(str + ret, "IOT ");

	return str;
}

static inline char *cdns3_decode_epx_irq(char *str,
					 char *ep_name,
					 u32 ep_sts)
{
	return cdns3_decode_ep_irq(str, ep_sts, ep_name);
}

static inline char *cdns3_decode_ep0_irq(char *str,
					 int dir,
					 u32 ep_sts)
{
	return cdns3_decode_ep_irq(str, ep_sts,
				   dir ? "ep0IN" : "ep0OUT");
}

/**
 * Debug a transfer ring.
 *
 * Prints out all TRBs in the endpoint ring, even those after the Link TRB.
 *.
 */
static inline char *cdns3_dbg_ring(struct cdns3_endpoint *priv_ep,
				   struct cdns3_trb *ring, char *str)
{
	dma_addr_t addr = priv_ep->trb_pool_dma;
	struct cdns3_trb *trb;
	int trb_per_sector;
	int ret = 0;
	int i;

	trb_per_sector = GET_TRBS_PER_SEGMENT(priv_ep->type);

	trb = &priv_ep->trb_pool[priv_ep->dequeue];
	ret += sprintf(str + ret, "\n\t\tRing contents for %s:", priv_ep->name);

	ret += sprintf(str + ret,
		       "\n\t\tRing deq index: %d, trb: %p (virt), 0x%llx (dma)\n",
		       priv_ep->dequeue, trb,
		       (unsigned long long)cdns3_trb_virt_to_dma(priv_ep, trb));

	trb = &priv_ep->trb_pool[priv_ep->enqueue];
	ret += sprintf(str + ret,
		       "\t\tRing enq index: %d, trb: %p (virt), 0x%llx (dma)\n",
		       priv_ep->enqueue, trb,
		       (unsigned long long)cdns3_trb_virt_to_dma(priv_ep, trb));

	ret += sprintf(str + ret,
		       "\t\tfree trbs: %d, CCS=%d, PCS=%d\n",
		       priv_ep->free_trbs, priv_ep->ccs, priv_ep->pcs);

	if (trb_per_sector > TRBS_PER_SEGMENT)
		trb_per_sector = TRBS_PER_SEGMENT;

	if (trb_per_sector > TRBS_PER_SEGMENT) {
		sprintf(str + ret, "\t\tTo big transfer ring %d\n",
			trb_per_sector);
		return str;
	}

	for (i = 0; i < trb_per_sector; ++i) {
		trb = &ring[i];
		ret += sprintf(str + ret,
			"\t\t@%pad %08x %08x %08x\n", &addr,
			le32_to_cpu(trb->buffer),
			le32_to_cpu(trb->length),
			le32_to_cpu(trb->control));
		addr += sizeof(*trb);
	}

	return str;
}

void cdns3_dbg(struct cdns3_device *priv_dev, const char *fmt, ...);

#ifdef CONFIG_DEBUG_FS
void cdns3_debugfs_init(struct cdns3 *cdns);
void cdns3_debugfs_exit(struct cdns3 *cdns);
#else
void cdns3_debugfs_init(struct cdns3 *cdns);
{  }
void cdns3_debugfs_exit(struct cdns3 *cdns);
{  }
#endif

#endif /*__LINUX_CDNS3_DEBUG*/
