/* SPDX-License-Identifier: MIT */
#ifndef __NOUVEAU_VGA_H__
#define __NOUVEAU_VGA_H__

void yesuveau_vga_init(struct yesuveau_drm *);
void yesuveau_vga_fini(struct yesuveau_drm *);
void yesuveau_vga_lastclose(struct drm_device *dev);

#endif
