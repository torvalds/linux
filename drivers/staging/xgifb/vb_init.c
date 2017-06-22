#include <linux/delay.h>
#include <linux/vmalloc.h>

#include "XGIfb.h"
#include "vb_def.h"
#include "vb_util.h"
#include "vb_setmode.h"
#include "vb_init.h"
static const unsigned short XGINew_DDRDRAM_TYPE340[4][2] = {
	{ 16, 0x45},
	{  8, 0x35},
	{  4, 0x31},
	{  2, 0x21} };

static const unsigned short XGINew_DDRDRAM_TYPE20[12][2] = {
	{ 128, 0x5D},
	{ 64, 0x59},
	{ 64, 0x4D},
	{ 32, 0x55},
	{ 32, 0x49},
	{ 32, 0x3D},
	{ 16, 0x51},
	{ 16, 0x45},
	{ 16, 0x39},
	{  8, 0x41},
	{  8, 0x35},
	{  4, 0x31} };

#define XGIFB_ROM_SIZE	65536

static unsigned char
XGINew_GetXG20DRAMType(struct xgi_hw_device_info *HwDeviceExtension,
		       struct vb_device_info *pVBInfo)
{
	unsigned char data, temp;

	if (HwDeviceExtension->jChipType < XG20) {
		data = xgifb_reg_get(pVBInfo->P3c4, 0x39) & 0x02;
		if (data == 0)
			data = (xgifb_reg_get(pVBInfo->P3c4, 0x3A) &
				   0x02) >> 1;
		return data;
	} else if (HwDeviceExtension->jChipType == XG27) {
		temp = xgifb_reg_get(pVBInfo->P3c4, 0x3B);
		/* SR3B[7][3]MAA15 MAA11 (Power on Trapping) */
		if (((temp & 0x88) == 0x80) || ((temp & 0x88) == 0x08))
			data = 0; /* DDR */
		else
			data = 1; /* DDRII */
		return data;
	} else if (HwDeviceExtension->jChipType == XG21) {
		/* Independent GPIO control */
		xgifb_reg_and(pVBInfo->P3d4, 0xB4, ~0x02);
		usleep_range(800, 1800);
		xgifb_reg_or(pVBInfo->P3d4, 0x4A, 0x80); /* Enable GPIOH read */
		/* GPIOF 0:DVI 1:DVO */
		data = xgifb_reg_get(pVBInfo->P3d4, 0x48);
		/*
		 * HOTPLUG_SUPPORT
		 * for current XG20 & XG21, GPIOH is floating, driver will
		 * fix DDR temporarily
		 */
		/* DVI read GPIOH */
		data &= 0x01; /* 1=DDRII, 0=DDR */
		/* ~HOTPLUG_SUPPORT */
		xgifb_reg_or(pVBInfo->P3d4, 0xB4, 0x02);
		return data;
	}
	data = xgifb_reg_get(pVBInfo->P3d4, 0x97) & 0x01;

	if (data == 1)
		data++;

	return data;
}

static void XGINew_DDR1x_MRS_340(unsigned long P3c4,
				 struct vb_device_info *pVBInfo)
{
	xgifb_reg_set(P3c4, 0x18, 0x01);
	xgifb_reg_set(P3c4, 0x19, 0x20);
	xgifb_reg_set(P3c4, 0x16, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x80);

	usleep_range(3, 1003);
	xgifb_reg_set(P3c4, 0x18, 0x00);
	xgifb_reg_set(P3c4, 0x19, 0x20);
	xgifb_reg_set(P3c4, 0x16, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x80);

	usleep_range(60, 1060);
	xgifb_reg_set(P3c4, 0x18, pVBInfo->SR18[pVBInfo->ram_type]); /* SR18 */
	xgifb_reg_set(P3c4, 0x19, 0x01);
	xgifb_reg_set(P3c4, 0x16, 0x03);
	xgifb_reg_set(P3c4, 0x16, 0x83);
	usleep_range(1, 1001);
	xgifb_reg_set(P3c4, 0x1B, 0x03);
	usleep_range(500, 1500);
	xgifb_reg_set(P3c4, 0x18, pVBInfo->SR18[pVBInfo->ram_type]); /* SR18 */
	xgifb_reg_set(P3c4, 0x19, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x03);
	xgifb_reg_set(P3c4, 0x16, 0x83);
	xgifb_reg_set(P3c4, 0x1B, 0x00);
}

static void XGINew_SetMemoryClock(struct vb_device_info *pVBInfo)
{
	xgifb_reg_set(pVBInfo->P3c4,
		      0x28,
		      pVBInfo->MCLKData[pVBInfo->ram_type].SR28);
	xgifb_reg_set(pVBInfo->P3c4,
		      0x29,
		      pVBInfo->MCLKData[pVBInfo->ram_type].SR29);
	xgifb_reg_set(pVBInfo->P3c4,
		      0x2A,
		      pVBInfo->MCLKData[pVBInfo->ram_type].SR2A);

	xgifb_reg_set(pVBInfo->P3c4,
		      0x2E,
		      XGI340_ECLKData[pVBInfo->ram_type].SR2E);
	xgifb_reg_set(pVBInfo->P3c4,
		      0x2F,
		      XGI340_ECLKData[pVBInfo->ram_type].SR2F);
	xgifb_reg_set(pVBInfo->P3c4,
		      0x30,
		      XGI340_ECLKData[pVBInfo->ram_type].SR30);
}

