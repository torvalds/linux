/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GM7122_TVE_H__
#define __GM7122_TVE_H__

#include<linux/regmap.h>
#include<linux/io.h>

#define BURST_START			(0x28)
	#define M_D5_BS0		(1 << 5)
	#define M_D4_BS0		(1 << 4)
	#define M_D3_BS0		(1 << 3)
	#define M_D2_BS0		(1 << 2)
	#define M_D1_BS0		(1 << 1)
	#define M_D0_BS0		(1 << 0)

	#define V_D0_BS5(x)		((x & 1) << 5)
	#define V_D0_BS4(x)		((x & 1) << 4)
	#define V_D0_BS3(x)		((x & 1) << 3)
	#define V_D0_BS2(x)		((x & 1) << 2)
	#define V_D0_BS1(x)		((x & 1) << 1)
	#define V_D0_BS0(x)		((x & 1) << 0)

#define BURST_END			(0x29)
	#define M_D5_BE0		(1 << 5)
	#define M_D4_BE0		(1 << 4)
	#define M_D3_BE0		(1 << 3)
	#define M_D2_BE0		(1 << 2)
	#define M_D1_BE0		(1 << 1)
	#define M_D0_BE0		(1 << 0)

	#define V_D0_BE5(x)		((x & 1) << 5)
	#define V_D0_BE4(x)		((x & 1) << 4)
	#define V_D0_BE3(x)		((x & 1) << 3)
	#define V_D0_BE2(x)		((x & 1) << 2)
	#define V_D0_BE1(x)		((x & 1) << 1)
	#define V_D0_BE0(x)		((x & 1) << 0)

#define DA_MODE_CTL			(0x2F)
	#define M_DA7			(1 << 7)
	#define M_DA3			(1 << 3)
	#define M_DA2			(1 << 2)

	#define V_DA7(x)		((x & 1) << 7)
	#define V_DA3(x)		((x & 1) << 3)
	#define V_DA2(x)		((x & 1) << 2)

#define INPUT_PORT_CTL			(0x3A)
	#define M_CBENB			(1 << 7)
	#define M_SYMP			(1 << 4)
	#define M_Y2C			(1 << 1)
	#define M_UV2C			(1 << 0)

	#define V_CBENB(x)		((x & 1) << 7)
	#define V_SYMP(x)		((x & 1) << 4)
	#define V_Y2C(x)		((x & 1) << 1)
	#define V_UV2C(x)		((x & 1) << 0)

#define COLOR_DIFF_CTL			(0x5A)
	#define M_CHPS7			(1 << 7)
	#define M_CHPS6			(1 << 6)
	#define M_CHPS5			(1 << 5)
	#define M_CHPS4			(1 << 4)
	#define M_CHPS3			(1 << 3)
	#define M_CHPS2			(1 << 2)
	#define M_CHPS1			(1 << 1)
	#define M_CHPS0			(1 << 0)

	#define V_CHPS7(x)		((x & 1) << 7)
	#define V_CHPS6(x)		((x & 1) << 6)
	#define V_CHPS5(x)		((x & 1) << 5)
	#define V_CHPS4(x)		((x & 1) << 4)
	#define V_CHPS3(x)		((x & 1) << 3)
	#define V_CHPS2(x)		((x & 1) << 2)
	#define V_CHPS1(x)		((x & 1) << 1)
	#define V_CHPS0(x)		((x & 1) << 0)

#define U_GAIN_CTL			(0x5B)
	#define M_GAINU7		(1 << 7)
	#define M_GAINU6		(1 << 6)
	#define M_GAINU5		(1 << 5)
	#define M_GAINU4		(1 << 4)
	#define M_GAINU3		(1 << 3)
	#define M_GAINU2		(1 << 2)
	#define M_GAINU1		(1 << 1)
	#define M_GAINU0		(1 << 0)

	#define V_GAINU7(x)		((x & 1) << 7)
	#define V_GAINU6(x)		((x & 1) << 6)
	#define V_GAINU5(x)		((x & 1) << 5)
	#define V_GAINU4(x)		((x & 1) << 4)
	#define V_GAINU3(x)		((x & 1) << 3)
	#define V_GAINU2(x)		((x & 1) << 2)
	#define V_GAINU1(x)		((x & 1) << 1)
	#define V_GAINU0(x)		((x & 1) << 0)

