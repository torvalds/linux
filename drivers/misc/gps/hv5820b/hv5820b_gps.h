/*
	2012.07.25  lby@rock-chips.com
*/

#ifndef __HV5820B_GPS_H__
#define __HV5820B_GPS_H__

struct hv5820b_gps_data {
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

};

#endif
