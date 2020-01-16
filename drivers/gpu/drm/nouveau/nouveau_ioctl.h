/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_IOCTL_H__
#define __NOUVEAU_IOCTL_H__

long yesuveau_compat_ioctl(struct file *, unsigned int cmd, unsigned long arg);
long yesuveau_drm_ioctl(struct file *, unsigned int cmd, unsigned long arg);

#endif