#define V_GAIN_CTL			(0x5C)
	#define M_GAINV7		(1 << 7)
	#define M_GAINV6		(1 << 6)
	#define M_GAINV5		(1 << 5)
	#define M_GAINV4		(1 << 4)
	#define M_GAINV3		(1 << 3)
	#define M_GAINV2		(1 << 2)
	#define M_GAINV1		(1 << 1)
	#define M_GAINV0		(1 << 0)

	#define V_GAINV7(x)		((x & 1) << 7)
	#define V_GAINV6(x)		((x & 1) << 6)
	#define V_GAINV5(x)		((x & 1) << 5)
	#define V_GAINV4(x)		((x & 1) << 4)
	#define V_GAINV3(x)		((x & 1) << 3)
	#define V_GAINV2(x)		((x & 1) << 2)
	#define V_GAINV1(x)		((x & 1) << 1)
	#define V_GAINV0(x)		((x & 1) << 0)

#define UMSB_BLACK_GAIN			(0x5D)
	#define M_GAINU8		(1 << 7)
	#define M_BLACK5		(1 << 5)
	#define M_BLACK4		(1 << 4)
	#define M_BLACK3		(1 << 3)
	#define M_BLACK2		(1 << 2)
	#define M_BLACK1		(1 << 1)
	#define M_BLACK0		(1 << 0)

	#define V_GAINU8(x)		((x & 1) << 7)
	#define V_BLACK5(x)		((x & 1) << 5)
	#define V_BLACK4(x)		((x & 1) << 4)
	#define V_BLACK3(x)		((x & 1) << 3)
	#define V_BLACK2(x)		((x & 1) << 2)
	#define V_BLACK1(x)		((x & 1) << 1)
	#define V_BLACK0(x)		((x & 1) << 0)

#define VMSB_BLNNL_GAIN			(0x5E)
	#define M_GAINV8		(1 << 7)
	#define M_BLNNL5		(1 << 5)
	#define M_BLNNL4		(1 << 4)
	#define M_BLNNL3		(1 << 3)
	#define M_BLNNL2		(1 << 2)
	#define M_BLNNL1		(1 << 1)
	#define M_BLNNL0		(1 << 0)

	#define V_GAINV8(x)		((x & 1) << 7)
	#define V_BLNNL5(x)		((x & 1) << 5)
	#define V_BLNNL4(x)		((x & 1) << 4)
	#define V_BLNNL3(x)		((x & 1) << 3)
	#define V_BLNNL2(x)		((x & 1) << 2)
	#define V_BLNNL1(x)		((x & 1) << 1)
	#define V_BLNNL0(x)		((x & 1) << 0)

#define STANDARD_CTL			(0x61)
	#define M_SCBW			(1 << 2)
	#define M_PAL			(1 << 1)
	#define M_BIT0			(1 << 0)

	#define V_SCBW(x)		((x & 1) << 2)
	#define V_PAL(x)		((x & 1) << 1)
	#define V_BIT0(x)		((x & 1) << 0)

#define RTCEN_BURST_CTL			(0x62)
	#define M_RTCEN			(1 << 7)
	#define M_BSTA6			(1 << 6)
	#define M_BSTA5			(1 << 5)
	#define M_BSTA4			(1 << 4)
	#define M_BSTA3			(1 << 3)
	#define M_BSTA2			(1 << 2)
	#define M_BSTA1			(1 << 1)
	#define M_BSTA0			(1 << 0)

	#define V_RTCEN(x)		((x & 1) << 7)
	#define V_BSTA6(x)		((x & 1) << 6)
	#define V_BSTA5(x)		((x & 1) << 5)
	#define V_BSTA4(x)		((x & 1) << 4)
	#define V_BSTA3(x)		((x & 1) << 3)
	#define V_BSTA2(x)		((x & 1) << 2)
	#define V_BSTA1(x)		((x & 1) << 1)
	#define V_BSTA0(x)		((x & 1) << 0)

