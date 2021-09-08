/*
 * drivers/input/touchscreen/gsl_point_id.c
 *
 * Copyright (c) 2012 Shanghai Basewin
 *	Guan Yuwei<guanyuwei@basewin.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

//#include "linux/module.h"
//#include <wdm.h>
/*
NTSTATUS
DriverEntry(
  IN PDRIVER_OBJECT DriverObject,
  IN PUNICODE_STRING RegistryPath
);

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

NTSTATUS
DllInitialize( IN PUNICODE_STRING pus )
{
    DbgPrint("GSL_POINT_ID: DllInitialize(%S)\n", pus->Buffer );
    return STATUS_SUCCESS;
}

NTSTATUS
DllUnload( )
{
    DbgPrint("GSL_POINT_ID: DllUnload\n");
    return STATUS_SUCCESS;
}
*/
//#define	GESTURE_ABLE	1
#define	GESTURE_LICH	1

#define GSL_VERSION		0x20140421
#ifndef NULL
#define	NULL  ((void*)0)
#endif
#ifndef UINT
#define	UINT  unsigned int
#endif

#define	POINT_MAX		10
#define	PP_DEEP			10
#define	PS_DEEP			10
#define PR_DEEP			10
#define POINT_DEEP		(PP_DEEP + PS_DEEP + PR_DEEP)
#define	PRESSURE_DEEP		8
#define	CONFIG_LENGTH		512
#define TRUE			1
#define FALSE			0
#define FLAG_ABLE		(0x4<<12)
#define FLAG_FILL		(0x2<<12)
#define	FLAG_KEY		(0x1<<12)
#define	FLAG_COOR		(0x0fff0fff)
#define	FLAG_COOR_EX		(0xffff0fff)
#define	FLAG_ID			(0xf0000000)

struct gsl_touch_info
{
	int x[10];
	int y[10];
	int id[10];
	int finger_num;
};

typedef struct
{
	unsigned int i;
	unsigned int j;
	unsigned int min;//distance min
	unsigned int d[POINT_MAX][POINT_MAX];//distance;
}gsl_DISTANCE_TYPE;

typedef union
{
	struct
	{
		unsigned y:12;
		unsigned key:1;
		unsigned fill:1;
		unsigned able:1;
		unsigned predict:1;
		unsigned x:16;
	}other;
	struct
	{
		unsigned y:13;
		unsigned rev_2:3;
		unsigned x:16;
	}dis;
	unsigned int all;
}gsl_POINT_TYPE;

typedef union
{
	struct
	{
		unsigned delay:8;
		unsigned report:8;
		unsigned rev_1:14;
		unsigned able:1;
		unsigned init:1;
	}other;
	unsigned int all;
}gsl_DELAY_TYPE;

typedef union
{
	struct
	{
		unsigned rev_0:8;
		unsigned rev_1:8;

		unsigned rev_2:7;
		unsigned ex:1;

		unsigned interpolation:4;
		unsigned rev_3:1;
		unsigned only:1;
		unsigned mask:1;
		unsigned reset:1;
	}other;
	unsigned int all;
}gsl_STATE_TYPE;

typedef struct
{
	unsigned int rate;
	unsigned int dis;
	gsl_POINT_TYPE coor;
}gsl_EDGE_TYPE;

typedef union
{
	struct
	{
		short y;
		short x;
	}other;
	unsigned int all;
}gsl_DECIMAL_TYPE;

typedef union
{
	struct
	{
		unsigned over_report_mask:1;
		unsigned opposite_x:1;
		unsigned opposite_y:1;
		unsigned opposite_xy:1;
		unsigned line:1;
		unsigned line_neg:1;
		unsigned line_half:1;
		unsigned middle_drv:1;

		unsigned key_only_one:1;
		unsigned key_line:1;
		unsigned refe_rt:1;
		unsigned refe_var:1;
		unsigned base_median:1;
		unsigned key_rt:1;
		unsigned refe_reset:1;
		unsigned sub_cross:1;

		unsigned row_neg:1;
		unsigned sub_line_coe:1;
		unsigned sub_row_coe:1;
		unsigned c2f_able:1;
		unsigned thumb:1;
		unsigned graph_h:1;
		unsigned init_repeat:1;
		unsigned near_reset_able:1;

		unsigned emb_dead:1;
		unsigned emb_point_mask:1;
		unsigned interpolation:1;
		unsigned sum2_able:1;
		unsigned reduce_pin:1;
		unsigned drv_order_ex:1;
		unsigned id_over:1;
		unsigned rev_1:1;
	}other;
	unsigned int all;
}gsl_FLAG_TYPE;

static gsl_POINT_TYPE point_array[POINT_DEEP][POINT_MAX];
static gsl_POINT_TYPE *point_pointer[PP_DEEP];
static gsl_POINT_TYPE *point_stretch[PS_DEEP];
static gsl_POINT_TYPE *point_report[PR_DEEP];
static gsl_POINT_TYPE point_now[POINT_MAX];
static gsl_DELAY_TYPE point_delay[POINT_MAX];
static int filter_deep[POINT_MAX];
static gsl_EDGE_TYPE point_edge;
static gsl_DECIMAL_TYPE point_decimal[POINT_MAX];

static unsigned int pressure_now[POINT_MAX];
static unsigned int pressure_array[PRESSURE_DEEP][POINT_MAX];
static unsigned int pressure_report[POINT_MAX];
static unsigned int *pressure_pointer[PRESSURE_DEEP];

#define	pp		point_pointer
#define	ps		point_stretch
#define	pr		point_report
#define	point_predict	pp[0]
#define	pa		pressure_pointer

static	gsl_STATE_TYPE global_state;
static	int inte_count;
static	unsigned int csensor_count;
static	unsigned int click_count[4];
static	gsl_POINT_TYPE point_click[4];
static	unsigned int double_click;
static	int point_n;
static	int point_num;
static	int prev_num;
static	int point_near;
static	unsigned int point_shake;
static	unsigned int reset_mask_send;
static	unsigned int reset_mask_max;
static	unsigned int reset_mask_count;
static	gsl_FLAG_TYPE global_flag;
static	unsigned int id_first_coe;
static	unsigned int id_speed_coe;
static	unsigned int id_static_coe;
static	unsigned int average;
static	unsigned int soft_average;
static	unsigned int report_delay;
static	unsigned int report_ahead;
static	unsigned char median_dis[4];
static	unsigned int shake_min;
static	int match_y[2];
static	int match_x[2];
static	int ignore_y[2];
static	int ignore_x[2];
static	int screen_y_max;
static	int screen_x_max;
static	int point_num_max;
static	unsigned int drv_num;
static	unsigned int sen_num;
static	unsigned int drv_num_nokey;
static	unsigned int sen_num_nokey;
static	unsigned int coordinate_correct_able;
static	unsigned int coordinate_correct_coe_x[64];
static	unsigned int coordinate_correct_coe_y[64];
static	unsigned int edge_cut[4];
static	unsigned int stretch_array[4*4*2];
static	unsigned int shake_all_array[2*8];
static	unsigned int reset_mask_dis;
static	unsigned int reset_mask_type;
static	unsigned int key_map_able;
static	unsigned int key_range_array[8*3];
static	int  filter_able;
static	unsigned int filter_coe[4];
static	unsigned int multi_x_array[4],multi_y_array[4];
static	unsigned int multi_group[4][64];
static	int ps_coe[4][8],pr_coe[4][8];
static	int point_repeat[2];
static	int near_set[2];
static	int diagonal;
// 	unsigned int key_dead_time			;
// 	unsigned int point_dead_time		;
// 	unsigned int point_dead_time2		;
// 	unsigned int point_dead_distance	;
// 	unsigned int point_dead_distance2	;
// 	unsigned int pressure_able;
// 	unsigned int pressure_save[POINT_MAX];
static	unsigned int edge_first;
static	unsigned int edge_first_coe;

static	unsigned int point_corner;
//-------------------------------------------------
static	unsigned int config_static[CONFIG_LENGTH];
//-------------------------------------------------
#ifdef GESTURE_ABLE

#define MAXSTACK	200
#define GesturePtNum	15
typedef union
{

	struct
	{
		unsigned y:16;
		unsigned x:12;
		unsigned id:4;
	} point_data;
	unsigned int data_int;
}POINT_TYPE_DEFINE;

typedef struct
{
	int top;
	POINT_TYPE_DEFINE  point_buff[MAXSTACK];
}TouchFinger;

