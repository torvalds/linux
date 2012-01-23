#include "de_bsp_i.h"
#include "de_be.h"
#include "de_fe.h"

volatile __image_reg_t * image1_reg;

__u32 image0_reg_base = 0;//DISE_REGS_BASE;

__u32  csc_tab[192] =
{
    //bt601
    0x04a7,0x1e6f,0x1cbf,0x0877,0x04a7,0x0000,0x0662,0x3211,0x04a7,0x0812,0x0000,0x2eb1,//yuv2rgb
    0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,//yuv2yuv
    0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,//rgb2rgb
    0x0204,0x0107,0x0064,0x0100,0x1ed6,0x1f68,0x01c1,0x0800,0x1e87,0x01c1,0x1fb7,0x0800,//rgb2yuv

    //bt709
    0x04a7,0x1f25,0x1ddd,0x04cf,0x04a7,0x0000,0x072c,0x307d,0x04a7,0x0875,0x0000,0x2dea,//yuv2rgb
    0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,//yuv2yuv
    0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,//rgb2rgb
    0x0274,0x00bb,0x003f,0x0100,0x1ea5,0x1f98,0x01c1,0x0800,0x1e67,0x01c1,0x1fd7,0x0800,//rgb2yuv

    //DISP_YCC
    0x0400,0x1e9e,0x1d24,0x087b,0x0400,0x0000,0x059b,0x34c8,0x0400,0x0715,0x0000,0x31d4,//yuv2rgb
    0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,//yuv2yuv
    0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,//rgb2rgb
    0x0258,0x0132,0x0075,0x0000,0x1eac,0x1f53,0x0200,0x0800,0x1e53,0x0200,0x1fac,0x0800,//rgb2yuv

    //xvYCC
    0x04a7,0x1f25,0x1ddd,0x04cf,0x04a7,0x0000,0x072c,0x307d,0x04a7,0x0875,0x0000,0x2dea,//yuv2rgb
    0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,//yuv2yuv
    0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,0x0000,0x0000,0x0000,0x0400,0x0000,//rgb2rgb
    0x0274,0x00bb,0x003f,0x0100,0x1ea5,0x1f98,0x01c1,0x0800,0x1e67,0x01c1,0x1fd7,0x0800 //rgb2yuv
};

__u32  image_enhance_tab[224] =
{
    //csc convert table
    0x00000107,0x00000204,0x00000064,0x00004000,0xffffff69,0xfffffed7,0x000001c1,0x00020000,
    0x000001c1,0xfffffe88,0xffffffb8,0x00020000,0x00000000,0x00000000,0x00000000,0x00000400,
    0x000004a7,0x00000000,0x00000662,0xfffc845b,0x000004a7,0xfffffe70,0xfffffcc0,0x00021df3,
    0x000004a7,0x00000812,0x00000000,0xfffbac4a,0x00000000,0x00000000,0x00000000,0x00000400,

    0x000000bb,0x00000274,0x0000003f,0x00004000,0xffffff99,0xfffffea6,0x000001c1,0x00020000,
    0x000001c1,0xfffffe68,0xffffffd8,0x00020000,0x00000000,0x00000000,0x00000000,0x00000400,
    0x000004a7,0x00000000,0x0000072c,0xfffc1f7d,0x000004a7,0xffffff26,0xfffffdde,0x000133f7,
    0x000004a7,0x00000875,0x00000000,0xfffb7aa0,0x00000000,0x00000000,0x00000000,0x00000400,

    0x00000132,0x00000258,0x00000075,0x00000000,0xffffff54,0xfffffead,0x00000200,0x00020000,
    0x00000200,0xfffffe54,0xffffffad,0x00020000,0x00000000,0x00000000,0x00000000,0x00000400,
    0x00000400,0x00000000,0x0000059b,0xfffd3213,0x00000400,0xfffffe9f,0xfffffd25,0x00021ec5,
    0x00000400,0x00000715,0x00000000,0xfffc7540,0x00000000,0x00000000,0x00000000,0x00000400,
    //sin table
    0xffffffbd,0xffffffbf,0xffffffc1,0xffffffc2,0xffffffc4,0xffffffc6,0xffffffc8,0xffffffca,
    0xffffffcc,0xffffffce,0xffffffd1,0xffffffd3,0xffffffd5,0xffffffd7,0xffffffd9,0xffffffdb,
    0xffffffdd,0xffffffdf,0xffffffe2,0xffffffe4,0xffffffe6,0xffffffe8,0xffffffea,0xffffffec,
    0xffffffef,0xfffffff1,0xfffffff3,0xfffffff5,0xfffffff8,0xfffffffa,0xfffffffc,0xfffffffe,
    0x00000000,0x00000002,0x00000004,0x00000006,0x00000008,0x0000000b,0x0000000d,0x0000000f,
    0x00000011,0x00000014,0x00000016,0x00000018,0x0000001a,0x0000001c,0x0000001e,0x00000021,
    0x00000023,0x00000025,0x00000027,0x00000029,0x0000002b,0x0000002d,0x0000002f,0x00000032,
    0x00000034,0x00000036,0x00000038,0x0000003a,0x0000003c,0x0000003e,0x0000003f,0x00000041,
    //cos table
    0x0000006c,0x0000006d,0x0000006e,0x0000006f,0x00000071,0x00000072,0x00000073,0x00000074,
    0x00000074,0x00000075,0x00000076,0x00000077,0x00000078,0x00000079,0x00000079,0x0000007a,
    0x0000007b,0x0000007b,0x0000007c,0x0000007c,0x0000007d,0x0000007d,0x0000007e,0x0000007e,
    0x0000007e,0x0000007f,0x0000007f,0x0000007f,0x0000007f,0x0000007f,0x0000007f,0x0000007f,
    0x00000080,0x0000007f,0x0000007f,0x0000007f,0x0000007f,0x0000007f,0x0000007f,0x0000007f,
    0x0000007e,0x0000007e,0x0000007e,0x0000007d,0x0000007d,0x0000007c,0x0000007c,0x0000007b,
    0x0000007b,0x0000007a,0x00000079,0x00000079,0x00000078,0x00000077,0x00000076,0x00000075,
    0x00000074,0x00000074,0x00000073,0x00000072,0x00000071,0x0000006f,0x0000006e,0x0000006d
};