#define SUBCARRIER0			(0x63)
	#define M_FSC07			(1 << 7)
	#define M_FSC06			(1 << 6)
	#define M_FSC05			(1 << 5)
	#define M_FSC04			(1 << 4)
	#define M_FSC03			(1 << 3)
	#define M_FSC02			(1 << 2)
	#define M_FSC01			(1 << 1)
	#define M_FSC00			(1 << 0)

	#define V_FSC07(x)		((x & 1) << 7)
	#define V_FSC06(x)		((x & 1) << 6)
	#define V_FSC05(x)		((x & 1) << 5)
	#define V_FSC04(x)		((x & 1) << 4)
	#define V_FSC03(x)		((x & 1) << 3)
	#define V_FSC02(x)		((x & 1) << 2)
	#define V_FSC01(x)		((x & 1) << 1)
	#define V_FSC00(x)		((x & 1) << 0)

#define SUBCARRIER1			(0x64)
	#define M_FSC15			(1 << 7)
	#define M_FSC14			(1 << 6)
	#define M_FSC13			(1 << 5)
	#define M_FSC12			(1 << 4)
	#define M_FSC11			(1 << 3)
	#define M_FSC10			(1 << 2)
	#define M_FSC09			(1 << 1)
	#define M_FSC08			(1 << 0)

	#define V_FSC15(x)		((x & 1) << 7)
	#define V_FSC14(x)		((x & 1) << 6)
	#define V_FSC13(x)		((x & 1) << 5)
	#define V_FSC12(x)		((x & 1) << 4)
	#define V_FSC11(x)		((x & 1) << 3)
	#define V_FSC10(x)		((x & 1) << 2)
	#define V_FSC09(x)		((x & 1) << 1)
	#define V_FSC08(x)		((x & 1) << 0)

#define SUBCARRIER2			(0x65)
	#define M_FSC23			(1 << 7)
	#define M_FSC22			(1 << 6)
	#define M_FSC21			(1 << 5)
	#define M_FSC20			(1 << 4)
	#define M_FSC19			(1 << 3)
	#define M_FSC18			(1 << 2)
	#define M_FSC17			(1 << 1)
	#define M_FSC16			(1 << 0)

	#define V_FSC23(x)		((x & 1) << 7)
	#define V_FSC22(x)		((x & 1) << 6)
	#define V_FSC21(x)		((x & 1) << 5)
	#define V_FSC20(x)		((x & 1) << 4)
	#define V_FSC19(x)		((x & 1) << 3)
	#define V_FSC18(x)		((x & 1) << 2)
	#define V_FSC17(x)		((x & 1) << 1)
	#define V_FSC16(x)		((x & 1) << 0)

#define SUBCARRIER3			(0x66)
	#define M_FSC31			(1 << 7)
	#define M_FSC30			(1 << 6)
	#define M_FSC29			(1 << 5)
	#define M_FSC28			(1 << 4)
	#define M_FSC27			(1 << 3)
	#define M_FSC26			(1 << 2)
	#define M_FSC25			(1 << 1)
	#define M_FSC24			(1 << 0)

	#define V_FSC31(x)		((x & 1) << 7)
	#define V_FSC30(x)		((x & 1) << 6)
	#define V_FSC29(x)		((x & 1) << 5)
	#define V_FSC28(x)		((x & 1) << 4)
	#define V_FSC27(x)		((x & 1) << 3)
	#define V_FSC26(x)		((x & 1) << 2)
	#define V_FSC25(x)		((x & 1) << 1)
	#define V_FSC24(x)		((x & 1) << 0)