static struct
{
	int num[10];
	int flag;
	int position;
}vector_x_y[2];
static TouchFinger point_stack;
static int qushi_x[10];
static int qushi_y[10];
static int vector_change_x[MAXSTACK];
static int vector_change_y[MAXSTACK];
static int rate_weight[MAXSTACK];
static POINT_TYPE_DEFINE top,bottom,left,right;
static int Letter_width;
static int Letter_height;
static unsigned int max_x,min_x,max_y,min_y;
static char gesture_letter;
#endif
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++
#ifdef GESTURE_LICH
#define	GESTURE_BUF_SIZE		256
#define	GESTURE_SIZE_REFE		255
#define	GESTURE_SIZE_NUM		32
#define	GESTURE_XY			0x1
#define	GESTURE_DEAL			0x2
#define	GESTURE_LRUD			0x4
#define	GESTURE_ALL			0x7fffffff
typedef union
{
	struct
	{
		unsigned y:12;
		unsigned rev:4;
		unsigned x:16;
	}other;
	unsigned int all;
}GESTURE_POINT_TYPE;
typedef struct
{
	int coe;
	int out;
	unsigned int coor[GESTURE_SIZE_NUM/2];
}GESTURE_MODEL_TYPE;
//GESTURE_POINT_TYPE gesture_buf[GESTURE_BUF_SIZE];//gesture_buf
#define	gesture_buf	 ((GESTURE_POINT_TYPE*)config_static)
#define	gesture_standard ((GESTURE_POINT_TYPE*)(&config_static[GESTURE_BUF_SIZE]))
static int  gesture_num,gesture_num_last;//gesture_num
static int  gesture_dis_min;
static int  gesture_deal;
static int  gesture_last;
static int  gesture_threshold[2];
static  int x_scale;
static  int y_scale;
static int double_down,double_up;
static const GESTURE_MODEL_TYPE * model_extern = NULL;
static int  model_extern_len = 0;
static int  GestureSqrt(int d);
static int  GestureDistance(GESTURE_POINT_TYPE* d1,GESTURE_POINT_TYPE* d2,int sqrt_able);
static int  GesturePush(GESTURE_POINT_TYPE* data);
static int  GestureStretch(void);
static int  GestureLength(void);
static int  GestureDeal(void);
static int  GestureModel(const GESTURE_MODEL_TYPE * model,int len,int threshold,int *out);
static int  GestureMain(unsigned int data[],unsigned int pn);
static void GestureStandard(void);
static void GestureInit(void);
static void ChangeXY(void);
static int  GestureLRUD(void);
static void GestureSet(unsigned int conf[]);
static const GESTURE_MODEL_TYPE model_default[]
={
	{0x10,'3',{
	0x37170105,0x78580000,0xba990a03,0xedd92e14,0xb9d85347,0x7798655b,0x3657716b,0x1f156d74,
	0x60406969,0xa2816f69,0xe3c28075,0xf9fbb899,0xc3e3e0d2,0x83a4f6ee,0x4262fffc,0x0021f7fd,}},
	{0x10,'6',{
	0xa2be0400,0x70881f10,0x4258402e,0x1d2e6c54,0x040ea084,0x0a01d6bc,0x381df9ec,0x7054fffd,
	0xa88cfafe,0xdac2ddef,0xfff0b2cb,0xe2f78497,0xaac7747a,0x728e7472,0x3b56817b,0x0420968b,}},
	{0x10,'7',{
	0x12000001,0x37240000,0x5b490000,0x806e0000,0xa5930000,0xcab70000,0xefdc0300,0xf9fd1f0e,
	0xe2ee3d30,0xc5d4564a,0xa7b76c61,0x8c9a8579,0x717e9f93,0x5863bbad,0x434cdbc9,0x3c3dffec,}},
	{0x10,'8',{
	0xdaff030c,0x8eb40000,0x41670c06,0x001c3116,0x431e5448,0x8f69635d,0xd1b58a6f,0xcedfd0af,
	0x88acf5e6,0x3c62fffd,0x0718cdf1,0x341493aa,0x7a556d7d,0xc19e4f60,0xf9e51c3c,0xb5dc0005,}},
	{0x10,'8',{
	0x627d231e,0x2f49382c,0x03175a48,0x21098172,0x563c958c,0x856eb0a2,0x8f99dac4,0x5b76eee5,
	0x243ffdf5,0x090ddbf4,0x2918acc2,0x4d3a8497,0x78636172,0xa38e4050,0xd0ba1f2e,0xffe7000f,}},
	{0x10,'9',{
	0xe8ff0715,0xb4ce0001,0x819a0500,0x4f68150c,0x1e362a1e,0x000c543c,0x270d7169,0x5b417273,
	0x9076666d,0xbda74a5a,0xddcf1e36,0xc8d7321b,0xb4be634b,0xa4ac967d,0x959ccab0,0x898fffe4,}},
	{0x10,'A',{
	0xaeca000b,0x74900e02,0x41582d1b,0x182a5942,0x02099375,0x0600cfb1,0x2c15fcea,0x664af1fe,
	0x957ec8dd,0xb5a894b0,0xc9bf5876,0xd7d31c3a,0xd4d75134,0xd3d38d6f,0xdbd4c9ab,0xffe9fce6,}},
	{0x10,'A',{
	0x8eab0102,0x56711307,0x2c3f3a25,0x0e1b6b51,0x0004a689,0x0e02ddc2,0x3e22fbf2,0x725be8fa,
	0x9284b6d0,0xa69d7f9b,0xb3ae4562,0xb7b80b28,0xa7a6290c,0xb1aa6346,0xd0be947d,0xffe7bba7,}},
	{0x10,'B',{
	0x56591a00,0x474e4e35,0x343f8168,0x242cb59c,0x0f1be7ce,0x170ddbf4,0x3c25b4c4,0x6e549ca6,
	0xa3889799,0xd8bd9d96,0xfcf1bea8,0xd3e9e4d4,0xa0baf6f0,0x6b85fcfa,0x3650fffd,0x001bfbff,}},
	{0x10,'C',{
	0xfaff2337,0xdaec0913,0xb0c50003,0x879c0500,0x5f720f09,0x3b4c271a,0x1d2b4534,0x08116a56,
	0x0003937f,0x0a03bca8,0x2515ddce,0x4b38f2e8,0x7560fff9,0x9e89f7fd,0xc6b2e8f0,0xeed9d7df,}},
	{0x10,'C',{
	0xacbf0100,0x86990a04,0x64751b12,0x45533225,0x2b375141,0x17217160,0x080f9582,0x0103bba8,
	0x0200e2cf,0x200ff9f1,0x4633fefc,0x6c59ffff,0x9380fcfd,0xb9a6f4f9,0xdccbe4ed,0xffeeceda,}},
	{0x10,'C',{
	0x57670a00,0x3a492116,0x222d3d2e,0x0f175e4d,0x0408816f,0x0001a693,0x0300cab8,0x0e07eddc,
	0x2f1dfefb,0x5241f5fc,0x7362e2ec,0x8e80c9d6,0xa89bafbc,0xc3b594a1,0xe1d27e89,0xfff06673,}},
	{0x10,'D',{
	0x99b5858f,0x5f7c8883,0x28429c8f,0x010fc6ab,0x240cf4e1,0x5d41fefb,0x957af1fc,0xc1adcbe0,
	0xd2cc92af,0xd3d25875,0xd7d71f3b,0xd7d71b02,0xd4d75538,0xd4d48f72,0xe1d9c9ac,0xffe6f4e6,}},
	{0x10,'E',{
	0x391c948f,0x73569595,0xad908a92,0xddc8677e,0xf1ee304d,0xc3dd0d1b,0x89a70002,0x536d1304,
	0x233b3a25,0x08137053,0x0301aa8d,0x220edcc6,0x573af7ee,0x9174fffc,0xcaaef6ff,0xffe5dbeb,}},
	{0x10,'G',{
	0xaaca0000,0x698a0000,0x2a491106,0x000f4226,0x23067061,0x64437674,0xa3836874,0xdac04759,
	0xfaec0b2a,0xfefb401f,0xffff8160,0xf5fdc0a1,0xc9e9eedf,0x89a9fff9,0x4869faff,0x0928e3f3,}},
	{0x10,'G',{
	0xeaff1421,0xb9d20308,0x88a00000,0x57700f05,0x2b3f2618,0x09174d37,0x00037f66,0x0d05af97,
	0x2a1adac7,0x5940ede5,0x8b72f2f3,0xbca4e9ee,0xe4d2cbde,0xfbf09cb3,0xf5f8ceb5,0xe9f1ffe6,}},
	{0x10,'H',{
	0x03021300,0x06053a26,0x0b0a604d,0x0b0b8774,0x0a0bae9a,0x0506d4c1,0x0002fbe8,0x1104e0f0,
	0x2e1ec3d1,0x503dadb5,0x7764a5a7,0x9e8aa1a2,0xc4b1a3a0,0xead8ada8,0xfff8d1bd,0xfffff8e4,}},
	{0x10,'K',{
	0x1d1a2000,0x171a6040,0x1114a080,0x060edfc0,0x1100e2ff,0x3420a8c5,0x6f4f8b95,0xaf8f8285,
	0xefcf8683,0xe1fcb3a0,0xa0c1c2bb,0x6080c6c5,0x2c40c9c7,0x6c4cd8d1,0xac8ceadf,0xedccfef3,}},
	{0x10,'K',{
	0x22341900,0x15185436,0x0e119072,0x0c0cccae,0x0709f6ea,0x0a07b9d8,0x2918859e,0x5b406170,
	0x90796658,0x627c8b7c,0x2a47a79a,0x110db8b1,0x4d2fbfbc,0x896bcac4,0xc3a6d9d1,0xffe1ede2,}},
	{0x10,'L',{
	0x3f4a0c00,0x35372c1c,0x2c314d3d,0x26296e5d,0x1b218e7e,0x1316af9f,0x0910cfc0,0x0004f1e0,
	0x1605ffff,0x3727ffff,0x5848ffff,0x7a69fdff,0x9b8afcfd,0xbcabfcfc,0xddcdfafb,0xffeef9f9,}},
	{0x10,'M',{
	0x0900e0ff,0x2017a0c0,0x3a296381,0x4e442443,0x5a583010,0x6b5f6f4f,0x7471ae8f,0x7977eece,
	0x8c80c5e5,0xa19886a5,0xbaad4766,0xd3c70a29,0xddda3516,0xe7e17555,0xf4f0b494,0xfffaf4d4,}},
	{0x10,'N',{
	0x0400e7ff,0x130bb8cf,0x281e89a1,0x38305a71,0x51452c43,0x675d1d13,0x68684e36,0x6b697f66,
	0x726fb097,0x7875e0c8,0x907ee8f8,0xa79ebbd2,0xbfb38fa4,0xd1c95f77,0xe6da3148,0xfff20019,}},
	{0x10,'O',{
	0x2e3f311f,0x101e5c46,0x03088f76,0x0001c2a8,0x1e08e7da,0x4f35fdf4,0x8168fcff,0xb39beef5,
	0xdac7cedf,0xf3e9a0b8,0xfef96d87,0xf9ff3c54,0xdaec1326,0xaac30108,0x77900100,0x465e1407,}},
	{0x10,'O',{
	0xd0e30213,0x9cb60000,0x68820c05,0x384f2416,0x17254c36,0x040c8066,0x0001b49a,0x1305e4ce,
	0x442bfaf0,0x785efffe,0xab92f1f9,0xd3c1cfe3,0xf1e3a1b9,0xfffa6e88,0xf8ff3b54,0xd5e81024,}},
	{0x10,'O',{
	0x000f768a,0x0900455d,0x2b171e30,0x5a420611,0x8d740100,0xbca5170b,0xe3d23824,0xfcf2644c,
	0xfaff977e,0xe3f0c4af,0xbdd2e6d7,0x8ea7faf2,0x5b74fefe,0x2e44e9f6,0x0a1bc2d6,0x02028fa9,}},
	{0x10,'O',{
	0x829c0900,0x4e682315,0x24384a34,0x08157c61,0x0002b598,0x0d03edd1,0x4326fffd,0x7a5feef8,
	0xab93cfe0,0xd5c1a7bd,0xf4e67690,0xfbff3d5a,0xcfec1b26,0x96b31818,0x5e7a251d,0x28433b2f,}},
	{0x10,'O',{
	0x381e5e68,0x6e535156,0xa388504f,0xd8be5e56,0xf9ed876d,0xf9fcbaa1,0xd8ece5d2,0xa5c0f9f2,
	0x708bfffd,0x3b55fbfe,0x1423d7ed,0x0006a4bf,0x09027089,0x26154157,0x50391e2e,0x7e670010,}},
	{0x10,'O',{
	0x8670020b,0xb8a01307,0xe3ce3423,0xf8f0664c,0xfffc9b81,0xf1faceb5,0xcee4f4e5,0x9ab4fffb,
	0x657fffff,0x364ceaf8,0x1623c0d7,0x00098ea7,0x06015973,0x24122e41,0x4d380c1c,0x82670104,}},
	{0x10,'S',{
	0xb7cf0001,0x869e0301,0x556d0905,0x273e1f12,0x0311442c,0x2009665a,0x5138726b,0x826a7876,
	0xb39b807b,0xdfcb998b,0xfff5bea7,0xdcf2ded0,0xadc6f1e8,0x7c94fbf7,0x4b63fffd,0x1932ffff,}},
	{0x10,'S',{
	0xcbde0200,0xa8ba1209,0x8597241a,0x6a753f2f,0x806c5751,0xa6935e5b,0xccb96662,0xf2e0746b,
	0xfcff9482,0xe0f0b2a6,0xbdcfc2bb,0x97abd1c9,0x7385ddd7,0x4c60e8e3,0x273af4ee,0x0014fff9,}},
	{0x10,'U',{
	0x050d2209,0x0001573c,0x03008c71,0x1106bfa6,0x2f1bebda,0x604ae1f0,0x8873bccd,0xa59990a8,
	0xbcb05f78,0xcdc72c46,0xd0d00911,0xc9cb3e24,0xc6c87359,0xc9c6a88d,0xd8d2ddc2,0xffe4fff6,}},
	{0x10,'V',{
	0x09000f00,0x1911301f,0x27205240,0x342d7563,0x413a9785,0x4f47b9a8,0x6057d9c9,0x7569f9ea,
	0x9486f4ff,0xa99fd5e5,0xb8b0b4c5,0xc9c093a3,0xdbd17484,0xe9e35263,0xf5ef2f41,0xfff90b1d,}},
	{0x10,'V',{
	0x08001908,0x160f3b2b,0x251d5d4c,0x312b806f,0x3e37a392,0x4843c6b5,0x524de9d8,0x5b58f2fb,
	0x6560cfe0,0x776dafbf,0x8a8090a0,0x9d947080,0xb4a85361,0xccc03745,0xe6d91d2a,0xfff2000e,}},
	{0x10,'W',{
	0x06001f00,0x110c5f3f,0x1c189f7f,0x2822debe,0x4131e3fd,0x554ba4c3,0x655c6484,0x786f2444,
	0x847f2f0f,0x8a866f4f,0x928eae8f,0x9e99eece,0xbaacd0ee,0xd5c893b3,0xebe05373,0xfff61333,}},
	{0x10,'W',{
	0xf7ff2000,0xe4ed6040,0xd5dba181,0xbdcbe0c0,0xa0aad3f3,0x909892b3,0x848a5272,0x80811131,
	0x777d3d1d,0x636d7e5e,0x535bbf9e,0x3c48fdde,0x272fc1e2,0x121a82a2,0x030a4161,0x00020021,}},
	{0x10,'Y',{
	0x16000b13,0x442d0303,0x4b48341b,0x61505e4b,0x91795c62,0xbca74551,0xe1d02637,0xfcf40c14,
	0xe8f13922,0xdbe16951,0xd6d99a82,0xced3cbb2,0xb0c4f0e1,0x7f98fbf7,0x4e67fefd,0x1d36fdff,}},
	{0x10,'Z',{
	0x30160200,0x644a0403,0x997f0303,0xcdb30202,0xe7e80d00,0xc4d63622,0x9bb05c4a,0x73857f6c,
	0x5061a893,0x293eccbb,0x0013f2dd,0x2d13fcfd,0x6248f6f8,0x967cf1f3,0xcbb1edef,0xffe5f1ec,}},
	//--------------------------------------------------------------------------------------------
	{0x10,0x1001,{
	0x0003ecff,0x0502c6d9,0x0b099fb2,0x1410788c,0x221b5265,0x362b2f40,0x5341121d,0x7966020a,
	0xa08d0100,0xc3b3170b,0xd9ce3a28,0xede25f4d,0xf4f28672,0xfaf6ac99,0xfffdd3c0,0xfffffae6,}},
	{0x10,0x1002,{
	0x1900847d,0x4c328785,0x7f658b88,0xb198898b,0xe3cb7f88,0xf8f24f68,0xecf81d35,0xbfd9010b,
	0x90a61504,0x8186462d,0x7c7d7960,0x7f7bac93,0x8883dfc6,0xaf97fff4,0xdcc7eafa,0xffeec1d7,}},
	{0x10,0x1003,{
	0x7543141a,0xd7a82e17,0xf8f78f5c,0xc6e6e9bf,0x6497f8fa,0x1338b3db,0x03024f82,0x4a1c0620,
	0xaf7c1001,0xf8da582b,0xeafebc8b,0x9acbfbe6,0x3868eeff,0x0a1892c4,0x2712305f,0x85520e11,}},
	{0x10,0x1004,{
	0x04003204,0x120b8e60,0x1714ebbd,0x2f27b8e6,0x43385b89,0x5b4c032d,0x6a666032,0x7770bc8e,
	0x8c82b8e6,0x9b945c8a,0xaea2012d,0xbbb75d2f,0xc9c1ba8c,0xe3d5bce7,0xf1ea5f8d,0xfff50231,}},
	{0x10,0x1005,{
	0x1900020a,0x4d330400,0x7c65180a,0xa18f3f2b,0xb3ac7258,0xbcb8a58b,0xb0b9d7bf,0x8aa0fcec,
	0x5971f2fd,0x414bc2db,0x433f8ea8,0x534a5d76,0x71613246,0x9d85101e,0xd0b60609,0xffea1409,}},
	{0x10,0x1006,{
	0xdeff0714,0x9abc0002,0x57790a02,0x1d373018,0x01076d4b,0x2f0f988a,0x72519c9c,0xb2948b9c,
	0xa3bc576b,0x60815653,0x26417963,0x020eb191,0x2005e7d3,0x6442fbf4,0xa785fbff,0xebc9e7f3,}},
	{0x10,0x1007,{
	0xf6ff0600,0xe4ed140c,0xd5dd261d,0xc5cd362e,0xb3bc463e,0xa2aa574e,0x939a6860,0x83897970,
	0x747c8a82,0x646c9c93,0x525baca4,0x434abdb4,0x333bcfc6,0x222addd5,0x121beee6,0x0009fff6,}},
	{0x10,0x1008,{
	0x09000900,0x19111b13,0x28202d24,0x39303d35,0x49424e45,0x5a515d56,0x6b626d65,0x7d747b74,
	0x8e868b82,0x9d969c93,0xaca4aea5,0xbcb4bfb6,0xcac4d1c7,0xdbd2e1d9,0xece3f1e9,0xfff5fff8,}},
	{0x10,0x1009,{
	0x0a00faff,0x1d14edf4,0x2e25dce5,0x3e36cbd4,0x4e44bac2,0x5f57aab2,0x6f679aa2,0x7f778890,
	0x8c85757e,0x9c94636b,0xaba3515a,0xbbb2414a,0xcbc33039,0xddd31f28,0xeee51119,0xfff7000a,}},
	{0x10,0x100a,{
	0xf4fffaff,0xe1eaeff6,0xd1d9dde6,0xc1cacdd5,0xb4bbbac3,0xa2aca8b0,0x929a99a1,0x83888791,
	0x767d747d,0x666e636b,0x535c545e,0x444d424b,0x333d323a,0x242b212a,0x141c0f18,0x000a0008,}},
	{0x10,0x100b,{
	0x30000208,0x90600200,0xf1c11e0c,0xb5e64d3f,0x55855653,0x32245556,0x93625756,0xf1c3745e,
	0xbaeaa997,0x598ab2b0,0x0f29aab1,0x7040a8a7,0xd1a0b8ae,0xd0f8ecd0,0x6f9ffcf5,0x0e3ffcff,}},
	{0x10,0x100c,{
	0x2600140c,0x66494226,0x827c8d66,0x6d80d8b4,0x284ffaf4,0x0a0ab9e0,0x411f8097,0x8e689182,
	0xd6b47f94,0xf9f23960,0xd5f80012,0x91af270a,0x7e83734c,0x7e7dc19a,0xb490fae5,0xffdbe9fe,}},
	{0x10,0x100d,{
	0x768e0c00,0x465e2619,0x192f4635,0x0107745a,0x32178a85,0x674d858a,0x9b81747d,0xccb45c69,
	0xf7e33b4d,0xebfd0d21,0xb6d00004,0x939e270d,0x888d5d42,0x83859378,0x7c80c9ae,0x6c75ffe4,}},
	{0x10,0x100e,{
	0xb3bd1000,0x9ea82d1e,0x87924a3b,0x6f7b6658,0x56628375,0x3e4a9e91,0x2934bcad,0x1720ddcd,
	0x010bfced,0x2513ffff,0x4937fdff,0x6d5bf9fa,0x9280f4f7,0xb6a4f0f1,0xdac8eeef,0xffececed,}},
};
#endif
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++
static void SortBubble(int t[],int size)
{
	int temp = 0;
	int m,n;
	for(m=0;m<size;m++)
	{
		for(n=m+1;n<size;n++)
		{
			temp = t[m];
			if (temp>t[n])
			{
				t[m] = t[n];
				t[n] = temp;
			}
		}
	}
}
int abs(int i){
 	if(i<0)
		return ~(--i);
	return i;
}
static int Sqrt(int d)
{
	int ret = 0;
	int i;
	for(i=14;i>=0;i--)
	{
		if((ret + (0x1<<i))*(ret + (0x1<<i)) <= d)
			ret |= (0x1<<i);
	}
	return ret;
}