__u32  fir_tab[672] =
{
    0x00004000,0x000140ff,0x00033ffe,0x00043ffd,0x00063efc,0xff083dfc,0x000a3bfb,0xff0d39fb,//h_ch0
    0xff0f37fb,0xff1136fa,0xfe1433fb,0xfe1631fb,0xfd192ffb,0xfd1c2cfb,0xfd1f29fb,0xfc2127fc,
    0xfc2424fc,0xfc2721fc,0xfb291ffd,0xfb2c1cfd,0xfb2f19fd,0xfb3116fe,0xfb3314fe,0xfa3611ff,
    0xfb370fff,0xfb390dff,0xfb3b0a00,0xfc3d08ff,0xfc3e0600,0xfd3f0400,0xfe3f0300,0xff400100,
    0x00004000,0x000140ff,0x00033ffe,0x00043ffd,0x00063efc,0xff083dfc,0x000a3bfb,0xff0d39fb,//h_ch1&ch2
    0xff0f37fb,0xff1136fa,0xfe1433fb,0xfe1631fb,0xfd192ffb,0xfd1c2cfb,0xfd1f29fb,0xfc2127fc,
    0xfc2424fc,0xfc2721fc,0xfb291ffd,0xfb2c1cfd,0xfb2f19fd,0xfb3116fe,0xfb3314fe,0xfa3611ff,
    0xfb370fff,0xfb390dff,0xfb3b0a00,0xfc3d08ff,0xfc3e0600,0xfd3f0400,0xfe3f0300,0xff400100,
    0x00053704,0x00063703,0x00073702,0x00093601,0x000b3500,0x000c3400,0x000e3200,0x000f3100,//v_ch0
    0x00112f00,0x00132d00,0x00152b00,0x00162a00,0x00182800,0x001a2600,0x001c2400,0x001e2200,
    0x00202000,0x00211f00,0x00231d00,0x00251b00,0x00271900,0x00291700,0x002a1600,0x002c1400,
    0x002e1200,0x00301000,0x00310f00,0x00330d00,0x00340c00,0x01350a00,0x02360800,0x03360700,
    0x00083008,0x00093007,0x000b3005,0x000d2f04,0x000e2f03,0x00102e02,0x00112e01,0x00132d00,//v_ch1&ch2
    0x00142c00,0x00152b00,0x00172900,0x00182800,0x001a2600,0x001b2500,0x001d2300,0x001e2200,
    0x00202000,0x00211f00,0x00221e00,0x00241c00,0x00251b00,0x00271900,0x00281800,0x002a1600,
    0x002b1500,0x002d1300,0x012d1200,0x022e1000,0x032e0f00,0x042f0d00,0x052f0c00,0x072f0a00,

    0x000b2a0b,0x000d2a09,0x000e2a08,0x000f2a07,0x00102a06,0x00122905,0x00132904,0x00142903,//h_ch0
    0x00162802,0x00172702,0x00182701,0x001a2600,0x001b2500,0x001c2400,0x001d2300,0x001e2200,
    0x00202000,0x00211f00,0x00221e00,0x00231d00,0x00241c00,0x00261a00,0x01261900,0x02271700,
    0x02281600,0x03281500,0x04291300,0x05291200,0x06291100,0x072a0f00,0x082a0e00,0x092a0d00,
    0x000d270c,0x000f260b,0x0010260a,0x00112609,0x00122608,0x00122608,0x00132607,0x00152506,//h_ch1&ch2
    0x00162505,0x00172504,0x00192403,0x00192403,0x001b2302,0x001d2201,0x001d2201,0x001f2100,
    0x00202000,0x00211f00,0x01211e00,0x01221d00,0x02221c00,0x03231a00,0x03241900,0x04241800,
    0x05241700,0x06251500,0x07251400,0x08251300,0x08261200,0x09261100,0x0a261000,0x0b260f00,
    0x000e240e,0x000f240d,0x0010240c,0x0012230b,0x0013230a,0x0013230a,0x00142309,0x00152308,//v_ch0
    0x00162307,0x00182206,0x00182206,0x00192205,0x001b2104,0x001c2103,0x001d2003,0x011e1f02,
    0x021e1e02,0x021f1e01,0x03201d00,0x03211c00,0x04211b00,0x05211a00,0x06211900,0x06221800,
    0x07221700,0x08221600,0x09221500,0x0a221400,0x0a231300,0x0b231200,0x0c231100,0x0d231000,
    0x0010210f,0x0011210e,0x0012210d,0x0012210d,0x0013210c,0x0014210b,0x0015210a,0x0015210a,//v_ch1&ch2
    0x00162109,0x00182008,0x00182008,0x01191f07,0x011a1f06,0x021b1e05,0x021b1e05,0x031c1d04,
    0x031d1d03,0x041d1c03,0x051e1b02,0x051e1b02,0x061f1a01,0x071f1901,0x07201801,0x08201800,
    0x09201700,0x0a201600,0x0a211500,0x0b211400,0x0c211300,0x0d201300,0x0d211200,0x0e211100,

    0x00102010,0x0011200f,0x0012200e,0x0012200e,0x0013200d,0x00151f0c,0x00151f0c,0x01151f0b,//h_ch0
    0x01161e0b,0x01171e0a,0x02171e09,0x02181d09,0x03191d07,0x03191c08,0x041a1c06,0x041a1c06,
    0x051b1b05,0x061b1b04,0x061c1a04,0x071c1904,0x071d1903,0x081d1803,0x091d1802,0x091e1702,
    0x0a1e1602,0x0b1e1601,0x0c1f1500,0x0c1f1500,0x0d1f1400,0x0e1f1300,0x0e201200,0x0f1f1200,
    0x00111e11,0x00121e10,0x00131e0f,0x00131e0f,0x01131e0e,0x01141d0e,0x02151d0c,0x02151d0c,//h_ch1&ch2
    0x02161d0b,0x03161c0b,0x03171c0a,0x04171c09,0x04181b09,0x05181b08,0x05191b07,0x06191a07,
    0x061a1a06,0x071a1906,0x071b1905,0x081b1805,0x091b1804,0x091c1704,0x0a1c1703,0x0a1c1604,
    0x0b1d1602,0x0c1d1502,0x0c1d1502,0x0d1d1402,0x0e1d1401,0x0e1e1301,0x0f1e1300,0x101e1200,
    0x02121b11,0x02121b11,0x02131b10,0x03131b0f,0x03131b0f,0x04141a0e,0x04141a0e,0x04151a0d,//v_ch0
    0x05151a0c,0x05151a0c,0x0616190b,0x0616190b,0x0616190b,0x0716190a,0x0717180a,0x08171809,
    0x08181808,0x09181708,0x09181708,0x0a181707,0x0a191607,0x0b191606,0x0b191606,0x0c1a1505,
    0x0c1a1505,0x0d1a1504,0x0d1a1405,0x0e1a1404,0x0f1a1403,0x0f1b1303,0x101b1302,0x101b1203,
    0x04121911,0x04121911,0x05121910,0x05121910,0x0513190f,0x0613180f,0x0614180e,0x0614180e,//v_ch1&ch2
    0x0714180d,0x0714180d,0x0715180c,0x0815170c,0x0815170c,0x0915170b,0x0915170b,0x0916160b,
    0x0a16160a,0x0a16160a,0x0b161609,0x0b161609,0x0b171509,0x0c171508,0x0c181507,0x0d171507,
    0x0d181407,0x0e181406,0x0e181406,0x0f181306,0x0f191305,0x10181305,0x10181305,0x10191205,

    0x06111811,0x06121711,0x06121711,0x06131710,0x0713170f,0x0713170f,0x0713170f,0x0813170e,//h_ch0
    0x0813170e,0x0814160e,0x0914160d,0x0914160d,0x0914160d,0x0a14160c,0x0a14160c,0x0a15150c,
    0x0b15150b,0x0b15150b,0x0c15150a,0x0c15150a,0x0c16140a,0x0d161409,0x0d161409,0x0d161409,
    0x0e161408,0x0e171308,0x0f171307,0x0f171307,0x0f171307,0x10171306,0x10171207,0x11171206,
    0x07121611,0x07121611,0x08121610,0x08121610,0x0813160f,0x0813160f,0x0912160f,0x0913160e,//h_ch1&ch2
    0x0913150f,0x0a13150e,0x0a14150d,0x0a14150d,0x0a14150d,0x0b13150d,0x0b14150c,0x0b14150c,
    0x0c14140c,0x0c15140b,0x0c15140b,0x0d14140b,0x0d15140a,0x0d15140a,0x0d15140a,0x0e15130a,
    0x0e15130a,0x0e161309,0x0f151309,0x0f161308,0x0f161308,0x10161208,0x10161208,0x10161208,
    0x0b111410,0x0b111410,0x0b111410,0x0b111410,0x0b121310,0x0b121310,0x0c12130f,0x0c12130f,//v_ch0
    0x0c12130f,0x0c12130f,0x0c12130f,0x0c12130f,0x0d12130e,0x0d12130e,0x0d12130e,0x0d12130e,
    0x0d13130d,0x0e12130d,0x0e13120d,0x0e13120d,0x0e13120d,0x0e13120d,0x0e13120d,0x0f13120c,
    0x0f13120c,0x0f13120c,0x0f13120c,0x0f13120c,0x1013120b,0x1013120b,0x1013120b,0x1014110b,
    0x0c111310,0x0c111211,0x0d111210,0x0d111210,0x0d111210,0x0d111210,0x0d111210,0x0d111210,//v_ch1&ch2
    0x0d111210,0x0d12120f,0x0d12120f,0x0e11120f,0x0e11120f,0x0e11120f,0x0e12120e,0x0e12120e,
    0x0e12120e,0x0e12120e,0x0e12120e,0x0f11120e,0x0f11120e,0x0f11120e,0x0f12120d,0x0f12120d,
    0x0f12110e,0x0f12110e,0x0f12110e,0x0f12110e,0x1012110d,0x1012110d,0x1012110d,0x1012110d,

    0x00400000,0x023e0000,0x043c0000,0x063a0000,0x08380000,0x0a360000,0x0c340000,0x0e320000,//h_ch0
    0x10300000,0x122e0000,0x142c0000,0x162a0000,0x18280000,0x1a260000,0x1c240000,0x1e220000,
    0x20200000,0x221e0000,0x241c0000,0x261a0000,0x28180000,0x2a160000,0x2c140000,0x2e120000,
    0x30100000,0x320e0000,0x340c0000,0x360a0000,0x38080000,0x3a060000,0x3c040000,0x3e020000,
    0x152b0000,0x162a0000,0x17290000,0x17290000,0x18280000,0x19270000,0x19270000,0x1a260000,//h_ch1&ch2
    0x1b250000,0x1b250000,0x1c240000,0x1d230000,0x1d230000,0x1e220000,0x1f210000,0x1f210000,
    0x20200000,0x211f0000,0x211f0000,0x221e0000,0x231d0000,0x231d0000,0x241c0000,0x251b0000,
    0x251b0000,0x261a0000,0x27190000,0x27190000,0x28180000,0x29170000,0x29170000,0x2a160000,
    0x1a260000,0x1a260000,0x1a260000,0x1b250000,0x1b250000,0x1c240000,0x1c240000,0x1c240000,//v_ch0
    0x1d230000,0x1d230000,0x1e220000,0x1e220000,0x1e220000,0x1f210000,0x1f210000,0x20200000,
    0x20200000,0x20200000,0x211f0000,0x211f0000,0x221e0000,0x221e0000,0x221e0000,0x231d0000,
    0x231d0000,0x241c0000,0x241c0000,0x241c0000,0x251b0000,0x251b0000,0x261a0000,0x261a0000,
    0x1b250000,0x1c240000,0x1c240000,0x1c240000,0x1d230000,0x1d230000,0x1d230000,0x1d230000,//v_ch1&ch2
    0x1e220000,0x1e220000,0x1e220000,0x1f210000,0x1f210000,0x1f210000,0x1f210000,0x20200000,
    0x20200000,0x20200000,0x211f0000,0x211f0000,0x211f0000,0x211f0000,0x221e0000,0x221e0000,
    0x221e0000,0x231d0000,0x231d0000,0x231d0000,0x231d0000,0x241c0000,0x241c0000,0x241c0000,

    0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,
    0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,
    0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,
    0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000,0x40000000
};