static void XGINew_DDRII_Bootup_XG27(
			struct xgi_hw_device_info *HwDeviceExtension,
			unsigned long P3c4, struct vb_device_info *pVBInfo)
{
	unsigned long P3d4 = P3c4 + 0x10;

	pVBInfo->ram_type = XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo);
	XGINew_SetMemoryClock(pVBInfo);

	/* Set Double Frequency */
	xgifb_reg_set(P3d4, 0x97, pVBInfo->XGINew_CR97); /* CR97 */

	usleep_range(200, 1200);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* Set SR18 */ /* EMRS2 */
	xgifb_reg_set(P3c4, 0x19, 0x80); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	usleep_range(15, 1015);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	usleep_range(15, 1015);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* Set SR18 */ /* EMRS3 */
	xgifb_reg_set(P3c4, 0x19, 0xC0); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	usleep_range(15, 1015);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	usleep_range(15, 1015);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* Set SR18 */ /* EMRS1 */
	xgifb_reg_set(P3c4, 0x19, 0x40); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	usleep_range(30, 1030);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	usleep_range(15, 1015);

	xgifb_reg_set(P3c4, 0x18, 0x42); /* Set SR18 */ /* MRS, DLL Enable */
	xgifb_reg_set(P3c4, 0x19, 0x0A); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x00); /* Set SR16 */
	usleep_range(30, 1030);
	xgifb_reg_set(P3c4, 0x16, 0x00); /* Set SR16 */
	xgifb_reg_set(P3c4, 0x16, 0x80); /* Set SR16 */

	xgifb_reg_set(P3c4, 0x1B, 0x04); /* Set SR1B */
	usleep_range(60, 1060);
	xgifb_reg_set(P3c4, 0x1B, 0x00); /* Set SR1B */

	xgifb_reg_set(P3c4, 0x18, 0x42); /* Set SR18 */ /* MRS, DLL Reset */
	xgifb_reg_set(P3c4, 0x19, 0x08); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x00); /* Set SR16 */

	usleep_range(30, 1030);
	xgifb_reg_set(P3c4, 0x16, 0x83); /* Set SR16 */
	usleep_range(15, 1015);

	xgifb_reg_set(P3c4, 0x18, 0x80); /* Set SR18 */ /* MRS, ODT */
	xgifb_reg_set(P3c4, 0x19, 0x46); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	usleep_range(30, 1030);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	usleep_range(15, 1015);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* Set SR18 */ /* EMRS */
	xgifb_reg_set(P3c4, 0x19, 0x40); /* Set SR19 */
	xgifb_reg_set(P3c4, 0x16, 0x20); /* Set SR16 */
	usleep_range(30, 1030);
	xgifb_reg_set(P3c4, 0x16, 0xA0); /* Set SR16 */
	usleep_range(15, 1015);

	/* Set SR1B refresh control 000:close; 010:open */
	xgifb_reg_set(P3c4, 0x1B, 0x04);
	usleep_range(200, 1200);
}

static void XGINew_DDR2_MRS_XG20(struct xgi_hw_device_info *HwDeviceExtension,
				 unsigned long P3c4,
				 struct vb_device_info *pVBInfo)
{
	unsigned long P3d4 = P3c4 + 0x10;

	pVBInfo->ram_type = XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo);
	XGINew_SetMemoryClock(pVBInfo);

	xgifb_reg_set(P3d4, 0x97, 0x11); /* CR97 */

	usleep_range(200, 1200);
	xgifb_reg_set(P3c4, 0x18, 0x00); /* EMRS2 */
	xgifb_reg_set(P3c4, 0x19, 0x80);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* EMRS3 */
	xgifb_reg_set(P3c4, 0x19, 0xC0);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	xgifb_reg_set(P3c4, 0x18, 0x00); /* EMRS1 */
	xgifb_reg_set(P3c4, 0x19, 0x40);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	xgifb_reg_set(P3c4, 0x18, 0x42); /* MRS1 */
	xgifb_reg_set(P3c4, 0x19, 0x02);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	usleep_range(15, 1015);
	xgifb_reg_set(P3c4, 0x1B, 0x04); /* SR1B */
	usleep_range(30, 1030);
	xgifb_reg_set(P3c4, 0x1B, 0x00); /* SR1B */
	usleep_range(100, 1100);

	xgifb_reg_set(P3c4, 0x18, 0x42); /* MRS1 */
	xgifb_reg_set(P3c4, 0x19, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x05);
	xgifb_reg_set(P3c4, 0x16, 0x85);

	usleep_range(200, 1200);
}

static void XGINew_DDR1x_MRS_XG20(unsigned long P3c4,
				  struct vb_device_info *pVBInfo)
{
	xgifb_reg_set(P3c4, 0x18, 0x01);
	xgifb_reg_set(P3c4, 0x19, 0x40);
	xgifb_reg_set(P3c4, 0x16, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x80);
	usleep_range(60, 1060);

	xgifb_reg_set(P3c4, 0x18, 0x00);
	xgifb_reg_set(P3c4, 0x19, 0x40);
	xgifb_reg_set(P3c4, 0x16, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x80);
	usleep_range(60, 1060);
	xgifb_reg_set(P3c4, 0x18, pVBInfo->SR18[pVBInfo->ram_type]); /* SR18 */
	xgifb_reg_set(P3c4, 0x19, 0x01);
	xgifb_reg_set(P3c4, 0x16, 0x03);
	xgifb_reg_set(P3c4, 0x16, 0x83);
	usleep_range(1, 1001);
	xgifb_reg_set(P3c4, 0x1B, 0x03);
	usleep_range(500, 1500);
	xgifb_reg_set(P3c4, 0x18, pVBInfo->SR18[pVBInfo->ram_type]); /* SR18 */
	xgifb_reg_set(P3c4, 0x19, 0x00);
	xgifb_reg_set(P3c4, 0x16, 0x03);
	xgifb_reg_set(P3c4, 0x16, 0x83);
	xgifb_reg_set(P3c4, 0x1B, 0x00);
}

static void XGINew_DDR1x_DefaultRegister(
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned long Port, struct vb_device_info *pVBInfo)
{
	unsigned long P3d4 = Port, P3c4 = Port - 0x10;

	if (HwDeviceExtension->jChipType >= XG20) {
		XGINew_SetMemoryClock(pVBInfo);
		xgifb_reg_set(P3d4,
			      0x82,
			      pVBInfo->CR40[11][pVBInfo->ram_type]); /* CR82 */
		xgifb_reg_set(P3d4,
			      0x85,
			      pVBInfo->CR40[12][pVBInfo->ram_type]); /* CR85 */
		xgifb_reg_set(P3d4,
			      0x86,
			      pVBInfo->CR40[13][pVBInfo->ram_type]); /* CR86 */

		xgifb_reg_set(P3d4, 0x98, 0x01);
		xgifb_reg_set(P3d4, 0x9A, 0x02);

		XGINew_DDR1x_MRS_XG20(P3c4, pVBInfo);
	} else {
		XGINew_SetMemoryClock(pVBInfo);

		switch (HwDeviceExtension->jChipType) {
		case XG42:
			/* CR82 */
			xgifb_reg_set(P3d4,
				      0x82,
				      pVBInfo->CR40[11][pVBInfo->ram_type]);
			/* CR85 */
			xgifb_reg_set(P3d4,
				      0x85,
				      pVBInfo->CR40[12][pVBInfo->ram_type]);
			/* CR86 */
			xgifb_reg_set(P3d4,
				      0x86,
				      pVBInfo->CR40[13][pVBInfo->ram_type]);
			break;
		default:
			xgifb_reg_set(P3d4, 0x82, 0x88);
			xgifb_reg_set(P3d4, 0x86, 0x00);
			/* Insert read command for delay */
			xgifb_reg_get(P3d4, 0x86);
			xgifb_reg_set(P3d4, 0x86, 0x88);
			xgifb_reg_get(P3d4, 0x86);
			xgifb_reg_set(P3d4,
				      0x86,
				      pVBInfo->CR40[13][pVBInfo->ram_type]);
			xgifb_reg_set(P3d4, 0x82, 0x77);
			xgifb_reg_set(P3d4, 0x85, 0x00);

			/* Insert read command for delay */
			xgifb_reg_get(P3d4, 0x85);
			xgifb_reg_set(P3d4, 0x85, 0x88);

			/* Insert read command for delay */
			xgifb_reg_get(P3d4, 0x85);
			/* CR85 */
			xgifb_reg_set(P3d4,
				      0x85,
				      pVBInfo->CR40[12][pVBInfo->ram_type]);
			/* CR82 */
			xgifb_reg_set(P3d4,
				      0x82,
				      pVBInfo->CR40[11][pVBInfo->ram_type]);
			break;
		}

		xgifb_reg_set(P3d4, 0x97, 0x00);
		xgifb_reg_set(P3d4, 0x98, 0x01);
		xgifb_reg_set(P3d4, 0x9A, 0x02);
		XGINew_DDR1x_MRS_340(P3c4, pVBInfo);
	}
}