static void PointCoor(void)
{
	int i;
	point_num &= 0xff;
	for(i=0;i<point_num;i++)
	{
		if(global_state.other.ex)
			point_now[i].all &= (FLAG_COOR_EX | FLAG_KEY | FLAG_ABLE);
		else
			point_now[i].all &= (FLAG_COOR | FLAG_KEY | FLAG_ABLE);
	}
}
static void PointRepeat(void)
{
	int i,j;
	int x,y;
	int x_min,x_max,y_min,y_max;
	int pn;
	if(point_near)
		point_near --;
	if(prev_num > point_num)
		point_near = 8;
	if(point_repeat[0]==0 || point_repeat[1]==0)
	{
		if(point_near)
			pn = 96;
		else
			pn = 32;
	}
	else
	{
		if(point_near)
			pn = point_repeat[1];
		else
			pn = point_repeat[0];
	}
	for(i=0;i<POINT_MAX;i++)
	{
		if(point_now[i].all == 0)
			continue;
		if (point_now[i].other.key)
			continue;
		x_min = point_now[i].other.x - pn;
		x_max = point_now[i].other.x + pn;
		y_min = point_now[i].other.y - pn;
		y_max = point_now[i].other.y + pn;
		for(j=i+1;j<POINT_MAX;j++)
		{
			if(point_now[j].all == 0)
				continue;
			if (point_now[j].other.key)
				continue;
			x = point_now[j].other.x;
			y = point_now[j].other.y;
			if(x>x_min && x<x_max && y>y_min && y<y_max)
			{
				point_now[i].other.x =
					(point_now[i].other.x +
					 point_now[j].other.x + 1) / 2;
				point_now[i].other.y =
					(point_now[i].other.y +
					 point_now[j].other.y + 1) / 2;
				point_now[j].all = 0;
				i--;
				point_near = 8;
				break;
			}
		}
	}
}

static void PointPointer(void)
{
	int i,pn;
	point_n ++ ;
	if(point_n >= PP_DEEP * PS_DEEP * PR_DEEP * PRESSURE_DEEP)
		point_n = 0;
	pn = point_n % PP_DEEP;
	for(i=0;i<PP_DEEP;i++)
	{
		pp[i] = point_array[pn];
		if(pn == 0)
			pn = PP_DEEP - 1;
		else
			pn--;
	}
	pn = point_n % PS_DEEP;
	for(i=0;i<PS_DEEP;i++)
	{
		ps[i] = point_array[pn+PP_DEEP];
		if(pn == 0)
			pn = PS_DEEP - 1;
		else
			pn--;
	}
	pn = point_n % PR_DEEP;
	for(i=0;i<PR_DEEP;i++)
	{
		pr[i] = point_array[pn+PP_DEEP+PS_DEEP];
		if(pn == 0)
			pn = PR_DEEP - 1;
		else
			pn--;
	}
	pn = point_n % PRESSURE_DEEP;
	for(i=0;i<PRESSURE_DEEP;i++)
	{
		pa[i] = pressure_array[pn];
		if(pn == 0)
			pn = PRESSURE_DEEP - 1;
		else
			pn--;
	}
	//------------------------------------------------------
	pn = 0;
	for(i=0;i<POINT_MAX;i++)
	{
		if(point_now[i].all)
			point_now[pn++].all = point_now[i].all;
		pp[0][i].all = 0;
		ps[0][i].all = 0;
		pr[0][i].all = 0;
	}
	point_num = pn;
	for(i=pn;i<POINT_MAX;i++)
		point_now[i].all = 0;
}

static unsigned int CCO(unsigned int x,unsigned int coe[],int k)
{
	if(k == 0)
	{
		if(x & 32)
			return (x & ~31)+(31 - (coe[31-(x&31)] & 31));
		else
			return (x & ~31)+(coe[x&31] & 31);
	}
	if(k == 1)
	{
		if(x & 64)
			return (x & ~63)+(63 - (coe[63-(x&63)] & 63));
		else
			return (x & ~63)+(coe[x&63] & 63);
	}
	if(k == 2)
	{
		return (x & ~63)+(coe[x&63] & 63);
	}
	return 0;
}

static void CoordinateCorrect(void)
{
	typedef struct
	{
		unsigned int range;
		unsigned int group;
	}MULTI_TYPE;
#ifdef LINE_MULTI_SIZE
	#define	LINE_SIZE	LINE_MULTI_SIZE
#else
	#define	LINE_SIZE		4
#endif
	int i,j;
	unsigned int *px[LINE_SIZE+1],*py[LINE_SIZE+1];
	MULTI_TYPE multi_x[LINE_SIZE],multi_y[LINE_SIZE];
	unsigned int edge_size = 64;
	int kx,ky;
	if((coordinate_correct_able&0xf) == 0)
		return;
	kx = (coordinate_correct_able>>4)&0xf;
	ky = (coordinate_correct_able>>8)&0xf;
	px[0] = coordinate_correct_coe_x;
	py[0] = coordinate_correct_coe_y;
	for(i=0;i<LINE_SIZE;i++)
	{
		px[i+1] = NULL;
		py[i+1] = NULL;
	}
	if(kx == 3 || ky == 3)
	{
		i=0;
		if(((coordinate_correct_able>>4)&0xf) == 3)
			px[1] = multi_group[i++];
		if(((coordinate_correct_able>>8)&0xf) == 3)
			py[1] = multi_group[i++];
	}
	else
	{
		for(i=0;i<LINE_SIZE;i++)
		{
			multi_x[i].range = multi_x_array[i] & 0xffff;
			multi_x[i].group = multi_x_array[i] >> 16;
			multi_y[i].range = multi_y_array[i] & 0xffff;
			multi_y[i].group = multi_y_array[i] >> 16;
		}
		j=1;
		for(i=0;i<LINE_SIZE;i++)
			if(multi_x[i].range && multi_x[i].group<LINE_SIZE)
				px[j++] = multi_group[multi_x[i].group];
		j=1;
		for(i=0;i<LINE_SIZE;i++)
			if(multi_y[i].range && multi_y[i].group<LINE_SIZE)
				py[j++] = multi_group[multi_y[i].group];
	}
	for(i=0;i<(int)point_num && i<POINT_MAX;i++)
	{
		if(point_now[i].all==0)
			break;
		if (point_now[i].other.key != 0)
			continue;
		if (point_now[i].other.x >= edge_size &&
			point_now[i].other.x <= drv_num_nokey * 64 - edge_size)
		{
			if(kx == 3)
			{
				if (point_now[i].other.x & 64)
					point_now[i].other.x = CCO(point_now[i].other.x, px[0], 2);
				else
					point_now[i].other.x = CCO(point_now[i].other.x, px[1], 2);
			}
			else
			{
				for(j=0;j<LINE_SIZE+1;j++)
				{
					if (!(j >= LINE_SIZE ||
						px[j + 1] == NULL ||
						multi_x[j].range == 0 ||
						point_now[i].other.x < multi_x[j].range))
						continue;
					point_now[i].other.x = CCO(point_now[i].other.x, px[j], kx);
					break;
				}
			}
		}
		if (point_now[i].other.y >= edge_size &&
			point_now[i].other.y <= sen_num_nokey * 64 - edge_size)
		{
			if(ky == 3)
			{
				if (point_now[i].other.y & 64)
					point_now[i].other.y = CCO(point_now[i].other.y, py[0], 2);
				else
					point_now[i].other.y = CCO(point_now[i].other.y, py[1], 2);
			}
			else
			{
				for(j=0;j<LINE_SIZE+1;j++)
				{
					if (!(j >= LINE_SIZE ||
						py[j + 1] == NULL ||
						multi_y[j].range == 0 ||
						point_now[i].other.y < multi_y[j].range))
						continue;
					point_now[i].other.y = CCO(point_now[i].other.y, py[j], ky);
					break;
				}
			}
		}
	}
#undef LINE_SIZE
}

static void PointPredictOne(unsigned int n)
{
	pp[0][n].all = pp[1][n].all & FLAG_COOR;
	pp[0][n].other.predict = 0;
}

static void PointPredictTwo(unsigned int n)
{
	unsigned int t;
	pp[0][n].all = 0;
	t = pp[1][n].other.x * 2;
	if (t > pp[2][n].other.x)
		t -= pp[2][n].other.x;
	else
		t = 0;
	if(t > 0xffff)
		pp[0][n].other.x = 0xffff;
	else
		pp[0][n].other.x = t;
	t = pp[1][n].other.y * 2;
	if (t > pp[2][n].other.y)
		t -= pp[2][n].other.y;
	else
		t = 0;
	if(t > 0xfff)
		pp[0][n].other.y = 0xfff;
	else
		pp[0][n].other.y = t;
	pp[0][n].other.predict = 1;
}

static void PointPredictThree(unsigned int n)
{
	unsigned int t,t2;
	pp[0][n].all = 0;
	t = pp[1][n].other.x * 5 + pp[3][n].other.x;
	t2 = pp[2][n].other.x * 4;
	if(t > t2)
		t -= t2;
	else
		t = 0;
	t /= 2;
	if(t > 0xffff)
		pp[0][n].other.x = 0xffff;
	else
		pp[0][n].other.x = t;
	t = pp[1][n].other.y * 5 + pp[3][n].other.y;
	t2 = pp[2][n].other.y * 4;
	if(t > t2)
		t -= t2;
	else
		t = 0;
	t /= 2;
	if(t > 0xfff)
		pp[0][n].other.y = 0xfff;
	else
		pp[0][n].other.y = t;
	pp[0][n].other.predict = 1;
}

static void PointPredict(void)
{
	int i;
	for(i=0;i<POINT_MAX;i++)
	{
		if(pp[1][i].all != 0)
		{
			if (global_state.other.interpolation
				|| pp[2][i].all == 0
				|| pp[2][i].other.fill != 0
				|| pp[3][i].other.fill != 0
				|| pp[1][i].other.key != 0
				|| global_state.other.only)
			{
				PointPredictOne(i);
			}
			else if(pp[2][i].all != 0)
			{
				if(pp[3][i].all != 0)
					PointPredictThree(i);
				else
					PointPredictTwo(i);
			}
			pp[0][i].all |= FLAG_FILL;
			pa[0][i] = pa[1][i];
		}
		else
			pp[0][i].all = 0x0fff0fff;
		if (pp[1][i].other.key)
			pp[0][i].all |= FLAG_KEY;
	}
}

static unsigned int PointDistance(gsl_POINT_TYPE *p1,gsl_POINT_TYPE *p2)
{
	int a,b,ret;
	a = p1->dis.x;
	b = p2->dis.x;
	ret = (a-b)*(a-b);
	a = p1->dis.y;
	b = p2->dis.y;
	ret += (a-b)*(a-b);
	return ret;
}

static void DistanceInit(gsl_DISTANCE_TYPE *p)
{
	int i;
	unsigned int *p_int = &(p->d[0][0]);
	for(i=0;i<POINT_MAX*POINT_MAX;i++)
		*p_int++ = 0x7fffffff;
}

static int DistanceMin(gsl_DISTANCE_TYPE *p)
{
	int i,j;
	p->min = 0x7fffffff;
	for(j=0;j<POINT_MAX;j++)
	{
		for(i=0;i<POINT_MAX;i++)
		{
			if(p->d[j][i] < p->min)
			{
				p->i = i;
				p->j = j;
				p->min = p->d[j][i];
			}
		}
	}
	if(p->min == 0x7fffffff)
		return 0;
	return 1;
}