__s32 DE_Set_Reg_Base(__u32 sel, __u32 address)
{
    if(sel == 0)
    {

	    image0_reg_base = address;
	   // memset((void*)(image0_reg_base+0x800), 0,0x1000-0x800);

	}
	else
	{

	    image1_reg =(__image_reg_t *)address;
	   // memset((void*)image1_reg, 0,0x58);

	}


	return 0;
}

__u32 DE_Get_Reg_Base(__u32 sel)
{
    if(sel == 0)
    {
	    return image0_reg_base;
	}
	else
	{
	    return (__u32)(image1_reg);;
	}
}

__u32 DE_BE_Reg_Init(__u32 sel)
{
	if(sel == 0)
	{
		memset((void*)(image0_reg_base+0x800), 0,0x1000-0x800);
	}
	else if(sel == 1)
	{
	    memset((void*)image1_reg, 0,0x58);
	}
	return 0;
}

__s32  DE_BE_Set_Internal_Framebuffer(__u32 address,__u32 size)
{
	__u32 *pdest_end;
    __u32 *psrc_cur;
    __u32 *pdest_cur;

    if(size > DE_BE_INTERNAL_FB_SIZE)
    {
        size = DE_BE_INTERNAL_FB_SIZE;
    }

	pdest_cur = (__u32*)(DE_Get_Reg_Base(0)+DE_BE_INTERNAL_FB_ADDR_OFF);
	psrc_cur = (__u32*)address;
	pdest_end = pdest_cur + (size>>2);

    while(pdest_cur < pdest_end)
    {
    	*(volatile __u32 *)pdest_cur++ = *psrc_cur++;
    }

    return 0;
}

