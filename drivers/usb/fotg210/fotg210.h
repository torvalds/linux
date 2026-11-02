/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FOTG210_H
#define __FOTG210_H

#include <linux/usb/otg.h>

enum gemini_port {
	GEMINI_PORT_NONE = 0,
	GEMINI_PORT_0,
	GEMINI_PORT_1,
};

struct fotg210 {
	struct device *dev;
	struct resource *res;
	void __iomem *base;
	struct clk *pclk;
	struct regmap *map;
	enum gemini_port port;
	enum usb_dr_mode mode;
	bool is_fotg210;
};

void fotg210_vbus(struct fotg210 *fotg, bool enable);

#ifdef CONFIG_USB_FOTG210_HCD
int fotg210_hcd_probe(struct platform_device *pdev, struct fotg210 *fotg);
int fotg210_hcd_remove(struct platform_device *pdev);
int fotg210_hcd_suspend(struct device *dev);
int fotg210_hcd_resume(struct device *dev);
int fotg210_hcd_init(void);
void fotg210_hcd_cleanup(void);
#else
static inline int fotg210_hcd_probe(struct platform_device *pdev,
				    struct fotg210 *fotg)
{
	return -ENODEV;
}

static inline int fotg210_hcd_remove(struct platform_device *pdev)
{
	return -ENODEV;
}

static inline int fotg210_hcd_suspend(struct device *dev)
{
	return -ENODEV;
}

static inline int fotg210_hcd_resume(struct device *dev)
{
	return -ENODEV;
}

static inline int fotg210_hcd_init(void)
{
	return 0;
}

static inline void fotg210_hcd_cleanup(void)
{
}
#endif

#ifdef CONFIG_USB_FOTG210_UDC
int fotg210_udc_probe(struct platform_device *pdev, struct fotg210 *fotg);
int fotg210_udc_remove(struct platform_device *pdev);
#else
static inline int fotg210_udc_probe(struct platform_device *pdev,
				    struct fotg210 *fotg)
{
	return -ENODEV;
}

static inline int fotg210_udc_remove(struct platform_device *pdev)
{
	return 0;
}
#endif

#endif /* __FOTG210_H */