#define RCV_PORT_CTL			(0x6B)
	#define M_ORCV1			(1 << 4)
	#define M_PRCV1			(1 << 3)
	#define M_ORCV2			(1 << 1)
	#define M_PRCV2			(1 << 0)

	#define V_ORCV1(x)		((x & 1) << 4)
	#define V_PRCV1(x)		((x & 1) << 3)
	#define V_ORCV2(x)		((x & 1) << 1)
	#define V_PRCV2(x)		((x & 1) << 0)


#define TRIG0_CTL			(0x6C)
	#define M_HTRIG7		(1 << 7)
	#define M_HTRIG6		(1 << 6)
	#define M_HTRIG5		(1 << 5)
	#define M_HTRIG4		(1 << 4)
	#define M_HTRIG3		(1 << 3)
	#define M_HTRIG2		(1 << 2)
	#define M_HTRIG1		(1 << 1)
	#define M_HTRIG0		(1 << 0)

	#define V_HTRIG7(x)		((x & 1) << 7)
	#define V_HTRIG6(x)		((x & 1) << 6)
	#define V_HTRIG5(x)		((x & 1) << 5)
	#define V_HTRIG4(x)		((x & 1) << 4)
	#define V_HTRIG3(x)		((x & 1) << 3)
	#define V_HTRIG2(x)		((x & 1) << 2)
	#define V_HTRIG1(x)		((x & 1) << 1)
	#define V_HTRIG0(x)		((x & 1) << 0)

#define TRIG1_CTL			(0x6D)
	#define M_HTRIG10		(1 << 7)
	#define M_HTRIG9		(1 << 6)
	#define M_HTRIG8		(1 << 5)
	#define M_VTRIG4		(1 << 4)
	#define M_VTRIG3		(1 << 3)
	#define M_VTRIG2		(1 << 2)
	#define M_VTRIG1		(1 << 1)
	#define M_VTRIG0		(1 << 0)

	#define V_HTRIG10(x)		((x & 1) << 7)
	#define V_HTRIG9(x)		((x & 1) << 6)
	#define V_HTRIG8(x)		((x & 1) << 5)
	#define V_VTRIG4(x)		((x & 1) << 4)
	#define V_VTRIG3(x)		((x & 1) << 3)
	#define V_VTRIG2(x)		((x & 1) << 2)
	#define V_VTRIG1(x)		((x & 1) << 1)
	#define V_VTRIG0(x)		((x & 1) << 0)

#define TRIG2_CTL			(0x75)
	#define M_VTRIG8		(1 << 7)
	#define M_VTRIG7		(1 << 6)
	#define M_VTRIG6		(1 << 5)
	#define M_VTRIG5		(1 << 4)

	#define V_VTRIG8(x)		((x & 1) << 7)
	#define V_VTRIG7(x)		((x & 1) << 6)
	#define V_VTRIG6(x)		((x & 1) << 5)
	#define V_VTRIG5(x)		((x & 1) << 4)

enum {
	TVOUT_CVBS_NTSC = 0,
	TVOUT_CVBS_PAL,
};

enum {
	INPUT_FORMAT_RGB = 0,
	INPUT_FORMAT_YUV,
	INPUT_FORMAT_CCIR656
};

enum {
	SOC_RK1000 = 0,
	SOC_GM7122
};

#define TVOUT_DEAULT TVOUT_CVBS_PAL

struct ioctrl {
	int gpio;
	int active;
};

struct gm7122_tve {
	struct device			*dev;
	u32				reg_phy_base;
	u32				len;
	unsigned int			lcdcid;
	unsigned int			property;
	struct rk_display_device	*ddev;
	unsigned int			enable;
	unsigned int			suspend;
	struct fb_videomode		*mode;
	struct list_head		modelist;
	struct rk_screen		screen;
	struct i2c_client		*client;
	struct ioctrl			io_reset;
	struct ioctrl			io_sleep;
};

#define GM7122_I2C_RATE	(100*1000)

#endif