__s32 DE_BE_Set_SystemPalette(__u32 * pbuffer, __u32 offset,__u32 size)
{
	__u32 *pdest_end;
    __u32 *psrc_cur;
    __u32 *pdest_cur;

    if(size > DE_BE_PALETTE_TABLE_SIZE)
    {
        size = DE_BE_PALETTE_TABLE_SIZE;
    }

	psrc_cur = pbuffer;
	pdest_cur = (__u32*)(DE_Get_Reg_Base(0)+DE_BE_PALETTE_TABLE_ADDR_OFF  + offset);
	pdest_end = pdest_cur + (size>>2);

    while(pdest_cur < pdest_end)
    {
    	*(volatile __u32 *)pdest_cur++ = *psrc_cur++;
    }

   return 0;
}

__s32 DE_BE_Get_SystemPalette(__u32 *pbuffer, __u32 offset,__u32 size)
{
	__u32 *pdest_end;
    __u32 *psrc_cur;
    __u32 *pdest_cur;

    if(size > DE_BE_PALETTE_TABLE_SIZE)
    {
        size = DE_BE_PALETTE_TABLE_SIZE;
    }

	psrc_cur = (__u32*)(DE_Get_Reg_Base(0)+DE_BE_PALETTE_TABLE_ADDR_OFF + offset);
	pdest_cur = pbuffer;
	pdest_end = pdest_cur + (size>>2);

    while(pdest_cur < pdest_end)
    {
    	*(volatile __u32 *)pdest_cur++ = *psrc_cur++;
    }

    return 0;
}