static void DistanceIgnore(gsl_DISTANCE_TYPE *p)
{
	int i,j;
	for(i=0;i<POINT_MAX;i++)
		p->d[p->j][i] = 0x7fffffff;
	for(j=0;j<POINT_MAX;j++)
		p->d[j][p->i] = 0x7fffffff;
}

static int SpeedGet(int d)
{
	int i;
	for(i=8;i>0;i--)
	{
		if(d > 0x100<<i)
			break;
	}
	return i;
}

static void PointId(void)
{
	int i,j;
	gsl_DISTANCE_TYPE distance;
	unsigned int id_speed[POINT_MAX];
	DistanceInit(&distance);
	for(i=0;i<POINT_MAX;i++)
	{
		if (pp[0][i].other.predict == 0 || pp[1][i].other.fill != 0)
			id_speed[i] = id_first_coe;
		else
			id_speed[i] = SpeedGet( PointDistance(&pp[1][i],&pp[0][i]) );
	}
	for(i=0;i<POINT_MAX;i++)
	{
		if(pp[0][i].all == FLAG_COOR)
			continue;
		for(j=0;j<point_num && j<POINT_MAX;j++)
		{
			distance.d[j][i] = PointDistance(&point_now[j],&pp[0][i]);
		}
	}
	if(point_num == 0)
		return;
	if (global_state.other.only)
	{
		//do
		{
			if(DistanceMin(&distance))
			{
				if (pp[1][0].all != 0 &&
					pp[1][0].other.key !=
					point_now[distance.j].other.key)
				{
					DistanceIgnore(&distance);
					return;
					//continue;
				}
				pp[0][0].all = point_now[distance.j].all;
			}
			else
				pp[0][0].all = point_now[0].all;
			for(i=0;i<POINT_MAX;i++)
				point_now[i].all = 0;
		}
		//while(0);
		point_num = 1;
	}
	else
	{
		for (j=0;j<point_num && j<POINT_MAX;j++)
		{
			if (DistanceMin(&distance) == 0)
				break;
			if (distance.min >= (id_static_coe +
				id_speed[distance.i] * id_speed_coe) /**average/(soft_average+1)*/)
			{
				//point_now[distance.j].id = 0xf;//new id
				continue;
			}
			pp[0][distance.i].all = point_now[distance.j].all;
			pa[0][distance.i] = pressure_now[distance.j];
			point_now[distance.j].all = 0;
			DistanceIgnore(&distance);
		}
	}
}

static int ClearLenPP(int i)
{
	int n;
	for(n=0;n<PP_DEEP;n++)
	{
		if(pp[n][i].all)
			break;
	}
	return n;
}
static void PointNewId(void)
{
	int id,j;
	for(j=0;j<POINT_MAX;j++)
		if((pp[0][j].all & FLAG_COOR) == FLAG_COOR)
			pp[0][j].all = 0;
	for(j=0;j<POINT_MAX;j++)
	{
		if(point_now[j].all != 0)
		{
			if (point_now[j].other.able)
				continue;
			for(id=1;id<=POINT_MAX;id++)
			{
				if(ClearLenPP(id-1) > (int)(1+1))
				{
					pp[0][id-1].all = point_now[j].all;
					pa[0][id-1] = pressure_now[j];
					point_now[j].all = 0;
					break;
				}
			}
		}
	}
}

static void PointOrder(void)
{
	int i;
	for(i=0;i<POINT_MAX;i++)
	{
		if (pp[0][i].other.fill == 0)
			continue;
		if (pp[1][i].all == 0 || pp[1][i].other.fill != 0 || filter_able == 0 || filter_able == 1)
		{
			pp[0][i].all = 0;
			pressure_now[i] = 0;
		}
	}
}

static void PointCross(void)
{
	unsigned int i,j;
	unsigned int t;
	for(j=0;j<POINT_MAX;j++)
	{
		for(i=j+1;i<POINT_MAX;i++)
		{
			if(pp[0][i].all == 0 || pp[0][j].all == 0
			|| pp[1][i].all == 0 || pp[1][j].all == 0)
				continue;
			if (((pp[0][j].other.x < pp[0][i].other.x && pp[1][j].other.x > pp[1][i].other.x)
				|| (pp[0][j].other.x > pp[0][i].other.x && pp[1][j].other.x < pp[1][i].other.x))
				&& ((pp[0][j].other.y < pp[0][i].other.y && pp[1][j].other.y > pp[1][i].other.y)
				|| (pp[0][j].other.y > pp[0][i].other.y && pp[1][j].other.y < pp[1][i].other.y)))
			{
				t = pp[0][i].other.x;
				pp[0][i].other.x = pp[0][j].other.x;
				pp[0][j].other.x = t;
				t = pp[0][i].other.y;
				pp[0][i].other.y = pp[0][j].other.y;
				pp[0][j].other.y = t;
			}
		}
	}
}

static void GetPointNum(gsl_POINT_TYPE *pt)
{
	int i;
	point_num = 0;
	for(i=0;i<POINT_MAX;i++)
		if(pt[i].all != 0)
			point_num++;
}

static void PointDelay(void)
{
	int i,j;
	for(i=0;i<POINT_MAX;i++)
	{
		if(report_delay == 0)
		{//
			point_delay[i].all = 0;
			if(pp[0][i].all)
				point_delay[i].other.able = 1;
			continue;
		}
		if (pp[0][i].all != 0 &&
			point_delay[i].other.init == 0 &&
			point_delay[i].other.able == 0)
		{
			if(point_num == 0)
				continue;
			point_delay[i].other.delay = (report_delay >> 3 *
				((point_num>10 ? 10 : point_num) - 1)) & 0x7;
			point_delay[i].other.report = (report_ahead >> 3 *
				((point_num>10 ? 10 : point_num) - 1)) & 0x7;
			if (point_delay[i].other.report > point_delay[i].other.delay)
				point_delay[i].other.report = point_delay[i].other.delay;
			point_delay[i].other.init = 1;
		}
		if(pp[0][i].all == 0)
		{
			point_delay[i].other.init = 0;
		}
		if (point_delay[i].other.able == 0 && point_delay[i].other.init != 0)
		{
			for (j = 0; j <= (int)point_delay[i].other.delay; j++)
				if (pp[j][i].all == 0 ||
					pp[j][i].other.fill != 0 ||
					pp[j][i].other.able != 0)
					break;
			if (j <= (int)point_delay[i].other.delay)
				continue;
			point_delay[i].other.able = 1;
		}
		if (pp[point_delay[i].other.report][i].all == 0)
		{
			point_delay[i].other.able = 0;
			continue;
		}
		if (point_delay[i].other.able == 0)
			continue;
		if (point_delay[i].other.report)
		{
			if (PointDistance(&pp[point_delay[i].other.report][i],
				&pp[point_delay[i].other.report - 1][i]) < 3 * 3)
				point_delay[i].other.report--;
		}
	}
}

static void FilterOne(int i,int *ps_c,int *pr_c,int denominator)
{
	int j;
	int x=0,y=0;
	pr[0][i].all = ps[0][i].all;
	if(pr[0][i].all == 0)
		return;
	if(denominator <= 0)
		return;
	for(j=0;j<8;j++)
	{
		x += (int)pr[j][i].other.x * (int)pr_c[j] +
			(int)ps[j][i].other.x * (int)ps_c[j];
		y += (int)pr[j][i].other.y * (int)pr_c[j] +
			(int)ps[j][i].other.y * (int)ps_c[j];
	}
	x = (x + denominator/2) / denominator;
	y = (y + denominator/2) / denominator;
	if(x < 0)
		x = 0;
	if(x > 0xffff)
		x = 0xffff;
	if(y < 0)
		y = 0;
	if(y > 0xfff)
		y = 0xfff;
	pr[0][i].other.x = x;
	pr[0][i].other.y = y;
}

static unsigned int FilterSpeed(int i)
{
	return (Sqrt(PointDistance(&ps[0][i], &ps[1][i])) +
		Sqrt(PointDistance(&ps[1][i], &ps[2][i])))/2;
}

static int MedianSpeedOver(int id,int deep)
{
	int i;
	unsigned int dis;
	int speed_over = 0;
	deep = deep/2 - 1;
	if(deep < 0 || deep > 3)
		return TRUE;
	dis = median_dis[deep] * median_dis[deep];
	for(i=0;i<=deep && i<POINT_DEEP;i++)
	{
		if(PointDistance(&ps[i][id],&ps[i+1][id]) > dis)
			speed_over ++;
	}
	if(speed_over >= 2)
		return TRUE;
	return FALSE;
}

static void PointMedian(void)
{
	int i,j;
	int deep;
	int buf_x[PS_DEEP],buf_y[PS_DEEP];
	for(i=0;i<POINT_MAX;i++)
	{
		if(filter_deep[i] < 3)
			deep = 3;
		else
			deep = filter_deep[i] + 2;
		if(deep >= PS_DEEP)
			deep = PS_DEEP-1;
		deep |= 1;
		for(;deep>=3;deep-=2)
		{
			if(MedianSpeedOver(i,deep))
				continue;
			for(j=0;j<deep;j++)
			{
				buf_x[j] = ps[j][i].other.x;
				buf_y[j] = ps[j][i].other.y;
			}
			SortBubble(buf_x,deep);
			SortBubble(buf_y,deep);
			pr[0][i].other.x = buf_x[deep / 2];
			pr[0][i].other.y = buf_y[deep / 2];
		}
		filter_deep[i] = deep;
	}
}
static void PointFilter(void)
{
	int i,j;
	int speed_now;
	int filter_speed[6];
	int ps_c[8];
	int pr_c[8];
	for(i=0;i<POINT_MAX;i++)
	{
		pr[0][i].all = ps[0][i].all;
	}
	for(i=0;i<POINT_MAX;i++)
	{
		if(pr[0][i].all!=0 && pr[1][i].all == 0)
		{
			for(j=1;j<PR_DEEP;j++)
				pr[j][i].all = ps[0][i].all;
			for(j=1;j<PS_DEEP;j++)
				ps[j][i].all = ps[0][i].all;
		}
	}
	if(filter_able >=0 && filter_able <= 1)
		return;
	if(filter_able > 1)
	{
		for(i=0;i<8;i++)
		{
			ps_c[i] = (filter_coe[i/4] >> ((i%4)*8)) & 0xff;
			pr_c[i] = (filter_coe[i/4+2] >> ((i%4)*8)) & 0xff;
			if(ps_c[i] >= 0x80)
				ps_c[i] |= 0xffffff00;
			if(pr_c[i] >= 0x80)
				pr_c[i] |= 0xffffff00;
		}
		for(i=0;i<POINT_MAX;i++)
		{
			FilterOne(i,ps_c,pr_c,filter_able);
		}
	}
	else if(filter_able == -1)
	{
		PointMedian();
	}
	else if(filter_able < 0)
	{
		for(i=0;i<4;i++)
			filter_speed[i+1] = median_dis[i];
		filter_speed[0] = median_dis[0] * 2 - median_dis[1];
		filter_speed[5] = median_dis[3] /2;
		for(i=0;i<POINT_MAX;i++)
		{
 			if(pr[0][i].all == 0)
			{
				filter_deep[i] = 0;
				continue;
			}
			speed_now = FilterSpeed(i);
			if (filter_deep[i] > 0 &&
				speed_now > filter_speed[filter_deep[i]+1 - 2])
				filter_deep[i] --;
			else if(filter_deep[i] < 3 &&
				speed_now < filter_speed[filter_deep[i]+1 + 2])
				filter_deep[i] ++;

			FilterOne(i,ps_coe[filter_deep[i]],
				pr_coe[filter_deep[i]],0-filter_able);
		}
	}
}
static unsigned int KeyMap(int *drv,int *sen)
{
	typedef struct
	{
		unsigned int up_down,left_right;
		unsigned int coor;
	}KEY_TYPE_RANGE;
	KEY_TYPE_RANGE *key_range = (KEY_TYPE_RANGE * )key_range_array;
	int i;
	for(i=0;i<8;i++)
	{
		if ((unsigned int)*drv >= (key_range[i].up_down >> 16)
		&& (unsigned int)*drv <= (key_range[i].up_down & 0xffff)
		&& (unsigned int)*sen >= (key_range[i].left_right >> 16)
		&& (unsigned int)*sen <= (key_range[i].left_right & 0xffff))
		{
			*sen = key_range[i].coor >> 16;
			*drv = key_range[i].coor & 0xffff;
			return key_range[i].coor;
		}
	}
	return 0;
}

static unsigned int ScreenResolution(gsl_POINT_TYPE *p)
{
	int x,y;
	x = p->other.x;
	y = p->other.y;
	if (p->other.key == FALSE)
	{
		y = ((y - match_y[1]) * match_y[0] + 2048)/4096;
		x = ((x - match_x[1]) * match_x[0] + 2048)/4096 ;
	}
	y = y * (int)screen_y_max / ((int)sen_num_nokey * 64);
	x = x * (int)screen_x_max / ((int)drv_num_nokey * 64);
	if (p->other.key == FALSE)
	{
		if((ignore_y[0]!=0 || ignore_y[1]!=0))
		{
			if(y < ignore_y[0])
				return 0;
			if(ignore_y[1] <= screen_y_max/2 && y > screen_y_max - ignore_y[1])
				return 0;
			if(ignore_y[1] >= screen_y_max/2 && y > ignore_y[1])
				return 0;
		}
		if(ignore_x[0]!=0 || ignore_x[1]!=0)
		{
			if(x < ignore_x[0])
				return 0;
			if(ignore_x[1] <= screen_y_max/2 && x > screen_x_max - ignore_x[1])
				return 0;
			if(ignore_x[1] >= screen_y_max/2 && x > ignore_x[1])
				return 0;
		}
		if(y <= (int)edge_cut[2])
			y = (int)edge_cut[2] + 1;
		if(y >= screen_y_max - (int)edge_cut[3])
			y = screen_y_max - (int)edge_cut[3] - 1;
		if(x <= (int)edge_cut[0])
			x = (int)edge_cut[0] + 1;
		if(x >= screen_x_max - (int)edge_cut[1])
			x = screen_x_max - (int)edge_cut[1] - 1;
		if (global_flag.other.opposite_x)
			y = screen_y_max - y;
		if (global_flag.other.opposite_y)
			x = screen_x_max - x;
		if (global_flag.other.opposite_xy)
		{
			y ^= x;
			x ^= y;
			y ^= x;
		}
	}
	else
	{
		if(y < 0)
			y = 0;
		if(x < 0)
			x = 0;
		if((key_map_able & 0x1) != FALSE && KeyMap(&x,&y) == 0)
			return 0;
	}
	return ((y<<16) & 0x0fff0000) + (x & 0x0000ffff);
}