static void XGINew_DDR2_DefaultRegister(
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned long Port, struct vb_device_info *pVBInfo)
{
	unsigned long P3d4 = Port, P3c4 = Port - 0x10;
	/*
	 * keep following setting sequence, each setting in
	 * the same reg insert idle
	 */
	xgifb_reg_set(P3d4, 0x82, 0x77);
	xgifb_reg_set(P3d4, 0x86, 0x00);
	xgifb_reg_get(P3d4, 0x86); /* Insert read command for delay */
	xgifb_reg_set(P3d4, 0x86, 0x88);
	xgifb_reg_get(P3d4, 0x86); /* Insert read command for delay */
	/* CR86 */
	xgifb_reg_set(P3d4, 0x86, pVBInfo->CR40[13][pVBInfo->ram_type]);
	xgifb_reg_set(P3d4, 0x82, 0x77);
	xgifb_reg_set(P3d4, 0x85, 0x00);
	xgifb_reg_get(P3d4, 0x85); /* Insert read command for delay */
	xgifb_reg_set(P3d4, 0x85, 0x88);
	xgifb_reg_get(P3d4, 0x85); /* Insert read command for delay */
	xgifb_reg_set(P3d4,
		      0x85,
		      pVBInfo->CR40[12][pVBInfo->ram_type]); /* CR85 */
	if (HwDeviceExtension->jChipType == XG27)
		/* CR82 */
		xgifb_reg_set(P3d4, 0x82, pVBInfo->CR40[11][pVBInfo->ram_type]);
	else
		xgifb_reg_set(P3d4, 0x82, 0xA8); /* CR82 */

	xgifb_reg_set(P3d4, 0x98, 0x01);
	xgifb_reg_set(P3d4, 0x9A, 0x02);
	if (HwDeviceExtension->jChipType == XG27)
		XGINew_DDRII_Bootup_XG27(HwDeviceExtension, P3c4, pVBInfo);
	else
		XGINew_DDR2_MRS_XG20(HwDeviceExtension, P3c4, pVBInfo);
}

static void XGI_SetDRAM_Helper(unsigned long P3d4, u8 seed, u8 temp2, u8 reg,
			       u8 shift_factor, u8 mask1, u8 mask2)
{
	u8 j;

	for (j = 0; j < 4; j++) {
		temp2 |= (((seed >> (2 * j)) & 0x03) << shift_factor);
		xgifb_reg_set(P3d4, reg, temp2);
		xgifb_reg_get(P3d4, reg);
		temp2 &= mask1;
		temp2 += mask2;
	}
}

static void XGINew_SetDRAMDefaultRegister340(
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned long Port, struct vb_device_info *pVBInfo)
{
	unsigned char temp, temp1, temp2, temp3, j, k;

	unsigned long P3d4 = Port, P3c4 = Port - 0x10;

	xgifb_reg_set(P3d4, 0x6D, pVBInfo->CR40[8][pVBInfo->ram_type]);
	xgifb_reg_set(P3d4, 0x68, pVBInfo->CR40[5][pVBInfo->ram_type]);
	xgifb_reg_set(P3d4, 0x69, pVBInfo->CR40[6][pVBInfo->ram_type]);
	xgifb_reg_set(P3d4, 0x6A, pVBInfo->CR40[7][pVBInfo->ram_type]);

	/* CR6B DQS fine tune delay */
	temp = 0xaa;
	XGI_SetDRAM_Helper(P3d4, temp, 0, 0x6B, 2, 0xF0, 0x10);

	/* CR6E DQM fine tune delay */
	XGI_SetDRAM_Helper(P3d4, 0, 0, 0x6E, 2, 0xF0, 0x10);

	temp3 = 0;
	for (k = 0; k < 4; k++) {
		/* CR6E_D[1:0] select channel */
		xgifb_reg_and_or(P3d4, 0x6E, 0xFC, temp3);
		XGI_SetDRAM_Helper(P3d4, 0, 0, 0x6F, 0, 0xF8, 0x08);
		temp3 += 0x01;
	}

	xgifb_reg_set(P3d4,
		      0x80,
		      pVBInfo->CR40[9][pVBInfo->ram_type]); /* CR80 */
	xgifb_reg_set(P3d4,
		      0x81,
		      pVBInfo->CR40[10][pVBInfo->ram_type]); /* CR81 */

	temp2 = 0x80;
	/* CR89 terminator type select */
	XGI_SetDRAM_Helper(P3d4, 0, temp2, 0x89, 0, 0xF0, 0x10);

	temp = 0;
	temp1 = temp & 0x03;
	temp2 |= temp1;
	xgifb_reg_set(P3d4, 0x89, temp2);

	temp = pVBInfo->CR40[3][pVBInfo->ram_type];
	temp1 = temp & 0x0F;
	temp2 = (temp >> 4) & 0x07;
	temp3 = temp & 0x80;
	xgifb_reg_set(P3d4, 0x45, temp1); /* CR45 */
	xgifb_reg_set(P3d4, 0x99, temp2); /* CR99 */
	xgifb_reg_or(P3d4, 0x40, temp3); /* CR40_D[7] */
	xgifb_reg_set(P3d4,
		      0x41,
		      pVBInfo->CR40[0][pVBInfo->ram_type]); /* CR41 */

	if (HwDeviceExtension->jChipType == XG27)
		xgifb_reg_set(P3d4, 0x8F, XG27_CR8F); /* CR8F */

	for (j = 0; j <= 6; j++) /* CR90 - CR96 */
		xgifb_reg_set(P3d4, (0x90 + j),
			      pVBInfo->CR40[14 + j][pVBInfo->ram_type]);

	for (j = 0; j <= 2; j++) /* CRC3 - CRC5 */
		xgifb_reg_set(P3d4, (0xC3 + j),
			      pVBInfo->CR40[21 + j][pVBInfo->ram_type]);

	for (j = 0; j < 2; j++) /* CR8A - CR8B */
		xgifb_reg_set(P3d4, (0x8A + j),
			      pVBInfo->CR40[1 + j][pVBInfo->ram_type]);

	if (HwDeviceExtension->jChipType == XG42)
		xgifb_reg_set(P3d4, 0x8C, 0x87);

	xgifb_reg_set(P3d4,
		      0x59,
		      pVBInfo->CR40[4][pVBInfo->ram_type]); /* CR59 */

	xgifb_reg_set(P3d4, 0x83, 0x09); /* CR83 */
	xgifb_reg_set(P3d4, 0x87, 0x00); /* CR87 */
	xgifb_reg_set(P3d4, 0xCF, XG40_CRCF); /* CRCF */
	if (pVBInfo->ram_type) {
		xgifb_reg_set(P3c4, 0x17, 0x80); /* SR17 DDRII */
		if (HwDeviceExtension->jChipType == XG27)
			xgifb_reg_set(P3c4, 0x17, 0x02); /* SR17 DDRII */

	} else {
		xgifb_reg_set(P3c4, 0x17, 0x00); /* SR17 DDR */
	}
	xgifb_reg_set(P3c4, 0x1A, 0x87); /* SR1A */

	temp = XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo);
	if (temp == 0) {
		XGINew_DDR1x_DefaultRegister(HwDeviceExtension, P3d4, pVBInfo);
	} else {
		xgifb_reg_set(P3d4, 0xB0, 0x80); /* DDRII Dual frequency mode */
		XGINew_DDR2_DefaultRegister(HwDeviceExtension, P3d4, pVBInfo);
	}
	xgifb_reg_set(P3c4, 0x1B, 0x03); /* SR1B */
}

