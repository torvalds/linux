/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DIAG_IOCTL_H
#define _DIAG_IOCTL_H

#include <linux/types.h>

long diag324_pibbuf(unsigned long arg);
long diag324_piblen(unsigned long arg);

#endif /* _DIAG_IOCTL_H */
