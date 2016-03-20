#ifndef _VBSETMODE_
#define _VBSETMODE_

void InitTo330Pointer(unsigned char, struct vb_device_info *);
void XGI_UnLockCRT2(struct vb_device_info *);
void XGI_LockCRT2(struct vb_device_info *);
void XGI_DisplayOff(struct xgifb_video_info *,
		    struct xgi_hw_device_info *,
		    struct vb_device_info *);
void XGI_GetVBType(struct vb_device_info *);
void XGI_SenseCRT1(struct vb_device_info *);
unsigned char XGISetModeNew(struct xgifb_video_info *xgifb_info,
			    struct xgi_hw_device_info *HwDeviceExtension,
			    unsigned short ModeNo);

unsigned char XGI_SearchModeID(unsigned short ModeNo,
			       unsigned short *ModeIdIndex);
unsigned short XGI_GetRatePtrCRT2(struct xgi_hw_device_info *pXGIHWDE,
				  unsigned short ModeNo,
				  unsigned short ModeIdIndex,
				  struct vb_device_info *);

#endif
