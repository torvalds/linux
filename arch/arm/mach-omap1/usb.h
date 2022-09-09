/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fixme correct answer depends on hmc_mode,
 * as does (on omap1) any nonzero value for config->otg port number
 */
#include <linux/platform_data/usb-omap1.h>
#include <linux/soc/ti/omap1-usb.h>

#if IS_ENABLED(CONFIG_USB_OMAP)
#define	is_usb0_device(config)	1
#else
#define	is_usb0_device(config)	0
#endif

#if IS_ENABLED(CONFIG_USB_SUPPORT)
void omap1_usb_init(struct omap_usb_config *pdata);
#else
static inline void omap1_usb_init(struct omap_usb_config *pdata)
{
}
#endif

#define OMAP1_OHCI_BASE			0xfffba000
#define OMAP2_OHCI_BASE			0x4805e000
#define OMAP_OHCI_BASE			OMAP1_OHCI_BASE
