/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cadence USBSS and USBSSP DRD Header File.
 *
 * Copyright (C) 2017-2018 NXP
 * Copyright (C) 2018-2019 Cadence.
 *
 * Authors: Peter Chen <peter.chen@nxp.com>
 *          Pawel Laszczak <pawell@cadence.com>
 */
#ifndef __LINUX_CDNS3_CORE_H
#define __LINUX_CDNS3_CORE_H

#include <linux/usb/otg.h>
#include <linux/usb/role.h>

struct cdns;

/**
 * struct cdns_role_driver - host/gadget role driver
 * @start: start this role
 * @stop: stop this role
 * @suspend: suspend callback for this role
 * @resume: resume callback for this role
 * @irq: irq handler for this role
 * @name: role name string (host/gadget)
 * @state: current state
 */
struct cdns_role_driver {
	int (*start)(struct cdns *cdns);
	void (*stop)(struct cdns *cdns);
	int (*suspend)(struct cdns *cdns, bool do_wakeup);
	int (*resume)(struct cdns *cdns, bool lost_power);
	const char *name;
#define CDNS_ROLE_STATE_INACTIVE	0
#define CDNS_ROLE_STATE_ACTIVE		1
	int state;
};

#define CDNS_XHCI_RESOURCES_NUM	2

struct cdns3_platform_data {
	int (*platform_suspend)(struct device *dev,
			bool suspend, bool wakeup);
	unsigned long quirks;
#define CDNS3_DEFAULT_PM_RUNTIME_ALLOW	BIT(0)
#define CDNS3_DRD_SUSPEND_RESIDENCY_ENABLE	BIT(1)
};

/**
 * struct cdns - Representation of Cadence USB3 DRD controller.
 * @dev: pointer to Cadence device struct
 * @xhci_regs: pointer to base of xhci registers
 * @xhci_res: the resource for xhci
 * @dev_regs: pointer to base of dev registers
 * @otg_res: the resource for otg
 * @otg_v0_regs: pointer to base of v0 otg registers
 * @otg_v1_regs: pointer to base of v1 otg registers
 * @otg_cdnsp_regs: pointer to base of CDNSP otg registers
 * @otg_regs: pointer to base of otg registers
 * @otg_irq_regs: pointer to interrupt registers
 * @otg_irq: irq number for otg controller
 * @dev_irq: irq number for device controller
 * @wakeup_irq: irq number for wakeup event, it is optional
 * @roles: array of supported roles for this controller
 * @role: current role
 * @host_dev: the child host device pointer for cdns core
 * @gadget_dev: the child gadget device pointer
 * @usb2_phy: pointer to USB2 PHY
 * @usb3_phy: pointer to USB3 PHY
 * @mutex: the mutex for concurrent code at driver
 * @dr_mode: supported mode of operation it can be only Host, only Device
 *           or OTG mode that allow to switch between Device and Host mode.
 *           This field based on firmware setting, kernel configuration
 *           and hardware configuration.
 * @role_sw: pointer to role switch object.
 * @in_lpm: indicate the controller is in low power mode
 * @wakeup_pending: wakeup interrupt pending
 * @pdata: platform data from glue layer
 * @lock: spinlock structure
 * @xhci_plat_data: xhci private data structure pointer
 * @override_apb_timeout: hold value of APB timeout. For value 0 the default
 *                        value in CHICKEN_BITS_3 will be preserved.
 * @gadget_init: pointer to gadget initialization function
 */
struct cdns {
	struct device			*dev;
	void __iomem			*xhci_regs;
	struct resource			xhci_res[CDNS_XHCI_RESOURCES_NUM];
	struct cdns3_usb_regs __iomem	*dev_regs;

	struct resource				otg_res;
	struct cdns3_otg_legacy_regs __iomem	*otg_v0_regs;
	struct cdns3_otg_regs __iomem		*otg_v1_regs;
	struct cdnsp_otg_regs __iomem		*otg_cdnsp_regs;
	struct cdns_otg_common_regs __iomem	*otg_regs;
	struct cdns_otg_irq_regs __iomem	*otg_irq_regs;
#define CDNS3_CONTROLLER_V0	0
#define CDNS3_CONTROLLER_V1	1
#define CDNSP_CONTROLLER_V2	2
	u32				version;
	bool				phyrst_a_enable;

	int				otg_irq;
	int				dev_irq;
	int				wakeup_irq;
	struct cdns_role_driver	*roles[USB_ROLE_DEVICE + 1];
	enum usb_role			role;
	struct platform_device		*host_dev;
	void				*gadget_dev;
	struct phy			*usb2_phy;
	struct phy			*usb3_phy;
	/* mutext used in workqueue*/
	struct mutex			mutex;
	enum usb_dr_mode		dr_mode;
	struct usb_role_switch		*role_sw;
	bool				in_lpm;
	bool				wakeup_pending;
	struct cdns3_platform_data	*pdata;
	spinlock_t			lock;
	struct xhci_plat_priv		*xhci_plat_data;
	u32                             override_apb_timeout;

	int (*gadget_init)(struct cdns *cdns);
};

int cdns_hw_role_switch(struct cdns *cdns);
int cdns_init(struct cdns *cdns);
int cdns_remove(struct cdns *cdns);

#ifdef CONFIG_PM_SLEEP
int cdns_resume(struct cdns *cdns);
int cdns_suspend(struct cdns *cdns);
void cdns_set_active(struct cdns *cdns, u8 set_active);
#else /* CONFIG_PM_SLEEP */
static inline int cdns_resume(struct cdns *cdns)
{ return 0; }
static inline void cdns_set_active(struct cdns *cdns, u8 set_active) { }
static inline int cdns_suspend(struct cdns *cdns)
{ return 0; }
#endif /* CONFIG_PM_SLEEP */
#endif /* __LINUX_CDNS3_CORE_H */
