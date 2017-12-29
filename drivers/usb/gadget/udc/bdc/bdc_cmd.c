// SPDX-License-Identifier: GPL-2.0+
/*
 * bdc_cmd.c - BRCM BDC USB3.0 device controller
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * Author: Ashwini Pahuja
 */
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "bdc.h"
#include "bdc_cmd.h"
#include "bdc_dbg.h"

/* Issues a cmd to cmd processor and waits for cmd completion */
static int bdc_issue_cmd(struct bdc *bdc, u32 cmd_sc, u32 param0,
							u32 param1, u32 param2)
{
	u32 timeout = BDC_CMD_TIMEOUT;
	u32 cmd_status;
	u32 temp;

	bdc_writel(bdc->regs, BDC_CMDPAR0, param0);
	bdc_writel(bdc->regs, BDC_CMDPAR1, param1);
	bdc_writel(bdc->regs, BDC_CMDPAR2, param2);

	/* Issue the cmd */
	/* Make sure the cmd params are written before asking HW to exec cmd */
	wmb();
	bdc_writel(bdc->regs, BDC_CMDSC, cmd_sc | BDC_CMD_CWS | BDC_CMD_SRD);
	do {
		temp = bdc_readl(bdc->regs, BDC_CMDSC);
		dev_dbg_ratelimited(bdc->dev, "cmdsc=%x", temp);
		cmd_status =  BDC_CMD_CST(temp);
		if (cmd_status != BDC_CMDS_BUSY)  {
			dev_dbg(bdc->dev,
				"command completed cmd_sts:%x\n", cmd_status);
			return cmd_status;
		}
		udelay(1);
	} while (timeout--);

	dev_err(bdc->dev,
		"command operation timedout cmd_status=%d\n", cmd_status);

	return cmd_status;
}

/* Submits cmd and analyze the return value of bdc_issue_cmd */
static int bdc_submit_cmd(struct bdc *bdc, u32 cmd_sc,
					u32 param0, u32 param1,	u32 param2)
{
	u32 temp, cmd_status;
	int ret;

	temp = bdc_readl(bdc->regs, BDC_CMDSC);
	dev_dbg(bdc->dev,
		"%s:CMDSC:%08x cmdsc:%08x param0=%08x param1=%08x param2=%08x\n",
		 __func__, temp, cmd_sc, param0, param1, param2);

	cmd_status = BDC_CMD_CST(temp);
	if (cmd_status  ==  BDC_CMDS_BUSY) {
		dev_err(bdc->dev, "command processor busy: %x\n", cmd_status);
		return -EBUSY;
	}
	ret = bdc_issue_cmd(bdc, cmd_sc, param0, param1, param2);
	switch (ret) {
	case BDC_CMDS_SUCC:
		dev_dbg(bdc->dev, "command completed successfully\n");
		ret = 0;
		break;

	case BDC_CMDS_PARA:
		dev_err(bdc->dev, "command parameter error\n");
		ret = -EINVAL;
		break;

	case BDC_CMDS_STAT:
		dev_err(bdc->dev, "Invalid device/ep state\n");
		ret = -EINVAL;
		break;

	case BDC_CMDS_FAIL:
		dev_err(bdc->dev, "Command failed?\n");
		ret = -EAGAIN;
		break;

	case BDC_CMDS_INTL:
		dev_err(bdc->dev, "BDC Internal error\n");
		ret = -ECONNRESET;
		break;

	case BDC_CMDS_BUSY:
		dev_err(bdc->dev,
			"command timedout waited for %dusec\n",
			BDC_CMD_TIMEOUT);
		ret = -ECONNRESET;
		break;
	default:
		dev_dbg(bdc->dev, "Unknown command completion code:%x\n", ret);
	}

	return ret;
}

/* Deconfigure the endpoint from HW */
int bdc_dconfig_ep(struct bdc *bdc, struct bdc_ep *ep)
{
	u32 cmd_sc;

	cmd_sc = BDC_SUB_CMD_DRP_EP|BDC_CMD_EPN(ep->ep_num)|BDC_CMD_EPC;
	dev_dbg(bdc->dev, "%s ep->ep_num =%d cmd_sc=%x\n", __func__,
							ep->ep_num, cmd_sc);

	return bdc_submit_cmd(bdc, cmd_sc, 0, 0, 0);
}

/* Reinitalize the bdlist after config ep command */
static void ep_bd_list_reinit(struct bdc_ep *ep)
{
	struct bdc *bdc = ep->bdc;
	struct bdc_bd *bd;

	ep->bd_list.eqp_bdi = 0;
	ep->bd_list.hwd_bdi = 0;
	bd = ep->bd_list.bd_table_array[0]->start_bd;
	dev_dbg(bdc->dev, "%s ep:%p bd:%p\n", __func__, ep, bd);
	memset(bd, 0, sizeof(struct bdc_bd));
	bd->offset[3] |= cpu_to_le32(BD_SBF);
}

