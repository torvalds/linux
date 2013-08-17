//[*]--------------------------------------------------------------------------------------------------[*]
/*
 *	
 * HardKernel-C1XX _HKC1XX_TOUCH_H_ Header file(charles.park) 
 *
 */
//[*]--------------------------------------------------------------------------------------------------[*]
#ifndef	_NEXIO_USB_TOUCH_H_
#define	_NEXIO_USB_TOUCH_H_

//[*]--------------------------------------------------------------------------------------------------[*]
#ifdef CONFIG_HAS_EARLYSUSPEND
	#include <linux/earlysuspend.h>
#endif

//[*]--------------------------------------------------------------------------------------------------[*]
#define	NEXIO_USB_TOUCH_VENDOR			0x1870
#define	NEXIO_USB_TOUCH_PRODUCT			0x0100
//#define NEXIO_USB_TOUCH_DEVICE_NAME 	"nexio-touch"
#define NEXIO_USB_TOUCH_DEVICE_NAME 	"odroidxu-ts"

#define	NEXIO_USB_TOUCH_LANDSCAPE
//[*]--------------------------------------------------------------------------------------------------[*]
#define	TS_ABS_MIN_X			0
#define	TS_ABS_MIN_Y			0
#define	TS_ABS_MAX_X			0x3FFF
#define	TS_ABS_MAX_Y			0x3FFF

//[*]--------------------------------------------------------------------------------------------------[*]
#define	TOUCH_DATA_PRESS		0
#define	TOUCH_DATA_RELEASE		2
#define	TOUCH_DATA_MOVE			3

#define	TOUCH_PRESS				1
#define	TOUCH_RELEASE			0
		
//[*]--------------------------------------------------------------------------------------------------[*]
typedef	struct	nexio_touch__t	{
	struct	input_dev		*driver;

	// data store
	unsigned int			status;
	unsigned int			x;
	unsigned int			y;

	#ifdef CONFIG_HAS_EARLYSUSPEND
		struct	early_suspend		power;
	#endif

}	nexio_touch_t;

extern	nexio_touch_t	nexio_touch;

//[*]--------------------------------------------------------------------------------------------------[*]
#endif		/* _NEXIO_USB_TOUCH_H_ */
//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