__s32 DE_BE_Set_Gamma_Table(__u32 address,__u32 size)
{
	__u32 *pdest_end;
    __u32 *psrc_cur;
    __u32 *pdest_cur;

    if(size > DE_BE_GAMMA_TABLE_SIZE)
    {
        size = DE_BE_GAMMA_TABLE_SIZE>>2;
    }
    else
    {
        size = size>>2;
    }
	pdest_cur = (__u32*)(DE_Get_Reg_Base(0)+DE_BE_GAMMA_TABLE_ADDR_OFF);
	psrc_cur = (__u32*)address;
	pdest_end = pdest_cur + (size>>2);

    while(pdest_cur < pdest_end)
    {
    	*(volatile __u32 *)pdest_cur++ = *psrc_cur++;
    }

    return 0;
}

__s32 DE_BE_Enable(__u32 sel)
{
    if(sel == 0)
    {
        DE_BE_WUINT8(DE_BE_MODE_CTL_OFF,DE_BE_RUINT8(DE_BE_MODE_CTL_OFF) | (0x01<<1));//start
        DE_BE_WUINT8(DE_BE_MODE_CTL_OFF,DE_BE_RUINT8(DE_BE_MODE_CTL_OFF) | 0x01);//enable
    }
    else if(sel == 1)
    {
        image1_reg->control.reset_start = 1;
        image1_reg->control.en = 1;
    }
    return 0;
}

