/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * HDMI header definition for OMAP4 HDMI CEC IP
 *
 * Copyright 2016-2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#ifndef _HDMI4_CEC_H_
#define _HDMI4_CEC_H_

struct hdmi_core_data;
struct hdmi_wp_data;
struct platform_device;

/* HDMI CEC funcs */
#ifdef CONFIG_OMAP4_DSS_HDMI_CEC
void hdmi4_cec_set_phys_addr(struct hdmi_core_data *core, u16 pa);
void hdmi4_cec_irq(struct hdmi_core_data *core);
int hdmi4_cec_init(struct platform_device *pdev, struct hdmi_core_data *core,
		  struct hdmi_wp_data *wp);
void hdmi4_cec_uninit(struct hdmi_core_data *core);
#else
static inline void hdmi4_cec_set_phys_addr(struct hdmi_core_data *core, u16 pa)
{
}

static inline void hdmi4_cec_irq(struct hdmi_core_data *core)
{
}

static inline int hdmi4_cec_init(struct platform_device *pdev,
				struct hdmi_core_data *core,
				struct hdmi_wp_data *wp)
{
	return 0;
}

static inline void hdmi4_cec_uninit(struct hdmi_core_data *core)
{
}
#endif

#endif