static unsigned short XGINew_SetDRAMSize20Reg(
		unsigned short dram_size,
		struct vb_device_info *pVBInfo)
{
	unsigned short data = 0, memsize = 0;
	int RankSize;
	unsigned char ChannelNo;

	RankSize = dram_size * pVBInfo->ram_bus / 8;
	data = xgifb_reg_get(pVBInfo->P3c4, 0x13);
	data &= 0x80;

	if (data == 0x80)
		RankSize *= 2;

	data = 0;

	if (pVBInfo->ram_channel == 3)
		ChannelNo = 4;
	else
		ChannelNo = pVBInfo->ram_channel;

	if (ChannelNo * RankSize <= 256) {
		while ((RankSize >>= 1) > 0)
			data += 0x10;

		memsize = data >> 4;

		/* Fix DRAM Sizing Error */
		xgifb_reg_set(pVBInfo->P3c4,
			      0x14,
			      (xgifb_reg_get(pVBInfo->P3c4, 0x14) & 0x0F) |
				(data & 0xF0));
		usleep_range(15, 1015);
	}
	return memsize;
}

static int XGINew_ReadWriteRest(unsigned short StopAddr,
				unsigned short StartAddr,
				struct vb_device_info *pVBInfo)
{
	int i;
	unsigned long Position = 0;
	void __iomem *fbaddr = pVBInfo->FBAddr;

	writel(Position, fbaddr + Position);

	for (i = StartAddr; i <= StopAddr; i++) {
		Position = 1 << i;
		writel(Position, fbaddr + Position);
	}

	/* Fix #1759 Memory Size error in Multi-Adapter. */
	usleep_range(500, 1500);

	Position = 0;

	if (readl(fbaddr + Position) != Position)
		return 0;

	for (i = StartAddr; i <= StopAddr; i++) {
		Position = 1 << i;
		if (readl(fbaddr + Position) != Position)
			return 0;
	}
	return 1;
}

static unsigned char XGINew_CheckFrequence(struct vb_device_info *pVBInfo)
{
	unsigned char data;

	data = xgifb_reg_get(pVBInfo->P3d4, 0x97);

	if ((data & 0x10) == 0) {
		data = xgifb_reg_get(pVBInfo->P3c4, 0x39);
		return (data & 0x02) >> 1;
	}
	return data & 0x01;
}

static void XGINew_CheckChannel(struct xgi_hw_device_info *HwDeviceExtension,
				struct vb_device_info *pVBInfo)
{
	unsigned char data;

	switch (HwDeviceExtension->jChipType) {
	case XG20:
	case XG21:
		data = xgifb_reg_get(pVBInfo->P3d4, 0x97);
		data = data & 0x01;
		pVBInfo->ram_channel = 1; /* XG20 "JUST" one channel */

		if (data == 0) { /* Single_32_16 */

			if ((HwDeviceExtension->ulVideoMemorySize - 1)
					> 0x1000000) {
				pVBInfo->ram_bus = 32; /* 32 bits */
				/* 22bit + 2 rank + 32bit */
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xB1);
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x52);
				usleep_range(15, 1015);

				if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
					return;

				if ((HwDeviceExtension->ulVideoMemorySize - 1) >
				    0x800000) {
					/* 22bit + 1 rank + 32bit */
					xgifb_reg_set(pVBInfo->P3c4,
						      0x13,
						      0x31);
					xgifb_reg_set(pVBInfo->P3c4,
						      0x14,
						      0x42);
					usleep_range(15, 1015);

					if (XGINew_ReadWriteRest(23,
								 23,
								 pVBInfo) == 1)
						return;
				}
			}