/* Configure an endpoint */
int bdc_config_ep(struct bdc *bdc, struct bdc_ep *ep)
{
	const struct usb_ss_ep_comp_descriptor *comp_desc;
	const struct usb_endpoint_descriptor	*desc;
	u32 param0, param1, param2, cmd_sc;
	u32 mps, mbs, mul, si;
	int ret;

	desc = ep->desc;
	comp_desc = ep->comp_desc;
	cmd_sc = mul = mbs = param2 = 0;
	param0 = lower_32_bits(ep->bd_list.bd_table_array[0]->dma);
	param1 = upper_32_bits(ep->bd_list.bd_table_array[0]->dma);
	cpu_to_le32s(&param0);
	cpu_to_le32s(&param1);

	dev_dbg(bdc->dev, "%s: param0=%08x param1=%08x",
						__func__, param0, param1);
	si = desc->bInterval;
	si = clamp_val(si, 1, 16) - 1;

	mps = usb_endpoint_maxp(desc);
	mps &= 0x7ff;
	param2 |= mps << MP_SHIFT;
	param2 |= usb_endpoint_type(desc) << EPT_SHIFT;

	switch (bdc->gadget.speed) {
	case USB_SPEED_SUPER:
		if (usb_endpoint_xfer_int(desc) ||
					usb_endpoint_xfer_isoc(desc)) {
			param2 |= si;
			if (usb_endpoint_xfer_isoc(desc) && comp_desc)
					mul = comp_desc->bmAttributes;

		}
		param2 |= mul << EPM_SHIFT;
		if (comp_desc)
			mbs = comp_desc->bMaxBurst;
		param2 |= mbs << MB_SHIFT;
		break;

	case USB_SPEED_HIGH:
		if (usb_endpoint_xfer_isoc(desc) ||
					usb_endpoint_xfer_int(desc)) {
			param2 |= si;

			mbs = usb_endpoint_maxp_mult(desc);
			param2 |= mbs << MB_SHIFT;
		}
		break;

	case USB_SPEED_FULL:
	case USB_SPEED_LOW:
		/* the hardware accepts SI in 125usec range */
		if (usb_endpoint_xfer_isoc(desc))
			si += 3;

		/*
		 * FS Int endpoints can have si of 1-255ms but the controller
		 * accepts 2^bInterval*125usec, so convert ms to nearest power
		 * of 2
		 */
		if (usb_endpoint_xfer_int(desc))
			si = fls(desc->bInterval * 8) - 1;

		param2 |= si;
		break;
	default:
		dev_err(bdc->dev, "UNKNOWN speed ERR\n");
		return -EINVAL;
	}

	cmd_sc |= BDC_CMD_EPC|BDC_CMD_EPN(ep->ep_num)|BDC_SUB_CMD_ADD_EP;

	dev_dbg(bdc->dev, "cmd_sc=%x param2=%08x\n", cmd_sc, param2);
	ret = bdc_submit_cmd(bdc, cmd_sc, param0, param1, param2);
	if (ret) {
		dev_err(bdc->dev, "command failed :%x\n", ret);
		return ret;
	}
	ep_bd_list_reinit(ep);

	return ret;
}

/*
 * Change the HW deq pointer, if this command is successful, HW will start
 * fetching the next bd from address dma_addr.
 */
int bdc_ep_bla(struct bdc *bdc, struct bdc_ep *ep, dma_addr_t dma_addr)
{
	u32 param0, param1;
	u32 cmd_sc = 0;

	dev_dbg(bdc->dev, "%s: add=%08llx\n", __func__,
				(unsigned long long)(dma_addr));
	param0 = lower_32_bits(dma_addr);
	param1 = upper_32_bits(dma_addr);
	cpu_to_le32s(&param0);
	cpu_to_le32s(&param1);

	cmd_sc |= BDC_CMD_EPN(ep->ep_num)|BDC_CMD_BLA;
	dev_dbg(bdc->dev, "cmd_sc=%x\n", cmd_sc);

	return bdc_submit_cmd(bdc, cmd_sc, param0, param1, 0);
}

/* Set the address sent bu Host in SET_ADD request */
int bdc_address_device(struct bdc *bdc, u32 add)
{
	u32 cmd_sc = 0;
	u32 param2;

	dev_dbg(bdc->dev, "%s: add=%d\n", __func__, add);
	cmd_sc |=  BDC_SUB_CMD_ADD|BDC_CMD_DVC;
	param2 = add & 0x7f;

	return bdc_submit_cmd(bdc, cmd_sc, 0, 0, param2);
}

