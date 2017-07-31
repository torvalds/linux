/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author:
 *  Zhigang.Wei <zhigang.wei@mediatek.com>
 *  Chunfeng.Yun <chunfeng.yun@mediatek.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _XHCI_MTK_H_
#define _XHCI_MTK_H_

#include "xhci.h"

/**
 * To simplify scheduler algorithm, set a upper limit for ESIT,
 * if a synchromous ep's ESIT is larger than @XHCI_MTK_MAX_ESIT,
 * round down to the limit value, that means allocating more
 * bandwidth to it.
 */
#define XHCI_MTK_MAX_ESIT	64

/**
 * struct mu3h_sch_bw_info: schedule information for bandwidth domain
 *
 * @bus_bw: array to keep track of bandwidth already used at each uframes
 * @bw_ep_list: eps in the bandwidth domain
 *
 * treat a HS root port as a bandwidth domain, but treat a SS root port as
 * two bandwidth domains, one for IN eps and another for OUT eps.
 */
struct mu3h_sch_bw_info {
	u32 bus_bw[XHCI_MTK_MAX_ESIT];
	struct list_head bw_ep_list;
};

/**
 * struct mu3h_sch_ep_info: schedule information for endpoint
 *
 * @esit: unit is 125us, equal to 2 << Interval field in ep-context
 * @num_budget_microframes: number of continuous uframes
 *		(@repeat==1) scheduled within the interval
 * @bw_cost_per_microframe: bandwidth cost per microframe
 * @endpoint: linked into bandwidth domain which it belongs to
 * @ep: address of usb_host_endpoint struct
 * @offset: which uframe of the interval that transfer should be
 *		scheduled first time within the interval
 * @repeat: the time gap between two uframes that transfers are
 *		scheduled within a interval. in the simple algorithm, only
 *		assign 0 or 1 to it; 0 means using only one uframe in a
 *		interval, and 1 means using @num_budget_microframes
 *		continuous uframes
 * @pkts: number of packets to be transferred in the scheduled uframes
 * @cs_count: number of CS that host will trigger
 * @burst_mode: burst mode for scheduling. 0: normal burst mode,
 *		distribute the bMaxBurst+1 packets for a single burst
 *		according to @pkts and @repeat, repeate the burst multiple
 *		times; 1: distribute the (bMaxBurst+1)*(Mult+1) packets
 *		according to @pkts and @repeat. normal mode is used by
 *		default
 */
struct mu3h_sch_ep_info {
	u32 esit;
	u32 num_budget_microframes;
	u32 bw_cost_per_microframe;
	struct list_head endpoint;
	void *ep;
	/*
	 * mtk xHCI scheduling information put into reserved DWs
	 * in ep context
	 */
	u32 offset;
	u32 repeat;
	u32 pkts;
	u32 cs_count;
	u32 burst_mode;
};

#define MU3C_U3_PORT_MAX 4
#define MU3C_U2_PORT_MAX 5

/**
 * struct mu3c_ippc_regs: MTK ssusb ip port control registers
 * @ip_pw_ctr0~3: ip power and clock control registers
 * @ip_pw_sts1~2: ip power and clock status registers
 * @ip_xhci_cap: ip xHCI capability register
 * @u3_ctrl_p[x]: ip usb3 port x control register, only low 4bytes are used
 * @u2_ctrl_p[x]: ip usb2 port x control register, only low 4bytes are used
 * @u2_phy_pll: usb2 phy pll control register
 */
struct mu3c_ippc_regs {
	__le32 ip_pw_ctr0;
	__le32 ip_pw_ctr1;
	__le32 ip_pw_ctr2;
	__le32 ip_pw_ctr3;
	__le32 ip_pw_sts1;
	__le32 ip_pw_sts2;
	__le32 reserved0[3];
	__le32 ip_xhci_cap;
	__le32 reserved1[2];
	__le64 u3_ctrl_p[MU3C_U3_PORT_MAX];
	__le64 u2_ctrl_p[MU3C_U2_PORT_MAX];
	__le32 reserved2;
	__le32 u2_phy_pll;
	__le32 reserved3[33]; /* 0x80 ~ 0xff */
};

struct xhci_hcd_mtk {
	struct device *dev;
	struct usb_hcd *hcd;
	struct mu3h_sch_bw_info *sch_array;
	struct mu3c_ippc_regs __iomem *ippc_regs;
	bool has_ippc;
	int num_u2_ports;
	int num_u3_ports;
	struct regulator *vusb33;
	struct regulator *vbus;
	struct clk *sys_clk;	/* sys and mac clock */
	struct clk *ref_clk;
	struct clk *wk_deb_p0;	/* port0's wakeup debounce clock */
	struct clk *wk_deb_p1;
	struct regmap *pericfg;
	struct phy **phys;
	int num_phys;
	int wakeup_src;
	bool lpm_support;
};

static inline struct xhci_hcd_mtk *hcd_to_mtk(struct usb_hcd *hcd)
{
	return dev_get_drvdata(hcd->self.controller);
}

#if IS_ENABLED(CONFIG_USB_XHCI_MTK)
int xhci_mtk_sch_init(struct xhci_hcd_mtk *mtk);
void xhci_mtk_sch_exit(struct xhci_hcd_mtk *mtk);
int xhci_mtk_add_ep_quirk(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint *ep);
void xhci_mtk_drop_ep_quirk(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint *ep);

#else
static inline int xhci_mtk_add_ep_quirk(struct usb_hcd *hcd,
	struct usb_device *udev, struct usb_host_endpoint *ep)
{
	return 0;
}

static inline void xhci_mtk_drop_ep_quirk(struct usb_hcd *hcd,
	struct usb_device *udev, struct usb_host_endpoint *ep)
{
}

#endif

#endif		/* _XHCI_MTK_H_ */