			if ((HwDeviceExtension->ulVideoMemorySize - 1) >
			    0x800000) {
				pVBInfo->ram_bus = 16; /* 16 bits */
				/* 22bit + 2 rank + 16bit */
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xB1);
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x41);
				usleep_range(15, 1015);

				if (XGINew_ReadWriteRest(23, 22, pVBInfo) == 1)
					return;
				xgifb_reg_set(pVBInfo->P3c4,
					      0x13,
					      0x31);
				usleep_range(15, 1015);
			}

		} else { /* Dual_16_8 */
			if ((HwDeviceExtension->ulVideoMemorySize - 1) >
			    0x800000) {
				pVBInfo->ram_bus = 16; /* 16 bits */
				/* (0x31:12x8x2) 22bit + 2 rank */
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xB1);
				/* 0x41:16Mx16 bit */
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x41);
				usleep_range(15, 1015);

				if (XGINew_ReadWriteRest(23, 22, pVBInfo) == 1)
					return;

				if ((HwDeviceExtension->ulVideoMemorySize - 1) >
				    0x400000) {
					/* (0x31:12x8x2) 22bit + 1 rank */
					xgifb_reg_set(pVBInfo->P3c4,
						      0x13,
						      0x31);
					/* 0x31:8Mx16 bit */
					xgifb_reg_set(pVBInfo->P3c4,
						      0x14,
						      0x31);
					usleep_range(15, 1015);

					if (XGINew_ReadWriteRest(22,
								 22,
								 pVBInfo) == 1)
						return;
				}
			}

			if ((HwDeviceExtension->ulVideoMemorySize - 1) >
			    0x400000) {
				pVBInfo->ram_bus = 8; /* 8 bits */
				/* (0x31:12x8x2) 22bit + 2 rank */
				xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xB1);
				/* 0x30:8Mx8 bit */
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x30);
				usleep_range(15, 1015);

				if (XGINew_ReadWriteRest(22, 21, pVBInfo) == 1)
					return;

				/* (0x31:12x8x2) 22bit + 1 rank */
				xgifb_reg_set(pVBInfo->P3c4,
					      0x13,
					      0x31);
				usleep_range(15, 1015);
			}
		}
		break;

	case XG27:
		pVBInfo->ram_bus = 16; /* 16 bits */
		pVBInfo->ram_channel = 1; /* Single channel */
		xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x51); /* 32Mx16 bit */
		break;
	case XG42:
		/*
		 * XG42 SR14 D[3] Reserve
		 * D[2] = 1, Dual Channel
		 * = 0, Single Channel
		 *
		 * It's Different from Other XG40 Series.
		 */
		if (XGINew_CheckFrequence(pVBInfo) == 1) { /* DDRII, DDR2x */
			pVBInfo->ram_bus = 32; /* 32 bits */
			pVBInfo->ram_channel = 2; /* 2 Channel */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x44);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x34);
			if (XGINew_ReadWriteRest(23, 22, pVBInfo) == 1)
				return;

			pVBInfo->ram_channel = 1; /* Single Channel */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x40);

			if (XGINew_ReadWriteRest(23, 22, pVBInfo) == 1)
				return;
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x30);
		} else { /* DDR */
			pVBInfo->ram_bus = 64; /* 64 bits */
			pVBInfo->ram_channel = 1; /* 1 channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x52);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x42);
		}

		break;

	default: /* XG40 */

		if (XGINew_CheckFrequence(pVBInfo) == 1) { /* DDRII */
			pVBInfo->ram_bus = 32; /* 32 bits */
			pVBInfo->ram_channel = 3;
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x4C);

			if (XGINew_ReadWriteRest(25, 23, pVBInfo) == 1)
				return;

			pVBInfo->ram_channel = 2; /* 2 channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x48);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1)
				return;

			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x3C);

			if (XGINew_ReadWriteRest(24, 23, pVBInfo) == 1) {
				pVBInfo->ram_channel = 3; /* 4 channels */
			} else {
				pVBInfo->ram_channel = 2; /* 2 channels */
				xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x38);
			}
		} else { /* DDR */
			pVBInfo->ram_bus = 64; /* 64 bits */
			pVBInfo->ram_channel = 2; /* 2 channels */
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0xA1);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x5A);

			if (XGINew_ReadWriteRest(25, 24, pVBInfo) == 1)
				return;
			xgifb_reg_set(pVBInfo->P3c4, 0x13, 0x21);
			xgifb_reg_set(pVBInfo->P3c4, 0x14, 0x4A);
		}
		break;
	}
}

static int XGINew_DDRSizing340(struct xgi_hw_device_info *HwDeviceExtension,
			       struct vb_device_info *pVBInfo)
{
	u8 i, size;
	unsigned short memsize, start_addr;
	const unsigned short (*dram_table)[2];

	xgifb_reg_set(pVBInfo->P3c4, 0x15, 0x00); /* noninterleaving */
	xgifb_reg_set(pVBInfo->P3c4, 0x1C, 0x00); /* nontiling */
	XGINew_CheckChannel(HwDeviceExtension, pVBInfo);

	if (HwDeviceExtension->jChipType >= XG20) {
		dram_table = XGINew_DDRDRAM_TYPE20;
		size = ARRAY_SIZE(XGINew_DDRDRAM_TYPE20);
		start_addr = 5;
	} else {
		dram_table = XGINew_DDRDRAM_TYPE340;
		size = ARRAY_SIZE(XGINew_DDRDRAM_TYPE340);
		start_addr = 9;
	}

	for (i = 0; i < size; i++) {
		/* SetDRAMSizingType */
		xgifb_reg_and_or(pVBInfo->P3c4, 0x13, 0x80, dram_table[i][1]);
		usleep_range(50, 1050); /* should delay 50 ns */

		memsize = XGINew_SetDRAMSize20Reg(dram_table[i][0], pVBInfo);

		if (memsize == 0)
			continue;

		memsize += (pVBInfo->ram_channel - 2) + 20;
		if ((HwDeviceExtension->ulVideoMemorySize - 1) <
			(unsigned long)(1 << memsize))
			continue;

		if (XGINew_ReadWriteRest(memsize, start_addr, pVBInfo) == 1)
			return 1;
	}
	return 0;
}

static void XGINew_SetDRAMSize_340(struct xgifb_video_info *xgifb_info,
				   struct xgi_hw_device_info *HwDeviceExtension,
				   struct vb_device_info *pVBInfo)
{
	unsigned short data;

	pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress;

	XGISetModeNew(xgifb_info, HwDeviceExtension, 0x2e);

	data = xgifb_reg_get(pVBInfo->P3c4, 0x21);
	/* disable read cache */
	xgifb_reg_set(pVBInfo->P3c4, 0x21, (unsigned short)(data & 0xDF));
	XGI_DisplayOff(xgifb_info, HwDeviceExtension, pVBInfo);

	XGINew_DDRSizing340(HwDeviceExtension, pVBInfo);
	data = xgifb_reg_get(pVBInfo->P3c4, 0x21);
	/* enable read cache */
	xgifb_reg_set(pVBInfo->P3c4, 0x21, (unsigned short)(data | 0x20));
}

static u8 *xgifb_copy_rom(struct pci_dev *dev, size_t *rom_size)
{
	void __iomem *rom_address;
	u8 *rom_copy;

	rom_address = pci_map_rom(dev, rom_size);
	if (!rom_address)
		return NULL;

	rom_copy = vzalloc(XGIFB_ROM_SIZE);
	if (!rom_copy)
		goto done;

	*rom_size = min_t(size_t, *rom_size, XGIFB_ROM_SIZE);
	memcpy_fromio(rom_copy, rom_address, *rom_size);

done:
	pci_unmap_rom(dev, rom_address);
	return rom_copy;
}

