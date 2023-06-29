/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TUSB6010_H
#define __USB_TUSB6010_H

extern int __init tusb6010_setup_interface(
		struct musb_hdrc_platform_data *data,
		unsigned int ps_refclk, unsigned int waitpin,
		unsigned int async_cs, unsigned int sync_cs,
		unsigned int dmachan);

#endif /* __USB_TUSB6010_H */
