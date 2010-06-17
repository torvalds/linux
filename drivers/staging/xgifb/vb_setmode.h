#ifndef  _VBSETMODE_
#define  _VBSETMODE_

extern   void     InitTo330Pointer(unsigned char, PVB_DEVICE_INFO);
extern   void     XGI_UnLockCRT2(struct xgi_hw_device_info *HwDeviceExtension, PVB_DEVICE_INFO);
extern   void     XGI_LockCRT2(struct xgi_hw_device_info *HwDeviceExtension, PVB_DEVICE_INFO);
extern   void     XGI_LongWait( PVB_DEVICE_INFO );
extern   void     XGI_SetCRT2ModeRegs(unsigned short ModeNo,
				      struct xgi_hw_device_info *,
				      PVB_DEVICE_INFO);
extern   void     XGI_DisableBridge(struct xgi_hw_device_info *HwDeviceExtension, PVB_DEVICE_INFO);
extern   void     XGI_EnableBridge(struct xgi_hw_device_info *HwDeviceExtension, PVB_DEVICE_INFO);
extern   void     XGI_DisplayOff(struct xgi_hw_device_info *, PVB_DEVICE_INFO);
extern   void     XGI_DisplayOn(struct xgi_hw_device_info *, PVB_DEVICE_INFO);
extern   void     XGI_GetVBType(PVB_DEVICE_INFO);
extern   void     XGI_SenseCRT1(PVB_DEVICE_INFO );
extern   void     XGI_GetVGAType(struct xgi_hw_device_info *HwDeviceExtension, PVB_DEVICE_INFO);
extern   void     XGI_GetVBInfo(unsigned short ModeNo, unsigned short ModeIdIndex, struct xgi_hw_device_info *HwDeviceExtension, PVB_DEVICE_INFO);
extern   void     XGI_GetTVInfo(unsigned short ModeNo,unsigned short ModeIdIndex, PVB_DEVICE_INFO );
extern   void     XGI_SetCRT1Offset(unsigned short ModeNo, unsigned short ModeIdIndex, unsigned short RefreshRateTableIndex, struct xgi_hw_device_info *HwDeviceExtension, PVB_DEVICE_INFO);
extern   void     XGI_SetLCDAGroup(unsigned short ModeNo, unsigned short ModeIdIndex, struct xgi_hw_device_info *HwDeviceExtension, PVB_DEVICE_INFO);
extern   void     XGI_WaitDisply( PVB_DEVICE_INFO );
extern   unsigned short   XGI_GetResInfo(unsigned short ModeNo,unsigned short ModeIdIndex, PVB_DEVICE_INFO pVBInfo);

extern   unsigned char  XGISetModeNew(struct xgi_hw_device_info *HwDeviceExtension, unsigned short ModeNo) ;

extern   unsigned char  XGI_SearchModeID( unsigned short ModeNo,unsigned short  *ModeIdIndex, PVB_DEVICE_INFO );
extern   unsigned char  XGI_GetLCDInfo(unsigned short ModeNo,unsigned short ModeIdIndex,PVB_DEVICE_INFO );
extern   unsigned char  XGI_BridgeIsOn( PVB_DEVICE_INFO );
extern   unsigned char  XGI_SetCRT2Group301(unsigned short ModeNo, struct xgi_hw_device_info *HwDeviceExtension, PVB_DEVICE_INFO);
extern   unsigned short   XGI_GetRatePtrCRT2(struct xgi_hw_device_info *pXGIHWDE, unsigned short ModeNo, unsigned short ModeIdIndex, PVB_DEVICE_INFO);

extern   void     XGI_SetXG21FPBits(PVB_DEVICE_INFO pVBInfo);
extern   void     XGI_SetXG27FPBits(PVB_DEVICE_INFO pVBInfo);
extern   void     XGI_XG21BLSignalVDD(unsigned short tempbh,unsigned short tempbl, PVB_DEVICE_INFO pVBInfo);
extern   void     XGI_XG27BLSignalVDD(unsigned short tempbh,unsigned short tempbl, PVB_DEVICE_INFO pVBInfo);
extern   void     XGI_XG21SetPanelDelay(unsigned short tempbl, PVB_DEVICE_INFO pVBInfo);
extern   unsigned char  XGI_XG21CheckLVDSMode(unsigned short ModeNo,unsigned short ModeIdIndex, PVB_DEVICE_INFO pVBInfo );
extern   void     XGI_SetXG21LVDSPara(unsigned short ModeNo,unsigned short ModeIdIndex, PVB_DEVICE_INFO pVBInfo );
extern   unsigned short XGI_GetLVDSOEMTableIndex(PVB_DEVICE_INFO pVBInfo);

#endif
