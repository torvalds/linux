#ifndef __NOUVEAU_VGA_H__
#define __NOUVEAU_VGA_H__

#include <core/os.h>

/* access to various legacy io ports */
u8   nv_rdport(void *obj, int head, u16 port);
void nv_wrport(void *obj, int head, u16 port, u8 value);

/* VGA Sequencer */
u8   nv_rdvgas(void *obj, int head, u8 index);
void nv_wrvgas(void *obj, int head, u8 index, u8 value);

/* VGA Graphics */
u8   nv_rdvgag(void *obj, int head, u8 index);
void nv_wrvgag(void *obj, int head, u8 index, u8 value);

/* VGA CRTC */
u8   nv_rdvgac(void *obj, int head, u8 index);
void nv_wrvgac(void *obj, int head, u8 index, u8 value);

/* VGA indexed port access dispatcher */
u8   nv_rdvgai(void *obj, int head, u16 port, u8 index);
void nv_wrvgai(void *obj, int head, u16 port, u8 index, u8 value);

bool nv_lockvgac(void *obj, bool lock);
u8   nv_rdvgaowner(void *obj);
void nv_wrvgaowner(void *obj, u8);

#endif