/* Send a Function Wake notification packet using FH command */
int bdc_function_wake_fh(struct bdc *bdc, u8 intf)
{
	u32 param0, param1;
	u32 cmd_sc = 0;

	param0 = param1 = 0;
	dev_dbg(bdc->dev, "%s intf=%d\n", __func__, intf);
	cmd_sc  |=  BDC_CMD_FH;
	param0 |= TRA_PACKET;
	param0 |= (bdc->dev_addr << 25);
	param1 |= DEV_NOTF_TYPE;
	param1 |= (FWK_SUBTYPE<<4);
	dev_dbg(bdc->dev, "param0=%08x param1=%08x\n", param0, param1);

	return bdc_submit_cmd(bdc, cmd_sc, param0, param1, 0);
}

/* Send a Function Wake notification packet using DNC command */
int bdc_function_wake(struct bdc *bdc, u8 intf)
{
	u32 cmd_sc = 0;
	u32 param2 = 0;

	dev_dbg(bdc->dev, "%s intf=%d", __func__, intf);
	param2 |= intf;
	cmd_sc |= BDC_SUB_CMD_FWK|BDC_CMD_DNC;

	return bdc_submit_cmd(bdc, cmd_sc, 0, 0, param2);
}

/* Stall the endpoint */
int bdc_ep_set_stall(struct bdc *bdc, int epnum)
{
	u32 cmd_sc = 0;

	dev_dbg(bdc->dev, "%s epnum=%d\n", __func__, epnum);
	/* issue a stall endpoint command */
	cmd_sc |=  BDC_SUB_CMD_EP_STL | BDC_CMD_EPN(epnum) | BDC_CMD_EPO;

	return bdc_submit_cmd(bdc, cmd_sc, 0, 0, 0);
}

/* resets the endpoint, called when host sends CLEAR_FEATURE(HALT) */
int bdc_ep_clear_stall(struct bdc *bdc, int epnum)
{
	struct bdc_ep *ep;
	u32 cmd_sc = 0;
	int ret;

	dev_dbg(bdc->dev, "%s: epnum=%d\n", __func__, epnum);
	ep = bdc->bdc_ep_array[epnum];
	/*
	 * If we are not in stalled then stall Endpoint and issue clear stall,
	 * his will reset the seq number for non EP0.
	 */
	if (epnum != 1) {
		/* if the endpoint it not stallled */
		if (!(ep->flags & BDC_EP_STALL)) {
			ret = bdc_ep_set_stall(bdc, epnum);
				if (ret)
					return ret;
		}
	}
	/* Preserve the seq number for ep0 only */
	if (epnum != 1)
		cmd_sc |= BDC_CMD_EPO_RST_SN;

	/* issue a reset endpoint command */
	cmd_sc |=  BDC_SUB_CMD_EP_RST | BDC_CMD_EPN(epnum) | BDC_CMD_EPO;

	ret = bdc_submit_cmd(bdc, cmd_sc, 0, 0, 0);
	if (ret) {
		dev_err(bdc->dev, "command failed:%x\n", ret);
		return ret;
	}
	bdc_notify_xfr(bdc, epnum);

	return ret;
}

/* Stop the endpoint, called when software wants to dequeue some request */
int bdc_stop_ep(struct bdc *bdc, int epnum)
{
	struct bdc_ep *ep;
	u32 cmd_sc = 0;
	int ret;

	ep = bdc->bdc_ep_array[epnum];
	dev_dbg(bdc->dev, "%s: ep:%s ep->flags:%08x\n", __func__,
						ep->name, ep->flags);
	/* Endpoint has to be in running state to execute stop ep command */
	if (!(ep->flags & BDC_EP_ENABLED)) {
		dev_err(bdc->dev, "stop endpoint called for disabled ep\n");
		return   -EINVAL;
	}
	if ((ep->flags & BDC_EP_STALL) || (ep->flags & BDC_EP_STOP))
		return 0;

	/* issue a stop endpoint command */
	cmd_sc |= BDC_CMD_EP0_XSD | BDC_SUB_CMD_EP_STP
				| BDC_CMD_EPN(epnum) | BDC_CMD_EPO;

	ret = bdc_submit_cmd(bdc, cmd_sc, 0, 0, 0);
	if (ret) {
		dev_err(bdc->dev,
			"stop endpoint command didn't complete:%d ep:%s\n",
			ret, ep->name);
		return ret;
	}
	ep->flags |= BDC_EP_STOP;
	bdc_dump_epsts(bdc);

	return ret;
}
