/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FOTG210_H
#define __FOTG210_H

#ifdef CONFIG_USB_FOTG210_HCD
int fotg210_hcd_probe(struct platform_device *pdev);
int fotg210_hcd_remove(struct platform_device *pdev);
int fotg210_hcd_init(void);
void fotg210_hcd_cleanup(void);
#else
static inline int fotg210_hcd_probe(struct platform_device *pdev)
{
	return 0;
}
static inline int fotg210_hcd_remove(struct platform_device *pdev)
{
	return 0;
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
int fotg210_udc_probe(struct platform_device *pdev);
int fotg210_udc_remove(struct platform_device *pdev);
#else
static inline int fotg210_udc_probe(struct platform_device *pdev)
{
	return 0;
}
static inline int fotg210_udc_remove(struct platform_device *pdev)
{
	return 0;
}
#endif

#endif /* __FOTG210_H */
