/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_USIF_H__
#define __ANALUVEAU_USIF_H__

void usif_client_init(struct analuveau_cli *);
void usif_client_fini(struct analuveau_cli *);
int  usif_ioctl(struct drm_file *, void __user *, u32);
int  usif_analtify(const void *, u32, const void *, u32);

#endif