static bool xgifb_read_vbios(struct pci_dev *pdev)
{
	struct xgifb_video_info *xgifb_info = pci_get_drvdata(pdev);
	u8 *vbios;
	unsigned long i;
	unsigned char j;
	struct XGI21_LVDSCapStruct *lvds;
	size_t vbios_size;
	int entry;

	vbios = xgifb_copy_rom(pdev, &vbios_size);
	if (!vbios) {
		dev_err(&pdev->dev, "Video BIOS not available\n");
		return false;
	}
	if (vbios_size <= 0x65)
		goto error;
	/*
	 * The user can ignore the LVDS bit in the BIOS and force the display
	 * type.
	 */
	if (!(vbios[0x65] & 0x1) &&
	    (!xgifb_info->display2_force ||
	     xgifb_info->display2 != XGIFB_DISP_LCD)) {
		vfree(vbios);
		return false;
	}
	if (vbios_size <= 0x317)
		goto error;
	i = vbios[0x316] | (vbios[0x317] << 8);
	if (vbios_size <= i - 1)
		goto error;
	j = vbios[i - 1];
	if (j == 0)
		goto error;
	if (j == 0xff)
		j = 1;

	/* Read the LVDS table index scratch register set by the BIOS. */

	entry = xgifb_reg_get(xgifb_info->dev_info.P3d4, 0x36);
	if (entry >= j)
		entry = 0;
	i += entry * 25;
	lvds = &xgifb_info->lvds_data;
	if (vbios_size <= i + 24)
		goto error;
	lvds->LVDS_Capability	= vbios[i]	| (vbios[i + 1] << 8);
	lvds->LVDSHT		= vbios[i + 2]	| (vbios[i + 3] << 8);
	lvds->LVDSVT		= vbios[i + 4]	| (vbios[i + 5] << 8);
	lvds->LVDSHDE		= vbios[i + 6]	| (vbios[i + 7] << 8);
	lvds->LVDSVDE		= vbios[i + 8]	| (vbios[i + 9] << 8);
	lvds->LVDSHFP		= vbios[i + 10]	| (vbios[i + 11] << 8);
	lvds->LVDSVFP		= vbios[i + 12]	| (vbios[i + 13] << 8);
	lvds->LVDSHSYNC		= vbios[i + 14]	| (vbios[i + 15] << 8);
	lvds->LVDSVSYNC		= vbios[i + 16]	| (vbios[i + 17] << 8);
	lvds->VCLKData1		= vbios[i + 18];
	lvds->VCLKData2		= vbios[i + 19];
	lvds->PSC_S1		= vbios[i + 20];
	lvds->PSC_S2		= vbios[i + 21];
	lvds->PSC_S3		= vbios[i + 22];
	lvds->PSC_S4		= vbios[i + 23];
	lvds->PSC_S5		= vbios[i + 24];
	vfree(vbios);
	return true;
error:
	dev_err(&pdev->dev, "Video BIOS corrupted\n");
	vfree(vbios);
	return false;
}

static void XGINew_ChkSenseStatus(struct vb_device_info *pVBInfo)
{
	unsigned short tempbx = 0, temp, tempcx, CR3CData;

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x32);

	if (temp & Monitor1Sense)
		tempbx |= ActiveCRT1;
	if (temp & LCDSense)
		tempbx |= ActiveLCD;
	if (temp & Monitor2Sense)
		tempbx |= ActiveCRT2;
	if (temp & TVSense) {
		tempbx |= ActiveTV;
		if (temp & AVIDEOSense)
			tempbx |= (ActiveAVideo << 8);
		if (temp & SVIDEOSense)
			tempbx |= (ActiveSVideo << 8);
		if (temp & SCARTSense)
			tempbx |= (ActiveSCART << 8);
		if (temp & HiTVSense)
			tempbx |= (ActiveHiTV << 8);
		if (temp & YPbPrSense)
			tempbx |= (ActiveYPbPr << 8);
	}

	tempcx = xgifb_reg_get(pVBInfo->P3d4, 0x3d);
	tempcx |= (xgifb_reg_get(pVBInfo->P3d4, 0x3e) << 8);

	if (tempbx & tempcx) {
		CR3CData = xgifb_reg_get(pVBInfo->P3d4, 0x3c);
		if (!(CR3CData & DisplayDeviceFromCMOS))
			tempcx = 0x1FF0;
	} else {
		tempcx = 0x1FF0;
	}

	tempbx &= tempcx;
	xgifb_reg_set(pVBInfo->P3d4, 0x3d, (tempbx & 0x00FF));
	xgifb_reg_set(pVBInfo->P3d4, 0x3e, ((tempbx & 0xFF00) >> 8));
}

static void XGINew_SetModeScratch(struct vb_device_info *pVBInfo)
{
	unsigned short temp, tempcl = 0, tempch = 0, CR31Data, CR38Data;

	temp = xgifb_reg_get(pVBInfo->P3d4, 0x3d);
	temp |= xgifb_reg_get(pVBInfo->P3d4, 0x3e) << 8;
	temp |= (xgifb_reg_get(pVBInfo->P3d4, 0x31) & (DriverMode >> 8)) << 8;

	if (pVBInfo->IF_DEF_CRT2Monitor == 1) {
		if (temp & ActiveCRT2)
			tempcl = SetCRT2ToRAMDAC;
	}

	if (temp & ActiveLCD) {
		tempcl |= SetCRT2ToLCD;
		if (temp & DriverMode) {
			if (temp & ActiveTV) {
				tempch = SetToLCDA | EnableDualEdge;
				temp ^= SetCRT2ToLCD;

				if ((temp >> 8) & ActiveAVideo)
					tempcl |= SetCRT2ToAVIDEO;
				if ((temp >> 8) & ActiveSVideo)
					tempcl |= SetCRT2ToSVIDEO;
				if ((temp >> 8) & ActiveSCART)
					tempcl |= SetCRT2ToSCART;

				if (pVBInfo->IF_DEF_HiVision == 1) {
					if ((temp >> 8) & ActiveHiTV)
						tempcl |= SetCRT2ToHiVision;
				}

				if (pVBInfo->IF_DEF_YPbPr == 1) {
					if ((temp >> 8) & ActiveYPbPr)
						tempch |= SetYPbPr;
				}
			}
		}
	} else {
		if ((temp >> 8) & ActiveAVideo)
			tempcl |= SetCRT2ToAVIDEO;
		if ((temp >> 8) & ActiveSVideo)
			tempcl |= SetCRT2ToSVIDEO;
		if ((temp >> 8) & ActiveSCART)
			tempcl |= SetCRT2ToSCART;

		if (pVBInfo->IF_DEF_HiVision == 1) {
			if ((temp >> 8) & ActiveHiTV)
				tempcl |= SetCRT2ToHiVision;
		}

		if (pVBInfo->IF_DEF_YPbPr == 1) {
			if ((temp >> 8) & ActiveYPbPr)
				tempch |= SetYPbPr;
		}
	}

	tempcl |= SetSimuScanMode;
	if ((!(temp & ActiveCRT1)) && ((temp & ActiveLCD) ||
				       (temp & ActiveTV) ||
				       (temp & ActiveCRT2)))
		tempcl ^= (SetSimuScanMode | SwitchCRT2);
	if ((temp & ActiveLCD) && (temp & ActiveTV))
		tempcl ^= (SetSimuScanMode | SwitchCRT2);
	xgifb_reg_set(pVBInfo->P3d4, 0x30, tempcl);

	CR31Data = xgifb_reg_get(pVBInfo->P3d4, 0x31);
	CR31Data &= ~(SetNotSimuMode >> 8);
	if (!(temp & ActiveCRT1))
		CR31Data |= (SetNotSimuMode >> 8);
	CR31Data &= ~(DisableCRT2Display >> 8);
	if (!((temp & ActiveLCD) || (temp & ActiveTV) || (temp & ActiveCRT2)))
		CR31Data |= (DisableCRT2Display >> 8);
	xgifb_reg_set(pVBInfo->P3d4, 0x31, CR31Data);

	CR38Data = xgifb_reg_get(pVBInfo->P3d4, 0x38);
	CR38Data &= ~SetYPbPr;
	CR38Data |= tempch;
	xgifb_reg_set(pVBInfo->P3d4, 0x38, CR38Data);
}