static void PointReport(struct gsl_touch_info *cinfo)
{
	int i;
	unsigned int data[POINT_MAX];
	int num = 0;
	if (point_num > point_num_max && global_flag.other.over_report_mask != 0)
	{
		point_num = 0;
		cinfo->finger_num = 0;
		return;
	}
	for(i=0;i<POINT_MAX;i++)
		data[i] = 0;
	num = 0;
	if (global_flag.other.id_over)
	{
		for(i=0;i<POINT_MAX && num<point_num_max;i++)
		{
			if (point_delay[i].other.able == 0)
				continue;
			if (point_delay[i].other.report >= PR_DEEP - 1)
				continue;
			if (pr[point_delay[i].other.report + 1][i].other.able == 0)
				continue;
			if (pr[point_delay[i].other.report][i].all)
			{
				pr[point_delay[i].other.report][i].other.able = 1;
				data[i] = ScreenResolution(&pr[point_delay[i].other.report][i]);
				if(data[i])
				{
					data[i] |= (i+1)<<28;
					num++;
				}
			}
		}
		for(i=0;i<POINT_MAX && num<point_num_max;i++)
		{
			if (point_delay[i].other.able == 0)
				continue;
			if (point_delay[i].other.report >= PR_DEEP)
				continue;
			if (pr[point_delay[i].other.report][i].all == 0)
				continue;
			if (pr[point_delay[i].other.report][i].other.able == 0)
			{
				pr[point_delay[i].other.report][i].other.able = 1;
				data[i] = ScreenResolution(&pr[point_delay[i].other.report][i]);
				if(data[i])
				{
					data[i] |= (i+1)<<28;
					num++;
				}
			}
		}
	}
	else
	{
		num=0;
		for(i=0;i<point_num_max;i++)
		{
			if (point_delay[i].other.able == 0)
				continue;
			if (point_delay[i].other.report >= PR_DEEP)
				continue;
			data[num] = ScreenResolution(&pr[point_delay[i].other.report][i]);
			if(data[num])
				data[num++] |= (i+1)<<28;
		}
	}
	num = 0;
	for(i=0;i<POINT_MAX;i++)
	{
		if(data[i] == 0)
			continue;
		point_now[num].all = data[i];
		cinfo->x[num] = (data[i] >> 16) & 0xfff;
		cinfo->y[num] = data[i] & 0xfff;
		cinfo->id[num] = data[i] >> 28;
		pressure_now[num] = pressure_report[i];
		num++;
	}
	point_num = num;
	cinfo->finger_num = num;
}



static void PointEdge(void)
{
	typedef struct
	{
		int range;
		int coe;
	}STRETCH_TYPE;
	typedef struct
	{
		STRETCH_TYPE up[4];
		STRETCH_TYPE down[4];
		STRETCH_TYPE left[4];
		STRETCH_TYPE right[4];
	}STRETCH_TYPE_ALL;
	STRETCH_TYPE_ALL *stretch;
	int i,id;
	int data[2];
	int x,y;
	int sac[4*4*2];
	if(screen_x_max == 0 || screen_y_max == 0)
		return;
	id = 0;
	for(i=0;i<4*4*2;i++)
	{
		sac[i] = stretch_array[i];
		if(sac[i])
			id++;
	}
	if(id == 0)
		return;
	stretch = (STRETCH_TYPE_ALL *)sac;
	for(i=0;i<4;i++)
	{
		if(stretch->right[i].range > screen_y_max * 64 / 128
		|| stretch->down [i].range > screen_x_max * 64 / 128)
		{
			for(i=0;i<4;i++)
			{
				if(stretch->up[i].range)
					stretch->up[i].range =
						stretch->up[i].range *
						drv_num_nokey * 64 / screen_x_max;
				if(stretch->down[i].range)
					stretch->down[i].range =
						(screen_x_max -
						stretch->down[i].range) *
						drv_num_nokey * 64 / screen_x_max;
				if(stretch->left[i].range)
					stretch->left[i].range =
						stretch->left[i].range *
						sen_num_nokey * 64 / screen_y_max;
				if(stretch->right[i].range)
					stretch->right[i].range =
						(screen_y_max -
						stretch->right[i].range) *
						sen_num_nokey * 64 / screen_y_max;
			}
			break;
		}
	}
	for(id=0;id<POINT_MAX;id++)
	{
		if (point_now[id].all == 0 || point_now[id].other.key != 0)
			continue;
		x = point_now[id].other.x;
		y = point_now[id].other.y;

		data[0] = 0;
		data[1] = y;
		for(i=0;i<4;i++)
		{
			if(stretch->left[i].range == 0)
				break;
			if(data[1] < stretch->left[i].range)
			{
				data[0] += (stretch->left[i].range - data[1]) *
					stretch->left[i].coe/128;
				data[1] = stretch->left[i].range;
			}
		}
		y = data[1] - data[0];
		if(y <= 0)
			y = 1;
		if(y >= (int)sen_num_nokey*64)
			y = sen_num_nokey*64 - 1;

		data[0] = 0;
		data[1] = sen_num_nokey * 64 - y;
		for(i=0;i<4;i++)
		{
			if(stretch->right[i].range == 0)
				break;
			if(data[1] < stretch->right[i].range)
			{
				data[0] += (stretch->right[i].range - data[1]) *
					stretch->right[i].coe/128;
				data[1] = stretch->right[i].range;
			}
		}
		y = sen_num_nokey * 64 - (data[1] - data[0]);
		if(y <= 0)
			y = 1;
		if(y >= (int)sen_num_nokey*64)
			y = sen_num_nokey*64 - 1;

		data[0] = 0;
		data[1] = x;
		for(i=0;i<4;i++)
		{
			if(stretch->up[i].range == 0)
				break;
			if(data[1] < stretch->up[i].range)
			{
				data[0] += (stretch->up[i].range - data[1]) *
					stretch->up[i].coe/128;
				data[1] = stretch->up[i].range;
			}
		}
		x = data[1] - data[0];
		if(x <= 0)
			x = 1;
		if(x >= (int)drv_num_nokey*64)
			x = drv_num_nokey*64 - 1;

		data[0] = 0;
		data[1] = drv_num_nokey * 64 - x;
		for(i=0;i<4;i++)
		{
			if(stretch->down[i].range == 0)
				break;
			if(data[1] < stretch->down[i].range)
			{
				data[0] += (stretch->down[i].range - data[1]) *
					stretch->down[i].coe/128;
				data[1] = stretch->down[i].range;
			}
		}
		x = drv_num_nokey * 64 - (data[1] - data[0]);
		if(x <= 0)
			x = 1;
		if(x >= (int)drv_num_nokey*64)
			x = drv_num_nokey*64 - 1;

		point_now[id].other.x = x;
		point_now[id].other.y = y;
	}
}

static void PointStretch(void)
{
	static int save_dr[POINT_MAX],save_dn[POINT_MAX];
	typedef struct
	{
		int dis;
		int coe;
	}SHAKE_TYPE;
	SHAKE_TYPE * shake_all = (SHAKE_TYPE *) shake_all_array;
	int i,j;
	int dn;
	int dr;
	int dc[9],ds[9];
	int len = 8;
	unsigned int temp;
	for(i=0;i<POINT_MAX;i++)
	{
		ps[0][i].all = pp[0][i].all;
	}
	for(i=0;i<POINT_MAX;i++)
	{
		if (pp[0][i].all == 0 || pp[0][i].other.key)
		{
			point_shake &= ~(0x1<<i);
			if(i == 0)
				point_edge.rate = 0;
			continue;
		}
		if(i == 0)
		{
			if(edge_first!=0 && ps[1][i].all == 0)
			{
				point_edge.coor.all = ps[0][i].all;
				if (point_edge.coor.other.x < (unsigned int)((edge_first >> 24) & 0xff))
					point_edge.coor.other.x = ((edge_first >> 24) & 0xff);
				if (point_edge.coor.other.x > drv_num_nokey * 64 - ((edge_first >> 16) & 0xff))
					point_edge.coor.other.x = drv_num_nokey * 64 - ((edge_first >> 16) & 0xff);
				if (point_edge.coor.other.y < (unsigned int)((edge_first >> 8) & 0xff))
					point_edge.coor.other.y = ((edge_first >> 8) & 0xff);
				if (point_edge.coor.other.y > sen_num_nokey * 64 - ((edge_first >> 0) & 0xff))
					point_edge.coor.other.y = sen_num_nokey * 64 - ((edge_first >> 0) & 0xff);
				if(point_edge.coor.all != ps[0][i].all)
				{
					point_edge.dis = PointDistance(&ps[0][i],&point_edge.coor);
					if(point_edge.dis)
						point_edge.rate = 0x1000;
				}
			}
			if(point_edge.rate!=0 && point_edge.dis!=0)
			{
				temp = PointDistance(&ps[0][i],&point_edge.coor);
				if(temp >= point_edge.dis * edge_first_coe / 0x80)
				{
					point_edge.rate = 0;
				}
				else if(temp > point_edge.dis)
				{
					temp = (point_edge.dis * edge_first_coe / 0x80 - temp) *
						0x1000 / point_edge.dis;
					if(temp < point_edge.rate)
						point_edge.rate = temp;
				}
				ps[0][i].other.x = point_edge.coor.other.x +
					(ps[0][i].other.x - point_edge.coor.other.x) *
					(0x1000 - point_edge.rate) / 0x1000;
				ps[0][i].other.y = point_edge.coor.other.y +
					(ps[0][i].other.y - point_edge.coor.other.y) *
					(0x1000 - point_edge.rate) / 0x1000;
			}
		}
		if(ps[1][i].all == 0)
		{
			continue;
		}
		else if((point_shake & (0x1<<i)) == 0)
		{
			if(PointDistance(&ps[0][i],&ps[1][i]) < (unsigned int)shake_min)
			{
				ps[0][i].all = ps[1][i].all;
				continue;
			}
			else
				point_shake |= (0x1<<i);
		}
	}
	for(i=0;i<len;i++)
	{
		if(shake_all[i].dis == 0)
		{
			len=i;
			break;
		}
	}
	if(len == 1)
	{
		ds[0] = shake_all[0].dis;
		dc[0] = (shake_all[0].coe*100+64)/128;
		for(i=0;i<POINT_MAX;i++)
		{
			if(ps[1][i].all == 0)
			{
				for(j=1;j<PS_DEEP;j++)
					ps[j][i].all = ps[0][i].all;
				continue;
			}
			if((point_shake & (0x1<<i)) == 0)
				continue;
			dn = PointDistance(&pp[0][i],&ps[1][i]);
			dn = Sqrt(dn);
			dr = dn>ds[0] ? dn-ds[0] : 0;
			temp = ps[0][i].all;
			if(dn == 0 || dr == 0)
			{
				ps[0][i].other.x = ps[1][i].other.x;
				ps[0][i].other.y = ps[1][i].other.y;
			}
			else
			{
				ps[0][i].other.x = (int)ps[1][i].other.x +
					((int)pp[0][i].other.x -
					(int)ps[1][i].other.x) * dr / dn;
				ps[0][i].other.y = (int)ps[1][i].other.y +
					((int)pp[0][i].other.y -
					(int)ps[1][i].other.y) * dr / dn;
			}
			if(dc[0] > 0)
			{
				if(ps[0][i].all == ps[1][i].all && temp != ps[0][i].all)
				{
					ps[0][i].all = temp;
					point_decimal[i].other.x +=
						(short)ps[0][i].other.x -
						(short)ps[1][i].other.x;
					point_decimal[i].other.y +=
						(short)ps[0][i].other.y -
						(short)ps[1][i].other.y;
					ps[0][i].other.x = ps[1][i].other.x;
					ps[0][i].other.y = ps[1][i].other.y;
					if (point_decimal[i].other.x >  dc[0] && ps[1][i].other.x < 0xffff)
					{
						ps[0][i].other.x += 1;
						point_decimal[i].other.x = 0;
					}
					if (point_decimal[i].other.x  < -dc[0] && ps[1][i].other.x > 0)
					{
						ps[0][i].other.x -= 1;
						point_decimal[i].other.x = 0;
					}
					if (point_decimal[i].other.y >  dc[0] && ps[1][i].other.y < 0xffff)
					{
						ps[0][i].other.y += 1;
						point_decimal[i].other.y = 0;
					}
					if (point_decimal[i].other.y  < -dc[0] && ps[1][i].other.y > 0)
					{
						ps[0][i].other.y -= 1;
						point_decimal[i].other.y = 0;
					}
				}
				else
				{
					point_decimal[i].other.x = 0;
					point_decimal[i].other.y = 0;
				}
			}
		}

	}
	else if(len >= 2)
	{
		for(i=0;i<8 && i<len;i++)
		{
			ds[i+1] = shake_all[i].dis;
			dc[i+1] = shake_all[i].coe;//;ds[i+1] * shake_all[i].coe;
		}
		if(shake_all[0].coe >= 128 || shake_all[0].coe <= shake_all[1].coe)
		{
			ds[0] = ds[1];
			dc[0] = dc[1];
		}
		else
		{
			ds[0] = ds[1] + (128 - shake_all[0].coe) *
				(ds[1]-ds[2])/(shake_all[0].coe - shake_all[1].coe);
			dc[0] = 128;
		}
		for(i=0;i<POINT_MAX;i++)
		{
			if(ps[1][i].all == 0)
			{
				for(j=1;j<PS_DEEP;j++)
					ps[j][i].all = ps[0][i].all;
				save_dr[i] = 128;
				save_dn[i] = 0;
				continue;
			}
			if((point_shake & (0x1<<i)) == 0)
				continue;
			dn = PointDistance(&pp[0][i],&ps[1][i]);
			dn = Sqrt(dn);
			if(dn >= ds[0])
			{
				continue;
			}
			if(dn < save_dn[i])
			{
				dr = save_dr[i];
				save_dn[i] = dn;
				ps[0][i].other.x = (int)ps[1][i].other.x +
					(((int)pp[0][i].other.x -
					(int)ps[1][i].other.x) * dr) / 128;
				ps[0][i].other.y = (int)ps[1][i].other.y +
					(((int)pp[0][i].other.y -
					(int)ps[1][i].other.y) * dr) / 128;
				continue;
			}
			for(j=0;j<=len;j++)
			{
				if(j == len || dn == 0)
				{
					ps[0][i].other.x = ps[1][i].other.x;
					ps[0][i].other.y = ps[1][i].other.y;
					break;
				}
				else if(ds[j] > dn && dn >=ds[j+1])
				{
					dr = dc[j+1] + ((dn - ds[j+1]) * (dc[j] - dc[j+1])) / (ds[j] - ds[j+1]);
					save_dr[i] = dr;
					save_dn[i] = dn;
//					ps[0][i].x = (int)ps[1][i].x + ((int)pp[0][i].x - (int)ps[1][i].x) * dr / dn / 128;
//					ps[0][i].y = (int)ps[1][i].y + ((int)pp[0][i].y - (int)ps[1][i].y) * dr / dn / 128;
					ps[0][i].other.x = (int)ps[1][i].other.x +
						(((int)pp[0][i].other.x -
						(int)ps[1][i].other.x) * dr + 64) / 128;
					ps[0][i].other.y = (int)ps[1][i].other.y +
						(((int)pp[0][i].other.y -
						(int)ps[1][i].other.y) * dr + 64) / 128;
					break;
				}
			}
		}
	}
	else
	{
		return;
	}
}