__s32 DE_BE_Disable(__u32 sel)
{
    if(sel == 0)
    {
        DE_BE_WUINT8(DE_BE_MODE_CTL_OFF,DE_BE_RUINT8(DE_BE_MODE_CTL_OFF) & 0xfd);//reset
        DE_BE_WUINT8(DE_BE_MODE_CTL_OFF,DE_BE_RUINT8(DE_BE_MODE_CTL_OFF) & 0xfe);//disable
    }
    else if(sel == 1)
    {
        image1_reg->control.reset_start = 0;
        image1_reg->control.en = 0;
    }

    return 0;
}

__s32 DE_BE_Set_BkColor(__u32 sel, __disp_color_t bkcolor)
{
    if(sel == 0)
    {
        DE_BE_WUINT32(DE_BE_COLOR_CTL_OFF, bkcolor.alpha<<24|bkcolor.red<<16|bkcolor.green<<8|bkcolor.blue);
    }
    else if(sel == 1)
    {
        //image1_reg->back_color.red = bkcolor.red;
        //image1_reg->back_color.green = bkcolor.green;
        //image1_reg->back_color.blue = bkcolor.blue;
        *(__u32*)((__u32)image1_reg+0x08) = (bkcolor.alpha<<24|bkcolor.red<<16|bkcolor.green<<8|bkcolor.blue);
    }
    return 0;
}

__s32 DE_BE_Set_ColorKey(__disp_color_t ck_max,__disp_color_t  ck_min,__u32 ck_red_match, __u32 ck_green_match, __u32 ck_blue_match)
{
    DE_BE_WUINT32(DE_BE_CLRKEY_MAX_OFF,ck_max.alpha<<24|
        ck_max.red<<16|ck_max.green<<8|ck_max.blue);
    DE_BE_WUINT32(DE_BE_CLRKEY_MIN_OFF,ck_min.alpha<<24|
        ck_min.red<<16|ck_min.green<<8|ck_min.blue);
    DE_BE_WUINT32(DE_BE_CLRKEY_CFG_OFF,(ck_red_match<<4) | (ck_green_match<<2) | ck_blue_match);
    return 0;
}

__s32 DE_BE_Cfg_Ready(__u32 sel)
{
    if(sel == 0)
    {
        __u32 tmp;

        tmp = DE_BE_RUINT32(DE_BE_FRMBUF_CTL_OFF);
        DE_BE_WUINT32(DE_BE_FRMBUF_CTL_OFF,tmp | (0x1<<1) | 0x1);//bit1:enable, bit0:ready
    }
    else
    {
        image1_reg->control.ready_ctl = 1;
        image1_reg->control.ready_en = 1;
    }
    return 0;
}

static __s32 Sprite_Block_Get_Reg_From_Size(__u32 size)
{
	__u8 tmp = 0;

	size = (size>=8)?size:8;
	while(size!=0)
	{
		size >>= 1;
		tmp++;
	}
	tmp = (tmp<=11)?tmp-4:7;

	return tmp;

}

__s32 DE_BE_Sprite_Enable(__bool enable)
{
	DE_BE_WUINT8(DE_BE_SPRITE_EN_OFF,(DE_BE_RUINT8(DE_BE_SPRITE_EN_OFF)&0xfe) | enable);
	return 0;
}

__s32 DE_BE_Sprite_Disable(void)
{
	DE_BE_WUINT8(DE_BE_SPRITE_EN_OFF,DE_BE_RUINT8(DE_BE_SPRITE_EN_OFF)&0xfe);
	return 0;
}

__s32 DE_BE_Sprite_Double_FB_Switch_Control(__u8 buffer)
{
	DE_BE_WUINT8(DE_BE_SPRITE_EN_OFF,(DE_BE_RUINT8(DE_BE_SPRITE_EN_OFF)&0xfb)&(buffer<<2));
	return 0;
}

__s32 DE_BE_Sprite_Get_Double_FB_Status(void)
{
	return (DE_BE_RUINT8(DE_BE_SPRITE_EN_OFF)&0x04)>>2;
}

__s32 DE_BE_Sprite_Set_Format(__u8 pixel_seq,__u8 format)
{
	DE_BE_WUINT16(DE_BE_SPRITE_FORMAT_CTRL_OFF,(pixel_seq<<12)|(format<<8));
	return 0;
}

