#ifndef  _VBSETMODE_
#define  _VBSETMODE_

extern   void     InitTo330Pointer(UCHAR,PVB_DEVICE_INFO);
extern   void     XGI_UnLockCRT2(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO );
extern   void     XGI_LockCRT2(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO );
extern   void     XGI_LongWait( PVB_DEVICE_INFO );
extern   void     XGI_SetCRT2ModeRegs(USHORT ModeNo,PXGI_HW_DEVICE_INFO,  PVB_DEVICE_INFO  );
extern   void     XGI_DisableBridge(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO );
extern   void  	  XGI_EnableBridge(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO );
extern   void     XGI_DisplayOff( PXGI_HW_DEVICE_INFO, PVB_DEVICE_INFO );
extern   void     XGI_DisplayOn( PXGI_HW_DEVICE_INFO, PVB_DEVICE_INFO );
extern   void     XGI_GetVBType(PVB_DEVICE_INFO);
extern   void     XGI_SenseCRT1(PVB_DEVICE_INFO );
extern   void     XGI_GetVGAType(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO );
extern   void     XGI_GetVBInfo(USHORT ModeNo,USHORT ModeIdIndex,PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO );
extern   void     XGI_GetTVInfo(USHORT ModeNo,USHORT ModeIdIndex, PVB_DEVICE_INFO );
extern   void     XGI_SetCRT1Offset(USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO );
extern   void     XGI_SetLCDAGroup(USHORT ModeNo,USHORT ModeIdIndex,PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO );
extern   void     XGI_WaitDisply( PVB_DEVICE_INFO );
extern   USHORT   XGI_GetResInfo(USHORT ModeNo,USHORT ModeIdIndex, PVB_DEVICE_INFO pVBInfo);

extern   BOOLEAN  XGISetModeNew( PXGI_HW_DEVICE_INFO HwDeviceExtension , USHORT ModeNo ) ;

extern   BOOLEAN  XGI_SearchModeID( USHORT ModeNo,USHORT  *ModeIdIndex, PVB_DEVICE_INFO );
extern   BOOLEAN  XGI_GetLCDInfo(USHORT ModeNo,USHORT ModeIdIndex,PVB_DEVICE_INFO );
extern   BOOLEAN  XGI_BridgeIsOn( PVB_DEVICE_INFO );
extern   BOOLEAN  XGI_SetCRT2Group301(USHORT ModeNo, PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO);
extern   USHORT   XGI_GetRatePtrCRT2( PXGI_HW_DEVICE_INFO pXGIHWDE, USHORT ModeNo,USHORT ModeIdIndex, PVB_DEVICE_INFO );

extern   void     XGI_SetXG21FPBits(PVB_DEVICE_INFO pVBInfo);
extern   void     XGI_SetXG27FPBits(PVB_DEVICE_INFO pVBInfo);
extern   void     XGI_XG21BLSignalVDD(USHORT tempbh,USHORT tempbl, PVB_DEVICE_INFO pVBInfo);
extern   void     XGI_XG27BLSignalVDD(USHORT tempbh,USHORT tempbl, PVB_DEVICE_INFO pVBInfo);
extern   void     XGI_XG21SetPanelDelay(USHORT tempbl, PVB_DEVICE_INFO pVBInfo);
extern   BOOLEAN  XGI_XG21CheckLVDSMode(USHORT ModeNo,USHORT ModeIdIndex, PVB_DEVICE_INFO pVBInfo );
extern   void     XGI_SetXG21LVDSPara(USHORT ModeNo,USHORT ModeIdIndex, PVB_DEVICE_INFO pVBInfo );
extern   USHORT XGI_GetLVDSOEMTableIndex(PVB_DEVICE_INFO pVBInfo);

#endif
