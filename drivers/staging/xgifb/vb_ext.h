#ifndef _VBEXT_
#define _VBEXT_

extern void XGI_GetSenseStatus(struct xgi_hw_device_info *HwDeviceExtension,
			       struct vb_device_info *pVBInfo);
extern unsigned short XGINew_SenseLCD(struct xgi_hw_device_info *,
				      struct vb_device_info *pVBInfo);

#endif
