#ifndef _BACKPORT_LINUX_MISCDEVICE_H
#define _BACKPORT_LINUX_MISCDEVICE_H
#include_next <linux/miscdevice.h>

#ifndef VHCI_MINOR
#define VHCI_MINOR		137
#endif

#endif /* _BACKPORT_LINUX_MISCDEVICE_H */
