/*
	2012.07.25  lby@rock-chips.com
*/

#ifndef __RK_GPS_H__
#define __RK_GPS_H__

struct rk_gps_data {
	int (*io_init)(void);
	int (*power_up)(void);
	int (*power_down)(void);
	int (*reset)(int);
	int (*enable_hclk_gps)(void);
	int (*disable_hclk_gps)(void);
	int   GpsSign;
	int    GpsMag;        //GPIO index
	int    GpsClk;        //GPIO index
	int    GpsVCCEn;      //GPIO index
	int    GpsSpi_CSO;    //GPIO index
	int    GpsSpiClk;     //GPIO index
	int    GpsSpiMOSI;	  //GPIO index
	int    GpsSpiEn;	//USE SPI
	int    GpsAdcCh;	//ADC CHANNEL
	int    GpsIrq;
	
	unsigned long   u32GpsPhyAddr;
	int		u32GpsPhySize;
	unsigned long   u32MemoryPhyAddr; //must reserved 8MB memory for GPS
	unsigned long   u32MemoryVirAddr;
	unsigned long   u32GpsRegBase;    //GPS register base virtual address

};

#endif
