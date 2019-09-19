// SPDX-License-Identifier: GPL-2.0
/*
 * mtu3_dr.h - dual role switch and host glue layer header
 *
 * Copyright (C) 2016 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 */

#ifndef _MTU3_DR_H_
#define _MTU3_DR_H_

#if IS_ENABLED(CONFIG_USB_MTU3_HOST) || IS_ENABLED(CONFIG_USB_MTU3_DUAL_ROLE)

int ssusb_host_init(struct ssusb_mtk *ssusb, struct device_node *parent_dn);
void ssusb_host_exit(struct ssusb_mtk *ssusb);
int ssusb_wakeup_of_property_parse(struct ssusb_mtk *ssusb,
				struct device_node *dn);
int ssusb_host_enable(struct ssusb_mtk *ssusb);
int ssusb_host_disable(struct ssusb_mtk *ssusb, bool suspend);
void ssusb_wakeup_set(struct ssusb_mtk *ssusb, bool enable);

#else

static inline int ssusb_host_init(struct ssusb_mtk *ssusb,

	struct device_node *parent_dn)
{
	return 0;
}

static inline void ssusb_host_exit(struct ssusb_mtk *ssusb)
{}

static inline int ssusb_wakeup_of_property_parse(
	struct ssusb_mtk *ssusb, struct device_node *dn)
{
	return 0;
}

static inline int ssusb_host_enable(struct ssusb_mtk *ssusb)
{
	return 0;
}

static inline int ssusb_host_disable(struct ssusb_mtk *ssusb, bool suspend)
{
	return 0;
}

static inline void ssusb_wakeup_set(struct ssusb_mtk *ssusb, bool enable)
{}

#endif


#if IS_ENABLED(CONFIG_USB_MTU3_GADGET) || IS_ENABLED(CONFIG_USB_MTU3_DUAL_ROLE)
int ssusb_gadget_init(struct ssusb_mtk *ssusb);
void ssusb_gadget_exit(struct ssusb_mtk *ssusb);
#else
static inline int ssusb_gadget_init(struct ssusb_mtk *ssusb)
{
	return 0;
}

static inline void ssusb_gadget_exit(struct ssusb_mtk *ssusb)
{}
#endif


#if IS_ENABLED(CONFIG_USB_MTU3_DUAL_ROLE)
int ssusb_otg_switch_init(struct ssusb_mtk *ssusb);
void ssusb_otg_switch_exit(struct ssusb_mtk *ssusb);
void ssusb_mode_switch(struct ssusb_mtk *ssusb, int to_host);
int ssusb_set_vbus(struct otg_switch_mtk *otg_sx, int is_on);
void ssusb_set_force_mode(struct ssusb_mtk *ssusb,
			  enum mtu3_dr_force_mode mode);

#else

static inline int ssusb_otg_switch_init(struct ssusb_mtk *ssusb)
{
	return 0;
}

static inline void ssusb_otg_switch_exit(struct ssusb_mtk *ssusb)
{}

static inline void ssusb_mode_switch(struct ssusb_mtk *ssusb, int to_host)
{}

static inline int ssusb_set_vbus(struct otg_switch_mtk *otg_sx, int is_on)
{
	return 0;
}

static inline void
ssusb_set_force_mode(struct ssusb_mtk *ssusb, enum mtu3_dr_force_mode mode)
{}

#endif

#endif		/* _MTU3_DR_H_ */