static void ResetMask(void)
{
	if (reset_mask_send)
	{
		reset_mask_send = 0;
	}
	if (global_state.other.mask)
		return;
	if (reset_mask_dis ==0 || reset_mask_type == 0)
		return;
	if (reset_mask_max == 0xfffffff1)
	{
		if (point_num == 0)
			reset_mask_max = 0xf0000000 + 1;
		return;
	}
	if (reset_mask_max >  0xf0000000)
	{
		reset_mask_max --;
		if (reset_mask_max == 0xf0000000)
		{
			reset_mask_send = reset_mask_type;
			global_state.other.mask = 1;
		}
		return;
	}
	if (point_num > 1 || pp[0][0].all == 0)
	{
		reset_mask_count = 0;
		reset_mask_max = 0;
		reset_mask_count = 0;
		return;
	}
	reset_mask_count ++;
	if (reset_mask_max == 0)
		reset_mask_max = pp[0][0].all;
	else
		if (PointDistance((gsl_POINT_TYPE*)(&reset_mask_max),pp[0]) >
			(((unsigned int)reset_mask_dis) & 0xffffff) &&
			reset_mask_count > (((unsigned int)reset_mask_dis) >> 24))
			reset_mask_max = 0xfffffff1;
}

static int ConfigCoorMulti(int data[])
{
	int i,j;
	int n = 0;
	for(i=0;i<4;i++)
	{
		if(data[247+i]!=0)
		{
			if((data[247+i]&63)==0 && (data[247+i]>>16)<4)
				n++;
			else
				return FALSE;
		}
		if(data[251+i]!=0)
		{
			if((data[251+i]&63)==0 && (data[251+i]>>16)<4)
				n++;
			else
				return FALSE;
		}
	}
	if(n == 0 || n > 4)
		return FALSE;
	for(j=0;j<n;j++)
	{
		for(i=0;i<64;i++)
		{
			if(data[256+j*64+i] >= 64)
				return FALSE;
			if(i)
			{
				if(data[256+j*64+i] < data[256+j*64+i-1])
					return FALSE;
			}
		}
	}
	return TRUE;
}

static int ConfigFilter(unsigned int data[])
{
	int i;
	unsigned int ps_c[8];
	unsigned int pr_c[8];
	unsigned int sum = 0;
	//if(data[242]>1 && (data[255]>=0 && data[255]<=256))
	if (data[242]>1 && (data[255] <= 256))
	{
		for(i=0;i<8;i++)
		{
			ps_c[i] = (data[243+i/4] >> ((i%4)*8)) & 0xff;
			pr_c[i] = (data[243+i/4+2] >> ((i%4)*8)) & 0xff;
			if(ps_c[i] >= 0x80)
				ps_c[i] |= 0xffffff00;
			if(pr_c[i] >= 0x80)
				pr_c[i] |= 0xffffff00;
			sum += ps_c[i];
			sum += pr_c[i];
		}
		if(sum == data[242] || sum + data[242] == 0)
			return TRUE;
	}
	return FALSE;
}

static int ConfigKeyMap(int data[])
{
	int i;
	if(data[217] != 1)
		return FALSE;
	for(i=0;i<8;i++)
	{
		if(data[218+2] == 0)
			return FALSE;
		if((data[218+i*3+0]>>16) > (data[218+i*3+0]&0xffff))
			return FALSE;
		if((data[218+i*3+1]>>16) > (data[218+i*3+1]&0xffff))
			return FALSE;
	}
	return TRUE;
}

static int DiagonalDistance(gsl_POINT_TYPE *p,int type)
{
	int divisor,square;
	divisor = ((int)sen_num_nokey * (int)sen_num_nokey +
		(int)drv_num_nokey * (int)drv_num_nokey)/16;
	if(divisor == 0)
		divisor = 1;
	if(type == 0)
		square = ((int)sen_num_nokey*(int)(p->other.x) -
			(int)drv_num_nokey*(int)(p->other.y)) / 4;
	else
		square = ((int)sen_num_nokey*(int)(p->other.x) +
			(int)drv_num_nokey*(int)(p->other.y) -
			(int)sen_num_nokey*(int)drv_num_nokey * 64) / 4;
	return square * square / divisor;
}

static void DiagonalCompress(gsl_POINT_TYPE *p,int type,int dis,int dis_max)
{
	int x,y;
	int tx,ty;
	int cp_ceof;
	if(dis_max == 0)
		return;
	if(dis > dis_max)
		cp_ceof = (dis - dis_max)*128/(3*dis_max) + 128;
	else
		cp_ceof = 128;
	if(cp_ceof > 256)
		cp_ceof = 256;
	x = p->other.x;
	y = p->other.y;
	if(type)
		y = (int)sen_num_nokey*64 - y;
	x *= (int)sen_num_nokey;
	y *= (int)drv_num_nokey;
	tx = x;
	ty = y;
	x = ((tx+ty)+(tx-ty)*cp_ceof/256)/2;
	y = ((tx+ty)+(ty-tx)*cp_ceof/256)/2;
	x /= (int)sen_num_nokey;
	y /= (int)drv_num_nokey;
	if(type)
		y = sen_num_nokey*64 - y;
	if(x < 1)
		x = 1;
	if(y < 1)
		y = 1;
	if(x >= (int)drv_num_nokey*64)
		x = drv_num_nokey*64 - 1;
	if(y >= (int)sen_num_nokey*64)
		y = (int)sen_num_nokey*64 - 1;
	p->other.x = x;
	p->other.y = y;
}

static void PointDiagonal(void)
{
	int i;
	int diagonal_size;
	int dis;
	unsigned int diagonal_start;
	if(diagonal == 0)
		return;
	diagonal_size = diagonal * diagonal;
	diagonal_start = diagonal * 3/2;
	for(i=0;i<POINT_MAX;i++)
	{
		if (ps[0][i].all == 0 || ps[0][i].other.key != 0)
		{
			point_corner &= ~(0x3<<i*2);
			continue;
		}
		else if((point_corner & (0x3<<i*2)) == 0)
		{
			if ((ps[0][i].other.x <= diagonal_start &&
				ps[0][i].other.y <= diagonal_start) ||
				(ps[0][i].other.x >= drv_num_nokey * 64 - diagonal_start &&
				 ps[0][i].other.y >= sen_num_nokey * 64 - diagonal_start))
				point_corner |= 0x2<<i*2;
			else if ((ps[0][i].other.x <= diagonal_start &&
				ps[0][i].other.y >= sen_num_nokey * 64 - diagonal_start) ||
				(ps[0][i].other.x >= drv_num_nokey * 64 - diagonal_start &&
				ps[0][i].other.y <= diagonal_start))
				point_corner |= 0x3<<i*2;
			else
				point_corner |= 0x1<<i*2;
		}
		if(point_corner & (0x2<<i*2))
		{
			dis = DiagonalDistance(&(ps[0][i]),point_corner & (0x1<<i*2));
			if(dis <= diagonal_size*4)
			{
				DiagonalCompress(&(ps[0][i]),point_corner & (0x1<<i*2),dis,diagonal_size);
			}
			else if(dis > diagonal_size*4)
			{
				point_corner &= ~(0x3<<i*2);
				point_corner |= 0x1<<i*2;
			}
		}
	}
}

static void PressureSave(void)
{
	int i;
	if((point_num & 0x1000)==0)
	{
		return;
	}
	for(i=0;i<POINT_MAX;i++)
	{
		pressure_now[i] = point_now[i].all >> 28;
		point_now[i].all &= ~(0xf<<28);
	}
}

static void PointPressure(void)
{
	int i,j;
	for(i=0;i<POINT_MAX;i++)
	{
		if(pa[0][i]!=0 && pa[1][i]==0)
		{
			pressure_report[i] = pa[0][i]*5;
			for(j=1;j<PRESSURE_DEEP;j++)
				pa[j][i] = pa[0][i];
			continue;
		}
		j = (pressure_report[i]+1)/2 + pa[0][i] +
			pa[1][i] + (pa[2][i]+1)/2 - pressure_report[i];
		if(j >= 2)
			j -= 2;
		else if(j <= -2)
			j += 2;
		else
			j = 0;
		pressure_report[i] = pressure_report[i]+j;
	}
}

void gsl_ReportPressure(unsigned int *p)
{
	int i;
	for(i=0;i<POINT_MAX;i++)
	{
		if(i < point_num)
		{
			if(pressure_now[i] == 0)
				p[i] = 0;
			else if(pressure_now[i] <= 7)
				p[i] = 1;
			else if(pressure_now[i] > 63+7)
				p[i] = 63;
			else
				p[i] = pressure_now[i] - 7;
		}
		else
			p[i] = 0;
	}
}
//EXPORT_SYMBOL(gsl_ReportPressure);

int  gsl_TouchNear(void)
{
		return 0;
}
//EXPORT_SYMBOL(gsl_TouchNear);

static void DoubleClick(void)
{
	int i;
	unsigned int width[3];
	double_click = 0;
//	printk("sileadinc DoubleClick c = %08x , %08x\n",csensor_count,pp[0][0].all);
	if (point_num >= 2 || (point_num == 1 && pp[0][0].all == 0) || pp[0][0].other.key)
	{
//		printk("sileadinc DoubleClick return\n");
		for(i=0;i<sizeof(click_count)/sizeof(click_count[0]);i++)
			click_count[i] = 0;
		return;
	}
	if(point_num!=0 && prev_num==0)
	{
		for(i=sizeof(click_count)/sizeof(click_count[0])-1;i>0;i--)
			click_count[i] = click_count[i-1];
		click_count[0] = csensor_count;
		for(i=sizeof(point_click)/sizeof(point_click[0])-1;i>1;i--)
			point_click[i].all = point_click[i-2].all;
		point_click[0].all = pp[0][0].all;
		point_click[1].all = pp[0][0].all;
	}
	if(point_num!=0 && prev_num!=0)
	{
		if(PointDistance(&point_click[1],&pp[0][0]) > PointDistance(&point_click[1],&point_click[0]))
			point_click[0].all = pp[0][0].all;
	}
	if(point_num==0 && prev_num!=0)
	{
//		printk("sileadinc DoubleClick point_click %08x  %08x %08x %08x\n",point_click[0],point_click[1],point_click[2],point_click[3]);
		for(i=sizeof(click_count)/sizeof(click_count[0])-1;i>0;i--)
			click_count[i] = click_count[i-1];
		click_count[0] = csensor_count;
		for(i=0;i<sizeof(click_count)/sizeof(click_count[0])-1;i++)
			width[i] = (click_count[i] - click_count[i+1]) & 0xffff;
		if(!(width[0]>=double_down*average && width[2]>=double_down*average && width[1]<=double_up*average))
		{
//			printk("sileadinc DoubleClick width %08x %08x %08x\n",width[0],width[1],width[2]);
			return;
		}
		if(PointDistance(&point_click[0],&point_click[1]) > 64*64
		|| PointDistance(&point_click[2],&point_click[3]) > 64*64
		|| PointDistance(&point_click[1],&point_click[3]) > 128*128)
		{
//			printk("sileadinc DoubleClick distance %08x %08x %08x\n",
//			PointDistance(&point_click[0],&point_click[1]),
//			PointDistance(&point_click[2],&point_click[3]),
//			PointDistance(&point_click[1],&point_click[3]));
			return;
		}
		for(i=0;i<sizeof(click_count)/sizeof(click_count[0]);i++)
			click_count[i] = 0;//?point_click
		double_click = '*';
//		printk("sileadinc DoubleClick succeed double_click=%c\n",double_click);
	}
}

