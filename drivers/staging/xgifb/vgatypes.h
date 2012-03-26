#ifndef _VGATYPES_
#define _VGATYPES_

#include <linux/ioctl.h>
#include <linux/fb.h>	/* for struct fb_var_screeninfo for sis.h */
#include "../../video/sis/vgatypes.h"
#include "../../video/sis/sis.h"		/* for LCD_TYPE */

#ifndef XGI_VB_CHIP_TYPE
enum XGI_VB_CHIP_TYPE {
	VB_CHIP_Legacy = 0,
	VB_CHIP_301,
	VB_CHIP_301B,
	VB_CHIP_301LV,
	VB_CHIP_302,
	VB_CHIP_302B,
	VB_CHIP_302LV,
	VB_CHIP_301C,
	VB_CHIP_302ELV,
	VB_CHIP_UNKNOWN, /* other video bridge or no video bridge */
	MAX_VB_CHIP
};
#endif


#define XGI_LCD_TYPE
/* Since the merge with video/sis the LCD_TYPEs are used from
 drivers/video/sis/sis.h . Nevertheless we keep this (for the moment) for
 future reference until the code is merged completely and we are sure
 nothing of this should be added to the sis.h header */
#ifndef XGI_LCD_TYPE
enum XGI_LCD_TYPE {
	LCD_INVALID = 0,
	LCD_320x480,       /* FSTN, DSTN */
	LCD_640x480,
	LCD_640x480_2,     /* FSTN, DSTN */
	LCD_640x480_3,     /* FSTN, DSTN */
	LCD_800x600,
	LCD_848x480,
	LCD_1024x600,
	LCD_1024x768,
	LCD_1152x768,
	LCD_1152x864,
	LCD_1280x720,
	LCD_1280x768,
	LCD_1280x800,
	LCD_1280x960,
	LCD_1280x1024,
	LCD_1400x1050,
	LCD_1600x1200,
	LCD_1680x1050,
	LCD_1920x1440,
	LCD_2048x1536,
	LCD_CUSTOM,
	LCD_UNKNOWN
};
#endif

struct xgi_hw_device_info {
	unsigned long ulExternalChip; /* NO VB or other video bridge*/
				      /* if ujVBChipID = VB_CHIP_UNKNOWN, */

	void __iomem *pjVideoMemoryAddress;/* base virtual memory address */
					    /* of Linear VGA memory */

	unsigned long ulVideoMemorySize; /* size, in bytes, of the
					    memory on the board */

	unsigned char *pjIOAddress; /* base I/O address of VGA ports (0x3B0) */

	unsigned char jChipType; /* Used to Identify Graphics Chip */
				 /* defined in the data structure type  */
				 /* "XGI_CHIP_TYPE" */

	unsigned char jChipRevision; /* Used to Identify Graphics
					Chip Revision */

	unsigned char ujVBChipID; /* the ID of video bridge */
				  /* defined in the data structure type */
				  /* "XGI_VB_CHIP_TYPE" */

	unsigned long ulCRT2LCDType; /* defined in the data structure type */
};

/* Additional IOCTL for communication xgifb <> X driver        */
/* If changing this, xgifb.h must also be changed (for xgifb) */
#endif