__s32 DE_BE_Sprite_Global_Alpha_Enable(__bool enable)
{
	DE_BE_WUINT8(DE_BE_SPRITE_ALPHA_CTRL_OFF,enable);
	return 0;
}

__s32 DE_BE_Sprite_Set_Global_Alpha(__u8 alpha_val)
{
	DE_BE_WUINT8IDX(DE_BE_SPRITE_ALPHA_CTRL_OFF,3,alpha_val);
	return 0;
}

__s32 DE_BE_Sprite_Block_Set_Pos(__u8 blk_idx,__s16 x,__s16 y)
{
	__u32 reg = 0;

	reg = DE_BE_RUINT32IDX(DE_BE_SPRITE_POS_CTRL_OFF,blk_idx) & 0xf000f000;

  	DE_BE_WUINT32IDX(DE_BE_SPRITE_POS_CTRL_OFF,blk_idx,reg | ((y&0xfff)<<16) | (x&0xfff));
   	return 0;
}

__s32 DE_BE_Sprite_Block_Set_Size(__u8 blk_idx,__u32 xsize,__u32 ysize)//todo
{
	__u8 w=0,h=0;
	__u32 tmp = 0;

	tmp = DE_BE_RUINT32IDX(DE_BE_SPRITE_POS_CTRL_OFF,blk_idx) & 0x0fff0fff;
	w = Sprite_Block_Get_Reg_From_Size(xsize);
	h = Sprite_Block_Get_Reg_From_Size(ysize);

	DE_BE_WUINT32IDX(DE_BE_SPRITE_POS_CTRL_OFF,blk_idx,tmp | (h<<28) | (w<<12));
	return 0;
}

__s32 DE_BE_Sprite_Block_Set_fb(__u8 blk_idx,__u32 addr, __u32 line_width)
{
	DE_BE_WUINT32IDX(DE_BE_SPRITE_ADDR_OFF,blk_idx,addr>>2);
	DE_BE_WUINT32IDX(DE_BE_SPRITE_LINE_WIDTH_OFF,blk_idx, line_width<<3);
	return 0;
}

__s32 DE_BE_Sprite_Block_Set_Next_Id(__u8 blk_idx,__u8 next_blk_id)
{
	DE_BE_WUINT32IDX(DE_BE_SPRITE_ATTR_CTRL_OFF,blk_idx,next_blk_id);
	return 0;
}

__s32 DE_BE_Sprite_Set_Palette_Table(__u32 address, __u32 offset, __u32 size)
{
	__u32 *pdest_end;
    __u32 *psrc_cur;
    __u32 *pdest_cur;

    if(size > DE_BE_SPRITE_PALETTE_TABLE_SIZE)
    {
        size = DE_BE_SPRITE_PALETTE_TABLE_SIZE;
    }

	psrc_cur = (__u32*)address;
	pdest_cur = (__u32*)(DE_Get_Reg_Base(0) + DE_BE_SPRITE_PALETTE_TABLE_ADDR_OFF + offset);
	pdest_end = pdest_cur + (size>>2);

    while(pdest_cur < pdest_end)
    {
    	*(volatile __u32 *)pdest_cur++ = *psrc_cur++;
    }

    return 0;
}

