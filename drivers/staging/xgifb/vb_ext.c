#include <linux/io.h>
#include <linux/types.h>
#include "XGIfb.h"

#include "vb_def.h"
#include "vgatypes.h"
#include "vb_struct.h"
#include "vb_util.h"
#include "vb_setmode.h"
#include "vb_ext.h"

/**************************************************************
 *********************** Dynamic Sense ************************
 *************************************************************/

static unsigned char
XGINew_GetLCDDDCInfo(struct xgi_hw_device_info *HwDeviceExtension,
		     struct vb_device_info *pVBInfo)
{
	unsigned short temp;

	/* add lcd sense */
	if (HwDeviceExtension->ulCRT2LCDType == LCD_UNKNOWN) {
		return 0;
	} else {
		temp = (unsigned short) HwDeviceExtension->ulCRT2LCDType;
		switch (HwDeviceExtension->ulCRT2LCDType) {
		case LCD_INVALID:
		case LCD_800x600:
		case LCD_1024x768:
		case LCD_1280x1024:
			break;

		case LCD_640x480:
		case LCD_1024x600:
		case LCD_1152x864:
		case LCD_1280x960:
		case LCD_1152x768:
			temp = 0;
			break;

		case LCD_1400x1050:
		case LCD_1280x768:
		case LCD_1600x1200:
			break;

		case LCD_1920x1440:
		case LCD_2048x1536:
			temp = 0;
			break;

		default:
			break;
		}
		xgifb_reg_and_or(pVBInfo->P3d4, 0x36, 0xF0, temp);
		return 1;
	}
}

unsigned short XGINew_SenseLCD(struct xgi_hw_device_info *HwDeviceExtension,
			       struct vb_device_info *pVBInfo)
{
	/* unsigned short SoftSetting ; */
	unsigned short temp;

	temp = XGINew_GetLCDDDCInfo(HwDeviceExtension, pVBInfo);

	return temp;
}