static void gsl_id_reg_init(int flag)
{
	int i,j;
	for(j=0;j<POINT_DEEP;j++)
		for(i=0;i<POINT_MAX;i++)
			point_array[j][i].all = 0;
	for(j=0;j<PRESSURE_DEEP;j++)
		for(i=0;i<POINT_MAX;i++)
			pressure_array[j][i] = 0;
	for(i=0;i<POINT_MAX;i++)
	{
		point_delay[i].all = 0;
		filter_deep[i] = 0;
		point_decimal[i].all = 0;
	}
	point_edge.rate = 0;
	point_n = 0;
	if(flag)
		point_num = 0;
	prev_num = 0;
	point_shake = 0;
	reset_mask_send = 0;
	reset_mask_max = 0;
	reset_mask_count = 0;
	point_near = 0;
	point_corner = 0;
	global_state.all = 0;
	double_click = 0;
	inte_count = 0;
	csensor_count = 0;
#ifdef GESTURE_LICH
	GestureInit();
#endif
}

static int DataCheck(void)
{
	if(drv_num==0 || drv_num_nokey==0 || sen_num==0 || sen_num_nokey==0)
		return 0;
	if(screen_x_max==0 || screen_y_max==0)
		return 0;
	return 1;
}

void gsl_DataInit(unsigned int * conf_in)
{
	int i, j;
	unsigned int *conf;
	int len;
	gsl_id_reg_init(1);
	conf = config_static;
	coordinate_correct_able = 0;
	for(i=0;i<32;i++)
	{
		coordinate_correct_coe_x[i] = i;
		coordinate_correct_coe_y[i] = i;
	}
	id_first_coe = 8;
	id_speed_coe = 128*128;
	id_static_coe = 64*64;
	average = 3+1;
	soft_average = 3;
	report_delay=0;
	report_ahead = 0x9249249;
	for(i=0;i<4;i++)
		median_dis[i]=0;
	shake_min = 0*0;
	for(i=0;i<2;i++)
	{
		match_y[i]=0;
		match_x[i]=0;
		ignore_y[i]=0;
		ignore_x[i]=0;
	}
	match_y[0]=4096;
	match_x[0]=4096;
	screen_y_max = 480;
	screen_x_max = 800;
	point_num_max=10;
	drv_num = 16;
	sen_num = 10;
	drv_num_nokey = 16;
	sen_num_nokey = 10;
	for(i=0;i<4;i++)
		edge_cut[i] = 0;
	for(i=0;i<32;i++)
		stretch_array[i] = 0;
	for(i=0;i<16;i++)
		shake_all_array[i] = 0;
	reset_mask_dis = 0;
	reset_mask_type=0;
	diagonal = 0;
	key_map_able = 0;
	for(i=0;i<8*3;i++)
		key_range_array[i] = 0;
	filter_able = 0;
	filter_coe[0] = ( 0<<6*4)+( 0<<6*3)+( 0<<6*2)+(40<<6*1)+(24<<6*0);
	filter_coe[1] = ( 0<<6*4)+( 0<<6*3)+(16<<6*2)+(24<<6*1)+(24<<6*0);
	filter_coe[2] = ( 0<<6*4)+(16<<6*3)+(24<<6*2)+(16<<6*1)+( 8<<6*0);
	filter_coe[3] = ( 6<<6*4)+(16<<6*3)+(24<<6*2)+(12<<6*1)+( 6<<6*0);
	for(i=0;i<4;i++)
	{
		multi_x_array[i]=0;
		multi_y_array[i]=0;
	}
	point_repeat[0] = 32;
	point_repeat[1] = 96;
	edge_first = 0;
	edge_first_coe = 0x80;
	//----------------------------------------------
	if(conf_in == NULL)
	{
		return;
	}
	if(conf_in[0] <= 0xfff)
	{
		if(ConfigCoorMulti((int*)conf_in))
			len = 512;
		else if(ConfigFilter(conf_in))
			len = 256;
		else if (ConfigKeyMap((int*)conf_in))
			len = 241;
		else
			len = 215;
	}
	else if(conf_in[1] <= CONFIG_LENGTH)
		len = conf_in[1];
	else
		len = CONFIG_LENGTH;
	for(i=0;i<len;i++)
		conf[i] = conf_in[i];
	for(;i<CONFIG_LENGTH;i++)
		conf[i] = 0;
	if(conf_in[0] <= 0xfff)
	{
		coordinate_correct_able = conf[0];
		drv_num = conf[1];
		sen_num = conf[2];
		drv_num_nokey = conf[3];
		sen_num_nokey = conf[4];
		id_first_coe = conf[5];
		id_speed_coe = conf[6];
		id_static_coe = conf[7];
		average = conf[8];
		soft_average = conf[9];

		report_delay = conf[13];
		shake_min = conf[14];
		screen_y_max = conf[15];
		screen_x_max = conf[16];
		point_num_max = conf[17];
		global_flag.all = conf[18];
		for(i=0;i<4;i++)
			median_dis[i] = (unsigned char)conf[19+i];
		for(i=0;i<2;i++)
		{
			match_y[i] = conf[23+i];
			match_x[i] = conf[25+i];
			ignore_y[i] = conf[27+i];
			ignore_x[i] = conf[29+i];
		}
		for(i=0;i<64;i++)
		{
			coordinate_correct_coe_x[i] = conf[31+i];
			coordinate_correct_coe_y[i] = conf[95+i];
		}
		for(i=0;i<4;i++)
			edge_cut[i] = conf[159+i];
		for(i=0;i<32;i++)
			stretch_array[i] = conf[163+i];
		for(i=0;i<16;i++)
			shake_all_array[i] = conf[195+i];
		reset_mask_dis = conf[213];
		reset_mask_type = conf[214];
		key_map_able = conf[217];
		for(i=0;i<8*3;i++)
			key_range_array[i] = conf[218+i];
		filter_able = conf[242];
		for(i=0;i<4;i++)
			filter_coe[i] = conf[243+i];
		for(i=0;i<4;i++)
			multi_x_array[i] = conf[247+i];
		for(i=0;i<4;i++)
			multi_y_array[i] = conf[251+i];
		diagonal = conf[255];
		for(i=0;i<4;i++)
		  for(j=0;j<64;j++)
			  multi_group[i][j] = conf[256+64*i+j];
		for(i=0;i<4;i++)
		{
		  for(j=0;j<8;j++)
		  {
			  ps_coe[i][j] = conf[256 + 64*3 + 8*i+j];
			  pr_coe[i][j] = conf[256 + 64*3 + 8*i+j + 32];
			}
		}
		//-----------------------
		near_set[0] = 0;
		near_set[1] = 0;
	}
	else
	{
		global_flag.all = conf[0x10];
		point_num_max = conf[0x11];
		drv_num = conf[0x12]&0xffff;
		sen_num = conf[0x12]>>16;
		drv_num_nokey = conf[0x13]&0xffff;
		sen_num_nokey = conf[0x13]>>16;
		screen_x_max = conf[0x14]&0xffff;
		screen_y_max = conf[0x14]>>16;
		average = conf[0x15];
		reset_mask_dis = conf[0x16];
		reset_mask_type = conf[0x17];
		point_repeat[0] = conf[0x18]>>16;
		point_repeat[1] = conf[0x18]&0xffff;
		//conf[0x19~0x1f]
		near_set[0] = conf[0x19]>>16;
		near_set[1] = conf[0x19]&0xffff;
		diagonal = conf[0x1a];
		//-------------------------

		id_first_coe = conf[0x20];
		id_speed_coe = conf[0x21];
		id_static_coe = conf[0x22];
		match_y[0] = conf[0x23]>>16;
		match_y[1] = conf[0x23]&0xffff;
		match_x[0] = conf[0x24]>>16;
		match_x[1] = conf[0x24]&0xffff;
		ignore_y[0] = conf[0x25]>>16;
		ignore_y[1] = conf[0x25]&0xffff;
		ignore_x[0] = conf[0x26]>>16;
		ignore_x[1] = conf[0x26]&0xffff;
		edge_cut[0] = (conf[0x27]>>24) & 0xff;
		edge_cut[1] = (conf[0x27]>>16) & 0xff;
		edge_cut[2] = (conf[0x27]>> 8) & 0xff;
		edge_cut[3] = (conf[0x27]>> 0) & 0xff;
		report_delay = conf[0x28];
		shake_min = conf[0x29];
		for(i=0;i<16;i++)
		{
			stretch_array[i*2+0] = conf[0x2a+i] & 0xffff;
			stretch_array[i*2+1] = conf[0x2a+i] >> 16;
		}
		for(i=0;i<8;i++)
		{
			shake_all_array[i*2+0] = conf[0x3a+i] & 0xffff;
			shake_all_array[i*2+1] = conf[0x3a+i] >> 16;
		}
		report_ahead			= conf[0x42];
// 		key_dead_time			= conf[0x43];
// 		point_dead_time			= conf[0x44];
// 		point_dead_time2		= conf[0x45];
// 		point_dead_distance		= conf[0x46];
// 		point_dead_distance2	= conf[0x47];
		edge_first				= conf[0x48];
		edge_first_coe			= conf[0x49];
		//goto_test

		key_map_able = conf[0x60];
		for(i=0;i<8*3;i++)
			key_range_array[i] = conf[0x61+i];

		coordinate_correct_able = conf[0x100];
		for(i=0;i<4;i++)
		{
			multi_x_array[i] = conf[0x101+i];
			multi_y_array[i] = conf[0x105+i];
		}
		for(i=0;i<64;i++)
		{
			coordinate_correct_coe_x[i] = (conf[0x109+i/4]>>(i%4*8)) & 0xff;
			coordinate_correct_coe_y[i] = (conf[0x109+64/4+i/4]>>(i%4*8)) & 0xff;
		}
		for(i=0;i<4;i++)
		{
		  for(j=0;j<64;j++)
			  multi_group[i][j] = (conf[0x109+64/4*2+(64*i+j)/4]>>((64*i+j)%4*8)) & 0xff;
		}

		filter_able = conf[0x180];
		for(i=0;i<4;i++)
			filter_coe[i] = conf[0x181+i];
		for(i=0;i<4;i++)
			median_dis[i] = (unsigned char)conf[0x185+i];
		for(i=0;i<4;i++)
		{
		    for(j=0;j<8;j++)
		    {
			    ps_coe[i][j] = conf[0x189 + 8*i+j];
			    pr_coe[i][j] = conf[0x189 + 8*i+j + 32];
		    }
		}
#ifdef GESTURE_LICH
		GestureSet(&conf[0x189 + 64]);
#endif
	}
	//---------------------------------------------
	if(average == 0)
		average = 4;
	for(i=0;i<8;i++)
	{
		if(shake_all_array[i*2] & 0x8000)
			shake_all_array[i*2] = shake_all_array[i*2] & ~0x8000;
		else
			shake_all_array[i*2] = Sqrt(shake_all_array[i*2]);
	}
	for(i=0;i<2;i++)
	{
		if(match_x[i] & 0x8000)
			match_x[i] |= 0xffff0000;
		if(match_y[i] & 0x8000)
			match_y[i] |= 0xffff0000;
		if(ignore_x[i] & 0x8000)
			ignore_x[i] |= 0xffff0000;
		if(ignore_y[i] & 0x8000)
			ignore_y[i] |= 0xffff0000;
	}
	for(i=0;i<CONFIG_LENGTH;i++)
		config_static[i] = 0;
}
//EXPORT_SYMBOL(gsl_DataInit);

unsigned int gsl_version_id(void)
{
	return GSL_VERSION;
}
//EXPORT_SYMBOL(gsl_version_id);

unsigned int gsl_mask_tiaoping(void)
{
	return reset_mask_send;
}
//EXPORT_SYMBOL(gsl_mask_tiaoping);


static void GetFlag(void)
{
	int i = 0;
	int num_save;
	if(((point_num & 0x100)!=0) ||
		((point_num & 0x200) != 0 &&
		global_state.other.reset == 1))
	{
		gsl_id_reg_init(0);
	}
	if((point_num & 0x300) == 0)
	{
		global_state.other.reset = 1;
	}
	if(point_num & 0x400)
		global_state.other.only = 1;
	else
		global_state.other.only = 0;
	if(point_num & 0x2000)
		global_state.other.interpolation = 0xf;
	else if (global_state.other.interpolation)
		global_state.other.interpolation--;
	if(point_num & 0x4000)
		global_state.other.ex = 1;
	else
		global_state.other.ex = 0;
	inte_count ++;
	csensor_count = ((unsigned int)point_num)>>16;
	num_save = point_num & 0xff;
	if(num_save > POINT_MAX)
		num_save = POINT_MAX;
	for(i=0;i<POINT_MAX;i++)
	{
		if(i >= num_save)
			point_now[i].all = 0;
	}
	point_num = (point_num & (~0xff)) + num_save;
}

void gsl_alg_id_main(struct gsl_touch_info *cinfo)
{
	int i;
	point_num = cinfo->finger_num;
	for(i=0;i<POINT_MAX;i++)
	{
		point_now[i].all = (cinfo->id[i]<<28) |
			(cinfo->x[i]<<16) | cinfo->y[i];
	}

	GetFlag();
	if(DataCheck() == 0)
	{
		point_num = 0;
		cinfo->finger_num = 0;
		return;
	}
	PressureSave();
	PointCoor();
	CoordinateCorrect();
	PointEdge();
	PointRepeat();
	GetPointNum(point_now);
	PointPointer();
	PointPredict();
	PointId();
	PointNewId();
	PointOrder();
	PointCross();
	GetPointNum(pp[0]);
	DoubleClick();
	prev_num = point_num;
	ResetMask();
	PointStretch();
	PointDiagonal();
 	PointFilter();
	GetPointNum(pr[0]);
#ifdef GESTURE_LICH
	GestureMain(&(pr[0][0].all),point_num);
#endif
	PointDelay();
	PointPressure();
	PointReport(cinfo);
}
//EXPORT_SYMBOL(gsl_alg_id_main);


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#ifdef GESTURE_LICH

int gsl_obtain_gesture(void)
{
	return GestureDeal();
}
//EXPORT_SYMBOL(gsl_obtain_gesture);

