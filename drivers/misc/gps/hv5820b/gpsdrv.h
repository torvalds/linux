///////////////////////////////////////////////////////////////////////////////////
//
// Filename: gpsdrv.h
// Author:	sjchen
// Copyright: 
// Date: 2012/07/09
// Description:
//			the struct driver to app
//
// Revision:
//		0.0.1
//
///////////////////////////////////////////////////////////////////////////////////
#ifndef __GPSDRV_H__
#define __GPSDRV_H__


#define DRV_MAJOR_VERSION		     1		
#define DRV_MINOR_VERSION		     9

typedef struct __tag_BB_COMMAND_BUFFER {
	int	n32BufferA;
	int	n32BufferB;
	int n32NavBuf;
} BB_COMMAND_BUFFER, *PBB_COMMAND_BUFFER;


typedef struct __tag_DRV_VERSION {
	unsigned int    u32Major;
	unsigned int    u32Minor;
	char            strCompileTime[32];

}BB_DRV_VERSION,*PBB_DRV_VERSION;

// IOCTL code
enum
{
  // GPS Start
  IOCTL_BB_GPS_START = 128,
  IOCTL_BB_GPS_STOP,

  //Update gps data
  IOCTL_BB_UPDATEDATA,

  //Driver version
  IOCTL_BB_GET_VERSION

};

#endif /* __GPSDRV_H__ */



