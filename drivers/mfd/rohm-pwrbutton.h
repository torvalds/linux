/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __LINUX_MFD_ROHM_PWRBUTTON_H__
#define __LINUX_MFD_ROHM_PWRBUTTON_H__

struct device;
struct irq_domain;

int rohm_register_pwrbutton(struct device *dev, int irq, const char *name,
			    bool wakeup, struct irq_domain *irq_domain);

#endif /* __LINUX_MFD_ROHM_PWRBUTTON_H__ */