static int GestureMain(unsigned int data_coor[],unsigned int num)
{
	gesture_deal = FALSE;
	if(gesture_dis_min == 0)
		return FALSE;
	if(num == 0)
	{
		if(gesture_num == 0)
			return FALSE;
		if(gesture_num <= 8)
		{
			GestureInit();
			return FALSE;
		}
		gesture_deal = GESTURE_ALL;
		return TRUE;
	}
	else if(gesture_num < 0)
	{
		return FALSE;
	}
	else if(num == 1 && data_coor[0] != 0)
	{
		GesturePush((GESTURE_POINT_TYPE*) data_coor);
		return FALSE;
	}
	else// if(num > 1)
	{
		gesture_num = -1;
		return FALSE;
	}
//	return TRUE;
}

static int GestureSqrt(int d)
{
	int ret = 0;
	int i;
	for(i=14;i>=0;i--)
	{
		if((ret + (0x1<<i))*(ret + (0x1<<i)) <= d)
			ret |= (0x1<<i);
	}
	return ret;
}

static int GestureDistance(GESTURE_POINT_TYPE* d1,GESTURE_POINT_TYPE* d2,int sqrt_able)
{
	if(sqrt_able)
		return GestureSqrt((d1->other.x - d2->other.x) *
			(d1->other.x - d2->other.x) +
			(d1->other.y - d2->other.y) *
			(d1->other.y - d2->other.y));
	else
		return (d1->other.x - d2->other.x) *
			(d1->other.x - d2->other.x) +
			(d1->other.y - d2->other.y) *
			(d1->other.y - d2->other.y);
}

static int GesturePush(GESTURE_POINT_TYPE* data)
{
	if(gesture_num >= GESTURE_BUF_SIZE)
		return FALSE;
	if(gesture_num == 0)
	{
		gesture_buf[gesture_num ++].all = data->all & 0xffff0fff;
		return TRUE;
	}
	if(GestureDistance(data,&gesture_buf[gesture_num-1],TRUE) <= gesture_dis_min)
		return FALSE;
	gesture_buf[gesture_num ++].all = data->all & 0xffff0fff;
		return TRUE;
}

static void GestureInit(void)
{
	gesture_num_last = gesture_num;
	gesture_num = 0;
	gesture_deal = FALSE;
	if(gesture_dis_min < 0 || gesture_dis_min > 64)
		gesture_dis_min = 2;
}

static int GestureStretch(void)
{
	unsigned int x_max=0,x_min=0xffff,y_max=0,y_min=0xffff;
	int i;
// 	if(gesture_num <= GESTURE_SIZE_NUM/2)
// 		return FALSE;
	if(gesture_num >= GESTURE_BUF_SIZE)
		return FALSE;
	for(i=0;i<gesture_num;i++)
	{
		if(gesture_buf[i].other.x > x_max)
			x_max = gesture_buf[i].other.x;
		if (gesture_buf[i].other.x < x_min)
			x_min = gesture_buf[i].other.x;
		if (gesture_buf[i].other.y > y_max)
			y_max = gesture_buf[i].other.y;
		if (gesture_buf[i].other.y < y_min)
			y_min = gesture_buf[i].other.y;
	}
	if(x_max < x_min+64*2 || y_max < y_min+64*3)
		return FALSE;
	for(i=0;i<gesture_num;i++)
	{
		gesture_buf[i].other.x = (gesture_buf[i].other.x - x_min) *
			GESTURE_SIZE_REFE / (x_max - x_min);
		gesture_buf[i].other.y = (gesture_buf[i].other.y - y_min) *
			GESTURE_SIZE_REFE / (y_max - y_min);
	}
	return TRUE;
}

static int GestureLength(void)
{
	int i;
	int len = 0;
	for(i=1;i<gesture_num;i++)
	{
		len += GestureDistance(&gesture_buf[i],&gesture_buf[i-1],TRUE);
	}
	return len;
}

static void GestureStandard(void)
{
	int i,n,t;
	int len_now = 0;
	int len_his = 0;
	int len_total = GestureLength();
	gesture_standard[0].all = gesture_buf[0].all&0x0fffffff;
	gesture_standard[GESTURE_SIZE_NUM - 1].all =
		gesture_buf[gesture_num -1].all&0x0fffffff;
	for(i=1,n=0;i<GESTURE_SIZE_NUM-1;i++)
	{
		while(++n<gesture_num)
		{
			len_now = GestureDistance(&gesture_buf[n],&gesture_buf[n-1],TRUE);
			len_his += len_now;
			if(len_his*(GESTURE_SIZE_NUM-1) >= len_total*i)
				break;
		}
		if(n >= gesture_num || len_now == 0)
			break;
		gesture_standard[i].all = 0;
		t = (int)gesture_buf[n - 1].other.x
			+ ((int)gesture_buf[n].other.x - (int)gesture_buf[n - 1].other.x)
			* ((int)len_total*i/(GESTURE_SIZE_NUM-1) - (int)len_his + (int)len_now)
			/ (int)len_now
			;
		if(t < 0)
			t = 0;
		gesture_standard[i].other.x = t;
		t = (int)gesture_buf[n - 1].other.y
			+ ((int)gesture_buf[n].other.y - (int)gesture_buf[n - 1].other.y)
			* ((int)len_total*i/(GESTURE_SIZE_NUM-1) - (int)len_his + (int)len_now)
			/ (int)len_now
			;
		if(t < 0)
			t = 0;
		gesture_standard[i].other.y = t;
		n--;
		len_his -= len_now;
	}
}

static int GestureModel(const GESTURE_MODEL_TYPE * model,int len,int threshold,int *out)
{
	int offset[] = {-2,-1,0,1,2};
	int ret_offset;
	int i,j,k,n;
	int min,min_n;
	GESTURE_POINT_TYPE model_coor;
	if(model == NULL || threshold <= 0)
	{
		*out = 0x7fffffff;
		return 0x7fffffff;
	}
	min=0x7fffffff;
	min_n = 0;
	for(j=0;j<len;j++)
	{
		for(k=0;k<sizeof(offset)/sizeof(offset[0]);k++)
		{
			n = 0;
			ret_offset = 0;
			for(i=0;i<GESTURE_SIZE_NUM;i++)
			{
				if(i+offset[k] < 0 || i+offset[k] >= GESTURE_SIZE_NUM)
					continue;
				if((i&1)==0)
					model_coor.all = model[j].coor[i/2] & 0x00ff00ff;
				else
					model_coor.all = (model[j].coor[i/2]>>8) & 0x00ff00ff;
				ret_offset += GestureDistance(&gesture_standard[i+offset[k]],&model_coor,FALSE);
				n ++;
			}
			if(n == 0)
				continue;
			ret_offset = ret_offset / n * model[j].coe / 0x10;//coe <0x3fff
			if(ret_offset < min)
			{
				min_n = j;
				min = ret_offset;
			}
		}
	}
	if(min < threshold)
		*out = model[min_n].out;
	else
		*out = 0x7fffffff;
	return min;
}

static void ChangeXY(void)
{
	int i;
	for(i=0;i<gesture_num && i<GESTURE_BUF_SIZE;i++)
		gesture_buf[i].all = ((gesture_buf[i].all & 0xfff) << 16) +
		((gesture_buf[i].all>>16) & 0xffff);
}

static void GestureSet(unsigned int conf[])
{
	if(conf == NULL)
		return;
	//if(conf[0] >= 0 && conf[0] <= 64)
	if (conf[0] <= 64)
		gesture_dis_min = conf[0];
	else
		gesture_dis_min = 0;
	if(conf[1] != 0)
		gesture_threshold[0] = conf[1];
	else
		gesture_threshold[0] = 0xfff;
	gesture_threshold[1] = conf[2];
	x_scale = (conf[3]==0) ? 4 : conf[3];
	y_scale = (conf[4]==0) ? 4 : conf[4];
	if(conf[5] == 0)
	{
		double_down = 2;
		double_up = 30;
	}
	else
	{
		double_down = conf[5] & 0xffff;
		double_up = conf[5] >> 16;
	}
}

static int GestureDeal(void)
{
	int i;
	int gesture_out[2];
	int gesture_val[2];

	//while(1)
	for (;;)
	{
		gesture_last = double_click;
		if(gesture_last)
			break;
		if(gesture_deal & GESTURE_XY)
		{
			gesture_deal &= ~GESTURE_XY;
			ChangeXY();
		}
		if((gesture_deal & GESTURE_DEAL) == 0)
			return FALSE;
		gesture_deal &= ~GESTURE_DEAL;
		gesture_last = GestureLRUD();
		if(gesture_last)
			break;
		if(GestureStretch() == FALSE)
			break;
		GestureStandard();
		gesture_val[0] =  GestureModel(model_default,
			sizeof(model_default)/sizeof(model_default[0]),
			gesture_threshold[0],&gesture_out[0]);
		gesture_val[1] =  GestureModel(model_extern,
			model_extern_len,gesture_threshold[1],&gesture_out[1]);
		gesture_last = 0x7fffffff;
		for(i=0;i<2;i++)
		{
			if(gesture_val[i] <= gesture_last)
			{
//				gesture_value = gesture_val[i];
				gesture_last  = gesture_out[i];
			}
		}
		break;
	}
	GestureInit();
	return gesture_last;
}

void gsl_GestureExtern(const GESTURE_MODEL_TYPE *model,int len)
{
	model_extern = model;
	model_extern_len = len;
}
//EXPORT_SYMBOL(gsl_GestureExtern);

static int GestureLRUD(void)
{
	int x1=0,y1=0,x2=0,y2=0,i=0;
	int flag3=0;
	int middle_x;
	int middle_y;
	int min_scale=5;
//	printk("flag3,gesture_deal=%x\n",gesture_deal);
	if(gesture_deal & GESTURE_XY)
	{
		gesture_deal &= ~GESTURE_XY;
		ChangeXY();
//		printk("flag3,ChangeXY_GestureLRUD,gesture_deal=%x\n",gesture_deal);
	}
	if((gesture_deal & GESTURE_LRUD) == 0)
		return FALSE;
	gesture_deal &= ~GESTURE_LRUD;
//	int screen_x_max=0,screen_y_max=0;
	x1 = gesture_buf[0].other.x;//480
	y1 = gesture_buf[0].other.y;//800
	x2 = gesture_buf[gesture_num - 1].other.x;
	y2 = gesture_buf[gesture_num - 1].other.y;
// 	if(!x1&&!y1&&!x2&&!y2)
// 		return '6';
	middle_x =( x1 + x2)/2;
	middle_y = (y1 + y2)/2;
	for(i=1;i<gesture_num;i++)
	{
		if (abs(gesture_buf[i].other.x - middle_x)<(int)sen_num_nokey * 64 / x_scale)//screen_y_max/8)//30
			flag3|=0x1;
		else
			flag3|=0x2;
		if (abs(gesture_buf[i].other.y - middle_y)<(int)drv_num_nokey * 64 / y_scale)//screen_x_max/8)//25
			flag3|=(0x1<<4);
		else
			flag3|=(0x2<<4);
		if ((int)gesture_buf[i].other.x - (int)gesture_buf[i - 1].other.x>min_scale)
			flag3|=(0x1<<8);
		else if ((int)gesture_buf[i].other.x - (int)gesture_buf[i - 1].other.x<-min_scale)
			flag3|=(0x2<<8);
		if ((int)gesture_buf[i].other.y - (int)gesture_buf[i - 1].other.y>min_scale)
			flag3|=(0x1<<12);
		else if ((int)gesture_buf[i].other.y - (int)gesture_buf[i - 1].other.y<-min_scale)
			flag3|=(0x2<<12);
	}
//	printk("flag3_____flag3=%x,x_scale=%d,y_scale=%d\n",flag3,x_scale,y_scale);
//	if(flag3&&!point_num)
// 	{
// 		printk("flag3,x1=%d,y1=%d,x2=%d,y2=%d,screen_x_max=%d,screen_y_max=%d\n",x1,y1,x2,y2,screen_x_max,screen_y_max);
// 		printk("flag3====== %x,x1-x2=%d,x2-x1=%d,y1-y2=%d,y2-y1=%d\n",flag3,x1-x2,x2-x1,y1-y2,y2-y1);
// 	}
//	if(!point_num)
//	if(1)
	{
		if((flag3==0x2031||flag3==0x2131||flag3==0x2231||flag3==0x2331))//&&(y2-y1>screen_x_max/3))
			return 0xa1fc;//up(a1,fc)
		else if((flag3==0x1031||flag3==0x1131||flag3==0x1231||flag3==0x1331))//&&(y1-y2>screen_x_max/3))
			return 0xa1fd;//down
		else if((flag3==0x213||flag3==0x1213||flag3==0x2213||flag3==0x2213))//&&(x2-x1>screen_y_max/3))
			return 0xa1fb;//left
		else if((flag3==0x113||flag3==0x1113||flag3==0x2113||flag3==0x3113))//&&(x1-x2>screen_y_max/3))
			return 0xa1fa;//right
//		if(abs(x2-x1)<64*4&&abs(y2-y1)<64*6)
//			return (int)'5';
	}
	return FALSE;
}

unsigned int gsl_GestureBuffer(unsigned int **buf)
{
	int i;
	if(gesture_num_last >= GESTURE_BUF_SIZE)
		gesture_num_last = GESTURE_BUF_SIZE - 1;
	for(i=0;i<gesture_num_last;i++)
	{
		gesture_buf[i].all = ScreenResolution((gsl_POINT_TYPE*)(&gesture_buf[i].all));
	}
	*buf = &(gesture_buf[0].all);
	return gesture_num_last;
}
//EXPORT_SYMBOL(gsl_GestureBuffer);
#endif
/*
// The DLL must have an entry point, but it is never called.
//
NTSTATUS
DriverEntry(
  IN PDRIVER_OBJECT DriverObject,
  IN PUNICODE_STRING RegistryPath
)
{
    return STATUS_SUCCESS;
}*/
