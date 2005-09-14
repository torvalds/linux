/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_proto.h,v 1.10 2003/07/31 20:24:29 mvojkovi Exp $ */

#ifndef __NV_PROTO_H__
#define __NV_PROTO_H__

/* in nv_setup.c */
void NVCommonSetup(struct fb_info *info);
void NVWriteCrtc(struct nvidia_par *par, u8 index, u8 value);
u8 NVReadCrtc(struct nvidia_par *par, u8 index);
void NVWriteGr(struct nvidia_par *par, u8 index, u8 value);
u8 NVReadGr(struct nvidia_par *par, u8 index);
void NVWriteSeq(struct nvidia_par *par, u8 index, u8 value);
u8 NVReadSeq(struct nvidia_par *par, u8 index);
void NVWriteAttr(struct nvidia_par *par, u8 index, u8 value);
u8 NVReadAttr(struct nvidia_par *par, u8 index);
void NVWriteMiscOut(struct nvidia_par *par, u8 value);
u8 NVReadMiscOut(struct nvidia_par *par);
void NVWriteDacMask(struct nvidia_par *par, u8 value);
void NVWriteDacReadAddr(struct nvidia_par *par, u8 value);
void NVWriteDacWriteAddr(struct nvidia_par *par, u8 value);
void NVWriteDacData(struct nvidia_par *par, u8 value);
u8 NVReadDacData(struct nvidia_par *par);

/* in nv_hw.c */
void NVCalcStateExt(struct nvidia_par *par, struct _riva_hw_state *,
		    int, int, int, int, int, int);
void NVLoadStateExt(struct nvidia_par *par, struct _riva_hw_state *);
void NVUnloadStateExt(struct nvidia_par *par, struct _riva_hw_state *);
void NVSetStartAddress(struct nvidia_par *par, u32);
int NVShowHideCursor(struct nvidia_par *par, int);
void NVLockUnlock(struct nvidia_par *par, int);

/* in nvidia-i2c.c */
#if defined(CONFIG_FB_NVIDIA_I2C) || defined (CONFIG_PPC_OF)
void nvidia_create_i2c_busses(struct nvidia_par *par);
void nvidia_delete_i2c_busses(struct nvidia_par *par);
int nvidia_probe_i2c_connector(struct fb_info *info, int conn,
			       u8 ** out_edid);
#else
#define nvidia_create_i2c_busses(...)
#define nvidia_delete_i2c_busses(...)
#define nvidia_probe_i2c_connector(p, c, edid) \
do {                                           \
	*(edid) = NULL;                        \
} while(0)
#endif

/* in nv_accel.c */
extern void NVResetGraphics(struct fb_info *info);
extern void nvidiafb_copyarea(struct fb_info *info,
			      const struct fb_copyarea *region);
extern void nvidiafb_fillrect(struct fb_info *info,
			      const struct fb_fillrect *rect);
extern void nvidiafb_imageblit(struct fb_info *info,
			       const struct fb_image *image);
extern int nvidiafb_sync(struct fb_info *info);
extern u8 byte_rev[256];
#endif				/* __NV_PROTO_H__ */
