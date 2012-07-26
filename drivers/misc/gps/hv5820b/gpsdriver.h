///////////////////////////////////////////////////////////////////////////////////
//
// Filename: gpsdriver.h
// Author:	sjchen
// Copyright: 
// Date: 2012/07/09
// Description:
//			gps driver function 
//
// Revision:
//		0.0.1
//
///////////////////////////////////////////////////////////////////////////////////

#ifndef __GPSDRIVER_H___
#define __GPSDRIVER_H___

#define  GPS_BB_INT_MASK            57  


typedef struct __tag_GPS_DRV_INIT
{
	unsigned long   u32MemoryPhyAddr; //must reserved 8MB memory for GPS
	unsigned long   u32MemoryVirAddr;
	unsigned long   u32GpsRegBase;    //GPS register base virtual address
	unsigned int    u32GpsSign;       //GPIO index
	unsigned int    u32GpsMag;        //GPIO index
	unsigned int    u32GpsClk;        //GPIO index
	unsigned int    u32GpsVCCEn;      //GPIO index
	unsigned int    u32GpsSpi_CSO;    //GPIO index
	unsigned int    u32GpsSpiClk;     //GPIO index
	unsigned int    u32GpsSpiMOSI;	  //GPIO index
}GPS_DRV_INIT,*PGPS_DRV_INIT;

extern void WriteGpsRegisterUlong ( int reg_offset, int value );

extern int  ReadGpsRegisterUlong ( int reg_offset );

extern irqreturn_t gps_int_handler ( int irq, void * dev_id );

extern  void Gps_Init(unsigned long arg,PGPS_DRV_INIT pGpsDrvInit);

extern  void Gps_UpdateData(unsigned long arg);

extern  void Gps_Stop(void);

extern  int GpsDrv_Read(char *buf ,int nSize);

#endif //__GPSDRIVER_H___

