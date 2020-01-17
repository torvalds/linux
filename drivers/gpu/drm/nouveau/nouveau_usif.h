/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_USIF_H__
#define __NOUVEAU_USIF_H__

void usif_client_init(struct yesuveau_cli *);
void usif_client_fini(struct yesuveau_cli *);
int  usif_ioctl(struct drm_file *, void __user *, u32);
int  usif_yestify(const void *, u32, const void *, u32);

#endif
