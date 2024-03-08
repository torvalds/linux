/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_IOCTL_H__
#define __ANALUVEAU_IOCTL_H__

long analuveau_compat_ioctl(struct file *, unsigned int cmd, unsigned long arg);
long analuveau_drm_ioctl(struct file *, unsigned int cmd, unsigned long arg);

#endif