static unsigned short XGINew_SenseLCD(struct xgi_hw_device_info
							*HwDeviceExtension,
				      struct vb_device_info *pVBInfo)
{
	unsigned short temp = HwDeviceExtension->ulCRT2LCDType;

	switch (HwDeviceExtension->ulCRT2LCDType) {
	case LCD_640x480:
	case LCD_1024x600:
	case LCD_1152x864:
	case LCD_1280x960:
	case LCD_1152x768:
	case LCD_1920x1440:
	case LCD_2048x1536:
		temp = 0; /* overwrite used ulCRT2LCDType */
		break;
	case LCD_UNKNOWN: /* unknown lcd, do nothing */
		return 0;
	}
	xgifb_reg_and_or(pVBInfo->P3d4, 0x36, 0xF0, temp);
	return 1;
}

static void XGINew_GetXG21Sense(struct pci_dev *pdev,
				struct vb_device_info *pVBInfo)
{
	struct xgifb_video_info *xgifb_info = pci_get_drvdata(pdev);
	unsigned char Temp;

	if (xgifb_read_vbios(pdev)) { /* For XG21 LVDS */
		xgifb_reg_or(pVBInfo->P3d4, 0x32, LCDSense);
		/* LVDS on chip */
		xgifb_reg_and_or(pVBInfo->P3d4, 0x38, ~0xE0, 0xC0);
	} else {
		/* Enable GPIOA/B read */
		xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x03, 0x03);
		Temp = xgifb_reg_get(pVBInfo->P3d4, 0x48) & 0xC0;
		if (Temp == 0xC0) { /* DVI & DVO GPIOA/B pull high */
			XGINew_SenseLCD(&xgifb_info->hw_info, pVBInfo);
			xgifb_reg_or(pVBInfo->P3d4, 0x32, LCDSense);
			/* Enable read GPIOF */
			xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x20, 0x20);
			if (xgifb_reg_get(pVBInfo->P3d4, 0x48) & 0x04)
				Temp = 0xA0; /* Only DVO on chip */
			else
				Temp = 0x80; /* TMDS on chip */
			xgifb_reg_and_or(pVBInfo->P3d4, 0x38, ~0xE0, Temp);
			/* Disable read GPIOF */
			xgifb_reg_and(pVBInfo->P3d4, 0x4A, ~0x20);
		}
	}
}

static void XGINew_GetXG27Sense(struct vb_device_info *pVBInfo)
{
	unsigned char Temp, bCR4A;

	bCR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	/* Enable GPIOA/B/C read */
	xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x07, 0x07);
	Temp = xgifb_reg_get(pVBInfo->P3d4, 0x48) & 0x07;
	xgifb_reg_set(pVBInfo->P3d4, 0x4A, bCR4A);

	if (Temp <= 0x02) {
		/* LVDS setting */
		xgifb_reg_and_or(pVBInfo->P3d4, 0x38, ~0xE0, 0xC0);
		xgifb_reg_set(pVBInfo->P3d4, 0x30, 0x21);
	} else {
		/* TMDS/DVO setting */
		xgifb_reg_and_or(pVBInfo->P3d4, 0x38, ~0xE0, 0xA0);
	}
	xgifb_reg_or(pVBInfo->P3d4, 0x32, LCDSense);
}

static unsigned char GetXG21FPBits(struct vb_device_info *pVBInfo)
{
	unsigned char CR38, CR4A, temp;

	CR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	/* enable GPIOE read */
	xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x10, 0x10);
	CR38 = xgifb_reg_get(pVBInfo->P3d4, 0x38);
	temp = 0;
	if ((CR38 & 0xE0) > 0x80) {
		temp = xgifb_reg_get(pVBInfo->P3d4, 0x48);
		temp &= 0x08;
		temp >>= 3;
	}

	xgifb_reg_set(pVBInfo->P3d4, 0x4A, CR4A);

	return temp;
}

static unsigned char GetXG27FPBits(struct vb_device_info *pVBInfo)
{
	unsigned char CR4A, temp;

	CR4A = xgifb_reg_get(pVBInfo->P3d4, 0x4A);
	/* enable GPIOA/B/C read */
	xgifb_reg_and_or(pVBInfo->P3d4, 0x4A, ~0x03, 0x03);
	temp = xgifb_reg_get(pVBInfo->P3d4, 0x48);
	if (temp > 2)
		temp = ((temp & 0x04) >> 1) | ((~temp) & 0x01);

	xgifb_reg_set(pVBInfo->P3d4, 0x4A, CR4A);

	return temp;
}

static bool xgifb_bridge_is_on(struct vb_device_info *vb_info)
{
	u8 flag;

	flag = xgifb_reg_get(vb_info->Part4Port, 0x00);
	return flag == 1 || flag == 2;
}

