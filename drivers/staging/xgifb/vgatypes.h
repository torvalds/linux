#ifndef _VGATYPES_
#define _VGATYPES_

#include <linux/fb.h>	/* for struct fb_var_screeninfo for sis.h */
#include "../../video/fbdev/sis/vgatypes.h"
#include "../../video/fbdev/sis/sis.h"		/* for LCD_TYPE */

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

struct xgi_hw_device_info {
	unsigned long ulExternalChip; /* NO VB or other video bridge*/
				      /* if ujVBChipID = VB_CHIP_UNKNOWN, */

	void __iomem *pjVideoMemoryAddress;/* base virtual memory address */
					    /* of Linear VGA memory */

	unsigned long ulVideoMemorySize; /* size, in bytes, of the
					  * memory on the board
					  */

	unsigned char jChipType; /* Used to Identify Graphics Chip */
				 /* defined in the data structure type  */
				 /* "XGI_CHIP_TYPE" */

	unsigned char jChipRevision; /* Used to Identify Graphics
				      * Chip Revision
				      */

	unsigned char ujVBChipID; /* the ID of video bridge */
				  /* defined in the data structure type */
				  /* "XGI_VB_CHIP_TYPE" */

	unsigned long ulCRT2LCDType; /* defined in the data structure type */
};

/* Additional IOCTL for communication xgifb <> X driver        */
/* If changing this, xgifb.h must also be changed (for xgifb) */
#endif
