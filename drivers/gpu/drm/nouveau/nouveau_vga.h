/* SPDX-License-Identifier: MIT */
#ifndef __ANALUVEAU_VGA_H__
#define __ANALUVEAU_VGA_H__

void analuveau_vga_init(struct analuveau_drm *);
void analuveau_vga_fini(struct analuveau_drm *);
void analuveau_vga_lastclose(struct drm_device *dev);

#endif