//brightness -100~100
//contrast -100~100
//saturaion -100~100
__s32 Be_Set_Enhance(__u8 sel, __s32 brightness, __s32 contrast, __s32 saturaion)
{
	__s32 Rr,Rg,Rb,Rc;
	__s32 Gr,Gg,Gb,Gc;
	__s32 Br,Bg,Bb,Bc;

	brightness = brightness>100?100:(brightness<0?0:brightness);
	contrast = contrast>100?100:(contrast<0?0:contrast);
	saturaion = saturaion>100?100:(saturaion<0?0:saturaion);

	brightness = (brightness-50) * 10;
	saturaion = saturaion * 10 / 50;
	contrast = contrast * 10 / 50;

	Rr=(1164*183*contrast+1793*439*saturaion) / (1000*1000*10/1024);
	Rg=(1164*614*contrast-1793*399*saturaion) / (1000*1000*10/1024);
	Rb=(1164*62*contrast-1793*40*saturaion) / (1000*1000*10/1024);
	Rc=((1164*(16*contrast*10+brightness*contrast-16*10*10))*0x10) / (1000*10*10);

	Gr=(1164*183*contrast-534*439*saturaion+213*101*saturaion) / (1000*1000*10/1024);
	Gg=(1164*614*contrast+534*399*saturaion+213*338*saturaion) / (1000*1000*10/1024);
	Gb=(1164*62*contrast+534*40*saturaion-213*439*saturaion) / (1000*1000*10/1024);
	Gc=((1164*(16*contrast*10+brightness*contrast-16*10*10))*0x10) / (1000*10*10);

	Br=(1164*183*contrast-2115*101*saturaion) / (1000*1000*10/1024);
	Bg=(1164*614*contrast-2115*338*saturaion) / (1000*1000*10/1024);
	Bb=(1164*62*contrast+2115*439*saturaion) / (1000*1000*10/1024);
	Bc=((1164*(16*contrast*10+brightness*contrast-16*10*10))*0x10) / (1000*10*10);

	Rr = (Rr > 8191)?8191:((Rr < -8191)?-8191:Rr);
	Rg = (Rg > 8191)?8191:((Rg < -8191)?-8191:Rg);
	Rb = (Rb > 8191)?8191:((Rb < -8191)?-8191:Rb);
	Rc = (Rc > 8191)?8191:((Rc < -8191)?-8191:Rc);

	Gr = (Gr > 8191)?8191:((Gr < -8191)?-8191:Gr);
	Gg = (Gg > 8191)?8191:((Gg < -8191)?-8191:Gg);
	Gb = (Gb > 8191)?8191:((Gb < -8191)?-8191:Gb);
	Gc = (Gc > 8191)?8191:((Gc < -8191)?-8191:Gc);

	Br = (Br > 8191)?8191:((Br < -8191)?-8191:Br);
	Rr = (Rr > 8191)?8191:((Rr < -8191)?-8191:Rr);
	Bb = (Bb > 8191)?8191:((Bb < -8191)?-8191:Bb);
	Bc = (Bc > 8191)?8191:((Bc < -8191)?-8191:Bc);

    DE_BE_WUINT32(DE_BE_OUT_COLOR_R_COEFF_OFF + 0, (__s32)Rr);
    DE_BE_WUINT32(DE_BE_OUT_COLOR_R_COEFF_OFF + 4, (__s32)Rg);
    DE_BE_WUINT32(DE_BE_OUT_COLOR_R_COEFF_OFF + 8, (__s32)Rb);
    DE_BE_WUINT32(DE_BE_OUT_COLOR_R_CONSTANT_OFF, (__s32)Rc);

    DE_BE_WUINT32(DE_BE_OUT_COLOR_G_COEFF_OFF + 0, (__s32)Gr);
    DE_BE_WUINT32(DE_BE_OUT_COLOR_G_COEFF_OFF + 4, (__s32)Gg);
    DE_BE_WUINT32(DE_BE_OUT_COLOR_G_COEFF_OFF + 8, (__s32)Gb);
    DE_BE_WUINT32(DE_BE_OUT_COLOR_G_CONSTANT_OFF, (__s32)Gc);

    DE_BE_WUINT32(DE_BE_OUT_COLOR_B_COEFF_OFF + 0, (__s32)Br);
    DE_BE_WUINT32(DE_BE_OUT_COLOR_B_COEFF_OFF + 4, (__s32)Bg);
    DE_BE_WUINT32(DE_BE_OUT_COLOR_B_COEFF_OFF + 8, (__s32)Bb);
    DE_BE_WUINT32(DE_BE_OUT_COLOR_B_CONSTANT_OFF, (__s32)Bc);

    return 0;
}

__s32 DE_BE_enhance_enable(__u32 sel, __bool enable)
{
    DE_BE_WUINT32(DE_BE_OUT_COLOR_CTRL_OFF, enable);

    return 0;
}

__s32 DE_BE_EnableINT(__u8 sel,__u32 irqsrc)
{
    __u32 tmp;

    tmp = DE_BE_RUINT32(DE_BE_INT_EN_OFF);
	DE_BE_WUINT32(DE_BE_INT_EN_OFF, tmp | irqsrc);

	return 0;
}

__s32 DE_BE_DisableINT(__u8 sel, __u32 irqsrc)
{
    __u32 tmp;

    tmp = DE_BE_RUINT32(DE_BE_INT_EN_OFF);
	DE_BE_WUINT32(DE_BE_INT_EN_OFF, tmp & (~irqsrc));

	return 0;
}

__u32 DE_BE_QueryINT(__u8 sel)
{
	__u32 ret = 0;

	ret = DE_BE_RUINT32(DE_BE_INT_FLAG_OFF);

	return ret;
}

__u32 DE_BE_ClearINT(__u8 sel,__u32 irqsrc)
{
	DE_BE_WUINT32(DE_BE_INT_FLAG_OFF,irqsrc);

	return 0;
}