unsigned char XGIInitNew(struct pci_dev *pdev)
{
	struct xgifb_video_info *xgifb_info = pci_get_drvdata(pdev);
	struct xgi_hw_device_info *HwDeviceExtension = &xgifb_info->hw_info;
	struct vb_device_info VBINF;
	struct vb_device_info *pVBInfo = &VBINF;
	unsigned char i, temp = 0, temp1;

	pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress;

	if (!pVBInfo->FBAddr) {
		dev_dbg(&pdev->dev, "pVBInfo->FBAddr == 0\n");
		return 0;
	}

	XGIRegInit(pVBInfo, xgifb_info->vga_base);

	outb(0x67, pVBInfo->P3c2);

	InitTo330Pointer(HwDeviceExtension->jChipType, pVBInfo);

	/* Openkey */
	xgifb_reg_set(pVBInfo->P3c4, 0x05, 0x86);

	/* GetXG21Sense (GPIO) */
	if (HwDeviceExtension->jChipType == XG21)
		XGINew_GetXG21Sense(pdev, pVBInfo);

	if (HwDeviceExtension->jChipType == XG27)
		XGINew_GetXG27Sense(pVBInfo);

	/* Reset Extended register */

	for (i = 0x06; i < 0x20; i++)
		xgifb_reg_set(pVBInfo->P3c4, i, 0);

	for (i = 0x21; i <= 0x27; i++)
		xgifb_reg_set(pVBInfo->P3c4, i, 0);

	for (i = 0x31; i <= 0x3B; i++)
		xgifb_reg_set(pVBInfo->P3c4, i, 0);

	/* Auto over driver for XG42 */
	if (HwDeviceExtension->jChipType == XG42)
		xgifb_reg_set(pVBInfo->P3c4, 0x3B, 0xC0);

	for (i = 0x79; i <= 0x7C; i++)
		xgifb_reg_set(pVBInfo->P3d4, i, 0);

	if (HwDeviceExtension->jChipType >= XG20)
		xgifb_reg_set(pVBInfo->P3d4, 0x97, pVBInfo->XGINew_CR97);

	/* SetDefExt1Regs begin */
	xgifb_reg_set(pVBInfo->P3c4, 0x07, XGI330_SR07);
	if (HwDeviceExtension->jChipType == XG27) {
		xgifb_reg_set(pVBInfo->P3c4, 0x40, XG27_SR40);
		xgifb_reg_set(pVBInfo->P3c4, 0x41, XG27_SR41);
	}
	xgifb_reg_set(pVBInfo->P3c4, 0x11, 0x0F);
	xgifb_reg_set(pVBInfo->P3c4, 0x1F, XGI330_SR1F);
	/* Frame buffer can read/write SR20 */
	xgifb_reg_set(pVBInfo->P3c4, 0x20, 0xA0);
	/* H/W request for slow corner chip */
	xgifb_reg_set(pVBInfo->P3c4, 0x36, 0x70);
	if (HwDeviceExtension->jChipType == XG27)
		xgifb_reg_set(pVBInfo->P3c4, 0x36, XG27_SR36);

	if (HwDeviceExtension->jChipType < XG20) {
		u32 Temp;

		/* Set AGP customize registers (in SetDefAGPRegs) Start */
		for (i = 0x47; i <= 0x4C; i++)
			xgifb_reg_set(pVBInfo->P3d4,
				      i,
				      XGI340_AGPReg[i - 0x47]);

		for (i = 0x70; i <= 0x71; i++)
			xgifb_reg_set(pVBInfo->P3d4,
				      i,
				      XGI340_AGPReg[6 + i - 0x70]);

		for (i = 0x74; i <= 0x77; i++)
			xgifb_reg_set(pVBInfo->P3d4,
				      i,
				      XGI340_AGPReg[8 + i - 0x74]);

		pci_read_config_dword(pdev, 0x50, &Temp);
		Temp >>= 20;
		Temp &= 0xF;

		if (Temp == 1)
			xgifb_reg_set(pVBInfo->P3d4, 0x48, 0x20); /* CR48 */
	} /* != XG20 */

	/* Set PCI */
	xgifb_reg_set(pVBInfo->P3c4, 0x23, XGI330_SR23);
	xgifb_reg_set(pVBInfo->P3c4, 0x24, XGI330_SR24);
	xgifb_reg_set(pVBInfo->P3c4, 0x25, 0);

	if (HwDeviceExtension->jChipType < XG20) {
		/* Set VB */
		XGI_UnLockCRT2(pVBInfo);
		/* disable VideoCapture */
		xgifb_reg_and_or(pVBInfo->Part0Port, 0x3F, 0xEF, 0x00);
		xgifb_reg_set(pVBInfo->Part1Port, 0x00, 0x00);
		/* chk if BCLK>=100MHz */
		temp1 = xgifb_reg_get(pVBInfo->P3d4, 0x7B);

		xgifb_reg_set(pVBInfo->Part1Port,
			      0x02, XGI330_CRT2Data_1_2);

		xgifb_reg_set(pVBInfo->Part1Port, 0x2E, 0x08); /* use VB */
	} /* != XG20 */

	xgifb_reg_set(pVBInfo->P3c4, 0x27, 0x1F);

	if ((HwDeviceExtension->jChipType == XG42) &&
	    XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo) != 0) {
		/* Not DDR */
		xgifb_reg_set(pVBInfo->P3c4,
			      0x31,
			      (XGI330_SR31 & 0x3F) | 0x40);
		xgifb_reg_set(pVBInfo->P3c4,
			      0x32,
			      (XGI330_SR32 & 0xFC) | 0x01);
	} else {
		xgifb_reg_set(pVBInfo->P3c4, 0x31, XGI330_SR31);
		xgifb_reg_set(pVBInfo->P3c4, 0x32, XGI330_SR32);
	}
	xgifb_reg_set(pVBInfo->P3c4, 0x33, XGI330_SR33);

	if (HwDeviceExtension->jChipType < XG20) {
		if (xgifb_bridge_is_on(pVBInfo)) {
			xgifb_reg_set(pVBInfo->Part2Port, 0x00, 0x1C);
			xgifb_reg_set(pVBInfo->Part4Port,
				      0x0D, XGI330_CRT2Data_4_D);
			xgifb_reg_set(pVBInfo->Part4Port,
				      0x0E, XGI330_CRT2Data_4_E);
			xgifb_reg_set(pVBInfo->Part4Port,
				      0x10, XGI330_CRT2Data_4_10);
			xgifb_reg_set(pVBInfo->Part4Port, 0x0F, 0x3F);
			XGI_LockCRT2(pVBInfo);
		}
	} /* != XG20 */

	XGI_SenseCRT1(pVBInfo);

	if (HwDeviceExtension->jChipType == XG21) {
		xgifb_reg_and_or(pVBInfo->P3d4,
				 0x32,
				 ~Monitor1Sense,
				 Monitor1Sense); /* Z9 default has CRT */
		temp = GetXG21FPBits(pVBInfo);
		xgifb_reg_and_or(pVBInfo->P3d4, 0x37, ~0x01, temp);
	}
	if (HwDeviceExtension->jChipType == XG27) {
		xgifb_reg_and_or(pVBInfo->P3d4,
				 0x32,
				 ~Monitor1Sense,
				 Monitor1Sense); /* Z9 default has CRT */
		temp = GetXG27FPBits(pVBInfo);
		xgifb_reg_and_or(pVBInfo->P3d4, 0x37, ~0x03, temp);
	}

	pVBInfo->ram_type = XGINew_GetXG20DRAMType(HwDeviceExtension, pVBInfo);

	XGINew_SetDRAMDefaultRegister340(HwDeviceExtension,
					 pVBInfo->P3d4,
					 pVBInfo);

	XGINew_SetDRAMSize_340(xgifb_info, HwDeviceExtension, pVBInfo);

	xgifb_reg_set(pVBInfo->P3c4, 0x22, 0xfa);
	xgifb_reg_set(pVBInfo->P3c4, 0x21, 0xa3);

	XGINew_ChkSenseStatus(pVBInfo);
	XGINew_SetModeScratch(pVBInfo);

	xgifb_reg_set(pVBInfo->P3d4, 0x8c, 0x87);

	return 1;
} /* end of init */
