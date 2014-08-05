/*
 * USB device controllers have lots of quirks.  Use these macros in
 * gadget drivers or other code that needs to deal with them, and which
 * autoconfigures instead of using early binding to the hardware.
 *
 * This SHOULD eventually work like the ARM mach_is_*() stuff, driven by
 * some config file that gets updated as new hardware is supported.
 * (And avoiding all runtime comparisons in typical one-choice configs!)
 *
 * NOTE:  some of these controller drivers may not be available yet.
 * Some are available on 2.4 kernels; several are available, but not
 * yet pushed in the 2.6 mainline tree.
 */

#ifndef __GADGET_CHIPS_H
#define __GADGET_CHIPS_H

#include <linux/usb/gadget.h>

/*
 * NOTICE: the entries below are alphabetical and should be kept
 * that way.
 *
 * Always be sure to add new entries to the correct position or
 * accept the bashing later.
 *
 * If you have forgotten the alphabetical order let VIM/EMACS
 * do that for you.
 */
#define gadget_is_at91(g)		(!strcmp("at91_udc", (g)->name))
#define gadget_is_goku(g)		(!strcmp("goku_udc", (g)->name))
#define gadget_is_musbhdrc(g)		(!strcmp("musb-hdrc", (g)->name))
#define gadget_is_net2280(g)		(!strcmp("net2280", (g)->name))
#define gadget_is_pxa(g)		(!strcmp("pxa25x_udc", (g)->name))
#define gadget_is_pxa27x(g)		(!strcmp("pxa27x_udc", (g)->name))

/**
 * gadget_supports_altsettings - return true if altsettings work
 * @gadget: the gadget in question
 */
static inline bool gadget_supports_altsettings(struct usb_gadget *gadget)
{
	/* PXA 21x/25x/26x has no altsettings at all */
	if (gadget_is_pxa(gadget))
		return false;

	/* PXA 27x and 3xx have *broken* altsetting support */
	if (gadget_is_pxa27x(gadget))
		return false;

	/* Everything else is *presumably* fine ... */
	return true;
}

#endif /* __GADGET_CHIPS_H */
