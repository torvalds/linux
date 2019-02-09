/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VBSETMODE_
#define _VBSETMODE_

void InitTo330Pointer(unsigned char ChipType, struct vb_device_info *pVBInfo);
void XGI_UnLockCRT2(struct vb_device_info *pVBInfo);
void XGI_LockCRT2(struct vb_device_info *pVBInfo);
void XGI_DisplayOff(struct xgifb_video_info *xgifb_info,
		    struct xgi_hw_device_info *pXGIHWDE,
		    struct vb_device_info *pVBInfo);
void XGI_GetVBType(struct vb_device_info *pVBInfo);
void XGI_SenseCRT1(struct vb_device_info *pVBInfo);
unsigned char XGISetModeNew(struct xgifb_video_info *xgifb_info,
			    struct xgi_hw_device_info *HwDeviceExtension,
			    unsigned short ModeNo);

unsigned char XGI_SearchModeID(unsigned short ModeNo,
			       unsigned short *ModeIdIndex);
unsigned short XGI_GetRatePtrCRT2(struct xgi_hw_device_info *pXGIHWDE,
				  unsigned short ModeNo,
				  unsigned short ModeIdIndex,
				  struct vb_device_info *pVBInfo);

#endif
