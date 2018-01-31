/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __REBOOT_MODE_H__
#define __REBOOT_MODE_H__

int reboot_mode_register(struct device *dev, int (*write)(int));

#endif
