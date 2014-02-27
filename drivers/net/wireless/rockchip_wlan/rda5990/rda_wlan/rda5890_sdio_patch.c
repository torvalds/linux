#include "rda5890_dev.h"
#include "rda5890_wid.h"
#include "rda5890_defs.h"

#ifdef WIFI_TEST_MODE
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_mount.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/tty.h>
#include <linux/syscalls.h>
#include <asm/termbits.h>
#include <linux/serial.h>
#endif

static u8 sdio_patch_complete = 0;
static u8 sdio_init_complete = 0;

//#define dig_access_pmu_cancel 
const u32 wifi_core_patch_data_32[][2] =
{
	{0x00108000, 0xEA03DF9C},
	{0x00108004, 0xE59F101C},
	{0x00108008, 0xE3A00040},
	{0x0010800C, 0xE5C10038},
	{0x00108010, 0xE1A0F00E},
	{0x00108014, 0xEA03DF95},
	{0x00108018, 0xE59F1008},
	{0x0010801C, 0xE3A00040},
	{0x00108020, 0xE5C10038},
	{0x00108024, 0xE1A0F00E},
	{0x00108028, 0x50300000},
	{0x0010802C, 0xEB03D6F2},
	{0x00108030, 0xE1A00B84},
	{0x00108034, 0xE1B00BA0},
	{0x00108038, 0x11A00B84},
	{0x0010803C, 0x11A00BA0},
	{0x00108040, 0x12600F80},
	{0x00108044, 0x10804004},
	{0x00108048, 0xE1A00124},
	{0x0010804C, 0xE92D0011},
	{0x00108050, 0xE51F4030},
	{0x00108054, 0xE3A00020},
	{0x00108058, 0xE5C40038},
	{0x0010805C, 0xE8BD0011},
	{0x00108060, 0xE1A0F00E},
	{0x00108064, 0xEA03D3D2},
	{0x00108068, 0xE3A00001},
	{0x0010806C, 0xE1A0F00E},
	{0x00108070, 0xEA03D6CD},
	{0x00108074, 0xE3A00001},
	{0x00108078, 0xE1A0F00E},
	{0x0010807C, 0xEB03C786},
	{0x00108080, 0xE51F0060},
	{0x00108084, 0xE5D00038},
	{0x00108088, 0xE3100080},
	{0x0010808C, 0x1A000001},
	{0x00108090, 0xE3A00001},
	{0x00108094, 0xE1A0F00E},
	{0x00108098, 0xE3A00000},
	{0x0010809C, 0xE1A0F00E},
	{0x001080A0, 0xEB03EADE},
	{0x001080A4, 0xE51F0084},
	{0x001080A8, 0xE5D00038},
	{0x001080AC, 0xE3100080},
	{0x001080B0, 0x1A000001},
	{0x001080B4, 0xE3A00001},
	{0x001080B8, 0xE1A0F00E},
	{0x001080BC, 0xE3A00000},
	{0x001080C0, 0xE1A0F00E},
	{0x001080C4, 0xEB03D89D},
	{0x001080C8, 0xE51F00A8},
	{0x001080CC, 0xE3A01080},
	{0x001080D0, 0xE5C01038},
	{0x001080D4, 0xE1A0F00E},
	{0x001080D8, 0xEB03D714},
	{0x001080DC, 0xE51F10BC},
	{0x001080E0, 0xE5D10038},
	{0x001080E4, 0xE3100080},
	{0x001080E8, 0x159F001C},
	{0x001080EC, 0x059F0014},
	{0x001080F0, 0xE59F100C},
	{0x001080F4, 0xE581003C},
	{0x001080F8, 0xE51F10D8},
	{0x001080FC, 0xE1D101B4},
	{0x00108100, 0xE1A0F00E},
	{0x00108104, 0x30010000},
	{0x00108108, 0x2E00A100},
	{0x0010810C, 0x2E00A000},
	{0x00108110, 0xEB03B485},
	{0x00108114, 0x13100010},
	{0x00108118, 0x13A00002},
	{0x0010811C, 0x15C50067},
	{0x00108120, 0xE1A0F00E},
	{0x00108124, 0xEA03D804},
	{0x00108128, 0xE51F1108},
	{0x0010812C, 0xE5D10038},
	{0x00108130, 0xE2000020},
	{0x00108134, 0xE3500000},
	{0x00108138, 0x1AFFFFFB},
	{0x0010813C, 0xE3A01004},
	{0x00108140, 0xE3A00B48},
	{0x00108144, 0xE280FF45},
	{0x00108148, 0xEA0402C5},
	{0x0010814C, 0xE59F600C},
	{0x00108150, 0xE3A04000},
	{0x00108154, 0xE5C64000},
	{0x00108158, 0xE59F6004},
	{0x0010815C, 0xE59FF004},
	{0x00108160, 0x0010200B},
	{0x00108164, 0x0010121D},
	{0x00108168, 0x00007634},
	{0x0010816C, 0xEAFFFFEC},
	{0x00108170, 0xEA03E0FF},
	{0x00108174, 0x13A00000},
	{0x00108178, 0x1B000001},
	{0x0010817C, 0x15C57005},
	{0x00108180, 0xE59FF004},
	{0x00108184, 0xE51FF004},
	{0x00108188, 0x000024DC},
	{0x0010818C, 0x0000FD74},
	{0x001087B0, 0xEA0403A4},
	{0x001087B4, 0x08BD4010},
	{0x001087B8, 0x0A000002},
	{0x001087BC, 0x13A00001},
	{0x001087C0, 0x18BD4010},
	{0x001087C4, 0x1A000001},
	{0x001087C8, 0xE51FF004},
	{0x001087CC, 0x0000D470},
	{0x001087D0, 0xE51FF004},
	{0x001087D4, 0x0000D2C0},
	{0x20040004, 0x0001018C},
	{0x20040024, 0x00108000},
	{0x20040008, 0x000101BC},
	{0x20040028, 0x00108014},
	{0x2004000C, 0x00012460},
	{0x2004002C, 0x0010802C},
	{0x20040010, 0x00013118},
	{0x20040030, 0x00108064},
	{0x20040014, 0x00012538},
	{0x20040034, 0x00108070},
	{0x20040018, 0x00016260},
	{0x20040038, 0x0010807C},
	{0x2004001C, 0x0000D524},
	{0x2004003C, 0x001080A0},
	{0x20040020, 0x00011E4C},
	{0x20040040, 0x001080C4},
	{0x20040100, 0x00012484},
	{0x20040120, 0x001080D8},
	{0x20040104, 0x0001AEF8},
	{0x20040124, 0x00108110},
	{0x20040108, 0x00012110},
	{0x20040128, 0x00108124},
	{0x2004010C, 0x00007630},
	{0x2004012C, 0x00108148},
	{0x20040110, 0x00017158},
	{0x20040130, 0x0010816C},
	{0x20040114, 0x0000FD70},
	{0x20040134, 0x00108170},
	{0x20040118, 0x0000791C},
	{0x20040138, 0x001087B0},
	{0x20040000, 0x00007FFF},

};

const u32 wifi_core_patch_data_32_E[][2] =
{
    {0x00108000, 0xEA04012F},
    {0x00108004, 0x1A000001},
    {0x00108008, 0xE8BD4010},
    {0x0010800C, 0xEA000001},
    {0x00108010, 0xE51FF004},
    {0x00108014, 0x0000D4AC},
    {0x00108018, 0xE51FF004},
    {0x0010801C, 0x0000D654},
    {0x00108020, 0xEA0401F4},
    {0x00108024, 0xE59F600C},
    {0x00108028, 0xE3A04000},
    {0x0010802C, 0xE5C64000},
    {0x00108030, 0xE59F6004},
    {0x00108034, 0xE59FF004},
    {0x00108038, 0x00101CE7},
    {0x0010803C, 0x00101039},
    {0x00108040, 0x00007850},
    {0x00108044, 0xEAFFFFEC},
    {0x00108048, 0xEA03E034},
    {0x0010804C, 0x13A00000},
    {0x00108050, 0x1B000001},
    {0x00108054, 0x15C57005},
    {0x00108058, 0xE59FF004},
    {0x0010805C, 0xE51FF004},
    {0x00108060, 0x000024E0},
    {0x00108064, 0x0000FF78},
    {0x20040004, 0x00007B40},
    {0x20040024, 0x00108000},
    {0x20040008, 0x0000784C},
    {0x20040028, 0x00108020},
    {0x2004000C, 0x00017518},
    {0x2004002C, 0x00108044},
    {0x20040010, 0x0000FF74},
    {0x20040030, 0x00108048},
    {0x20040000, 0x0000000D},

};

const u8 wifi_core_patch_data_8[][2] =
{
    { 0x28, 0x1a} ,
    { 0x29, 0x0d},
    { 0x35, 0x1e},
    { 0x4c, 0x90},
    { 0x4d, 0x38},
    { 0x39, 0x07},
    { 0xe4, 0xf5},
    { 0x21, 0x00},  //default 0
    { 0x23, 0x10},
    { 0x48, 0x0e},
    { 0x25, 0x00},
    { 0x20, 0xa8},
    { 0x3f, 0x05},
    { 0x41, 0x37},
    { 0x42, 0x40},
    { 0x5b, 0xa9},
};

//#define   WF_PAT_CFG_2012_04_15


/*if define FORCE_WF, wf is not allow of any activity, antenna switch is also forced to bt*/
//#define   FORCE_WF

/*if define FORCE_WF_TX ,wf is not allow to do tx and pa is also disabled, but antenna is not forced*/
//#define   FORCE_WF_TX

/*if define FORCE_WF_RX ,wf is not allow to do rx but antenna is not forced*/
//#define   FORCE_WF_RX


/*if define FORCE_WF_RX_TX wf is not allow to do any tx and rx , pa disabled , but  antenna is not forced*/
//#define   FORCE_WF_RX_TX

#define WF_PAT_CFG_2012_05_19

const u32 wifi_core_init_data_32[][2] =
{
#ifdef   FORCE_WF_RX_TX
    {0x50000800,0xFC003E05},   
    {0x50000804,0x00000000},   
    {0x50000808,0xA5000013},   //no pre_active protect
    {0x5000080c,0x000001C0},   
    {0x50000810,0xFFCC0F01},   
    {0x50000814,0x00000000},  // not grant to rx and tx
    {0x50000818,0x00FF0001},   
    {0x5000081C,0xFF000F00},   
    {0x50000820,0x00000000},   
    {0x50000824,0x0000F0FE},   
    {0x50000828,0x00100F10},   
    {0x50000838,0xFFFFFFFF},   
    {0x5000083C,0xFFFFFFFF},   
#endif



#ifdef   FORCE_WF_RX
    {0x50000800,0xFC003E05},   
    {0x50000804,0x00000000},   
    {0x50000808,0xA5000013},   //no pre_active protect
    {0x5000080c,0x000001C0},   
    {0x50000810,0xFFCC0F01},   
    {0x50000814,0xFF000F00},   //wf not grant to rx
    {0x50000818,0x00FF0001},   
    {0x5000081C,0xFF000F00},   
    {0x50000820,0xFF000F00},   
    {0x50000824,0x0000F0FE},   
    {0x50000828,0x00100F10},   
    {0x50000838,0xFFFFFFFF},   
    {0x5000083C,0xFFFFFFFF},   
#endif


#ifdef FORCE_WF_TX
    {0x50000800,0xFC003E05},   
    {0x50000804,0x00000000},   
    {0x50000808,0xA5000013},   //no pre_active protect
    {0x5000080c,0x000001C0},   
    {0x50000810,0xFFCC0F01},   
    {0x50000814,0x00FF0033},   //wf not grant to tx
    {0x50000818,0x00FF0001},   
    {0x5000081C,0xFF000F00},   
    {0x50000820,0x00000000},   
    {0x50000824,0x0000F0FE},   
    {0x50000828,0x00100F10},   
    {0x50000838,0xFFFFFFFF},   
    {0x5000083C,0xFFFFFFFF},   
#endif


#ifdef WF_PAT_CFG_2012_05_19
    {0x50000800,0xFC003E05},   
    {0x50000804,0x00000000},   
    {0x50000808,0xA5000013},   //no pre_active protect
    {0x5000080c,0x000001C0},   
    {0x50000810,0xFFCC0F01},   
    {0x50000814,0xFFFF0F03},   //0xFFFF0F33
    {0x50000818,0x00FF0001},   
    {0x5000081C,0xFF000F00},   
    {0x50000820,0xFF000F00},   
    {0x50000824,0x0000F0FE},   
    {0x50000828,0x00100F10},   
    {0x50000838,0xFFFFFFFF},   
    {0x5000083C,0xFFFFFFFF},   
#endif


#ifdef  FORCE_WF
    {0x50000800,0xFC003E05},
    {0x50000804,0x00000000},
    {0x50000838,0xF8003f2A},
    {0x5000083c,0x00000003},
    {0x50000808,0xfe00001b},
    {0x50000810,0x00000000},
    {0x50000814,0x00000000},
    {0x50000818,0x00000000},
    {0x5000081C,0x00000000},
    {0x50000820,0x00000000},
    {0x50000824,0xffffffff},
    {0x50000828,0x00100F10},
#endif

#ifdef WF_PAT_CFG_2012_04_15    /*pta config*/                
    {0x50000800,0xFC003E05}, //tx_pri hi bits ctrl&mgmt package
    {0x50000804,0x00000000}, //tx_pri hi bits																												 as hi pri
    {0x50000808,0xA500001B}, //sig_mode and protect time																												
    {0x5000080c,0x000001C0}, //sigWire mode																												
    {0x50000810,0xFFCC0F01}, //Lut bt																												
    {0x50000814,0xFFFF0F33}, //Lut wf																												
    {0x50000818,0x00FF0001}, //antSel0 for wl_rx																												
    {0x5000081C,0xFF000F00}, //antSel1 for wl_tx      																												
    {0x50000820,0xFF000F00}, //antSel2 for wl_pa	
    //{0x50000838,0xFFFFFFFF}, //rx_pri low bits as high pri
	//{0x5000083C,0xFFFFFFFF}, //rx_pri high  bits as high pri																											
#endif																												                                
    																											
/*end pta config*/
    {  0x00106b6c, 0x00000002 }, // scan channel 13
    {  0x30010004, 0x0000f77c },    //intn config
    {  0x30010010, 0x00007dff },    //intn config
    //item111:ver_b_wf_dig_2011_10_09
    {  0x30010000, 0x780369AF },   //disable tports wait  100ms;
    {  0x30000010, 0x7000FFFF },//wait 500ms;
    {  0x50090054, 0x00000001 },//enable update
    {  0x50090200, 0x00000000 },
    {  0x50090204, 0x00000000 },
    {  0x50090208, 0x00000002 },
    {  0x5009020c, 0x00000004 },
    {  0x50090210, 0x00000006 },
    {  0x50090214, 0x00000008 },
    {  0x50090218, 0x0000000a },
    {  0x5009021c, 0x00000040 },
    {  0x50090220, 0x00000042 },
    {  0x50090224, 0x00000044 },
    {  0x50090228, 0x00000046 },
    {  0x5009022c, 0x00000048 },
    {  0x50090230, 0x0000004a },
    {  0x50090234, 0x00000080 },
    {  0x50090238, 0x00000082 },
    {  0x5009023c, 0x00000084 },
    {  0x50090240, 0x00000086 },
    {  0x50090244, 0x00000088 },
    {  0x50090248, 0x0000008a },
    {  0x5009024c, 0x000000c0 },
    {  0x50090250, 0x000000c2 },
    {  0x50090254, 0x000000c4 },
    {  0x50090258, 0x000000c6 },
    {  0x5009025c, 0x000000c8 },
    {  0x5009025c, 0x000000c8 },
    {  0x50090260, 0x000000ca },
    {  0x50090264, 0x00000100 },
    {  0x50090268, 0x00000102 },
    {  0x5009026c, 0x00000104 },
    {  0x50090270, 0x00000106 },
    {  0x50090274, 0x00000108 },
    {  0x50090278, 0x00000140 },
    {  0x5009027c, 0x00000142 },//lna =0 end
    {  0x50090280, 0x00000080 },
    {  0x50090284, 0x00000082 },
    {  0x50090288, 0x00000084 },
    {  0x5009028c, 0x00000086 },
    {  0x50090290, 0x00000088 },
    {  0x50090294, 0x0000008a },
    {  0x50090298, 0x000000c0 },
    {  0x5009029c, 0x000000c2 },
    {  0x500902a0, 0x000000c4 },
    {  0x500902a4, 0x000000c6 },
    {  0x500902a8, 0x000000c8 },
    {  0x500902ac, 0x000000ca },
    {  0x500902b0, 0x00000100 },
    {  0x500902b4, 0x00000102 },
    {  0x500902b8, 0x00000104 },
    {  0x500902bc, 0x00000106 },
    {  0x500902c0, 0x00000108 },
    {  0x500902c4, 0x00000140 },
    {  0x500902c8, 0x00000142 },
    {  0x500902cc, 0x00000144 },
    {  0x500902d0, 0x00000146 },
    {  0x500902d4, 0x00000148 },
    {  0x500902d8, 0x00000180 },
    {  0x500902dc, 0x00000182 },
    {  0x500902e0, 0x00000184 },
    {  0x500902e4, 0x000001c0 },
    {  0x500902e8, 0x000001c2 },
    {  0x500902ec, 0x000001c4 },
    {  0x500902f0, 0x000001c6 },
    {  0x500902f4, 0x000001c8 },
    {  0x500902f8, 0x000001ca },
    {  0x500902fc, 0x000001cc },// lna = 01  end
    {  0x50090300, 0x00000102 },
    {  0x50090304, 0x00000104 },
    {  0x50090308, 0x00000106 },
    {  0x5009030c, 0x00000108 },
    {  0x50090310, 0x00000140 },
    {  0x50090314, 0x00000142 },
    {  0x50090318, 0x00000144 },
    {  0x5009031c, 0x00000146 },
    {  0x50090320, 0x00000148 },
    {  0x50090324, 0x00000180 },
    {  0x50090328, 0x00000182 },
    {  0x5009032c, 0x00000184 },
    {  0x50090330, 0x000001c0 },
    {  0x50090334, 0x000001c2 },
    {  0x50090338, 0x000001c4 },
    {  0x5009033c, 0x000001c6 },
    {  0x50090340, 0x000001c8 },
    {  0x50090344, 0x000001c9 },
    {  0x50090348, 0x000001c9 },
    {  0x5009034c, 0x000001c9 },
    {  0x50090350, 0x000001c9 },
    {  0x50090354, 0x000001c9 },
    {  0x50090358, 0x000001c9 },
    {  0x5009035c, 0x000001c9 },
    {  0x50090360, 0x000001c9 },
    {  0x50090364, 0x000001c9 },
    {  0x50090368, 0x000001c9 },
    {  0x5009036c, 0x000001c9 },
    {  0x50090370, 0x000001c9 },
    {  0x50090374, 0x000001c9 },
    {  0x50090378, 0x000001c9 },
    {  0x5009037c, 0x000001c9 },
    {  0x50090054, 0x00000000 },//disable update

    {  0x5000050c, 0x00008000 },// for association power save
    
    //{  0x50000808, 0x65000013 }, // disable prerx_priority;pta config
    //{  0x50000810, 0xFFCD0F01 },  //rx beacon priority

};

const u32 wifi_core_init_data_32_E[][2] =
{
#ifdef   FORCE_WF_RX_TX
    {0x50000800,0xFC003E05},   
    {0x50000804,0x00000000},   
    {0x50000808,0xA5000013},   //no pre_active protect
    {0x5000080c,0x000001C0},   
    {0x50000810,0xFFCC0F01},   
    {0x50000814,0x00000000},  // not grant to rx and tx
    {0x50000818,0x00FF0001},   
    {0x5000081C,0xFF000F00},   
    {0x50000820,0x00000000},   
    {0x50000824,0x0000F0FE},   
    {0x50000828,0x00100F10},   
    {0x50000838,0xFFFFFFFF},   
    {0x5000083C,0xFFFFFFFF},   
#endif

#ifdef   FORCE_WF_RX
    {0x50000800,0xFC003E05},   
    {0x50000804,0x00000000},   
    {0x50000808,0xA5000013},   //no pre_active protect
    {0x5000080c,0x000001C0},   
    {0x50000810,0xFFCC0F01},   
    {0x50000814,0xFF000F00},   //wf not grant to rx
    {0x50000818,0x00FF0001},   
    {0x5000081C,0xFF000F00},   
    {0x50000820,0xFF000F00},   
    {0x50000824,0x0000F0FE},   
    {0x50000828,0x00100F10},   
    {0x50000838,0xFFFFFFFF},   
    {0x5000083C,0xFFFFFFFF},   
#endif

#ifdef FORCE_WF_TX
    {0x50000800,0xFC003E05},   
    {0x50000804,0x00000000},   
    {0x50000808,0xA5000013},   //no pre_active protect
    {0x5000080c,0x000001C0},   
    {0x50000810,0xFFCC0F01},   
    {0x50000814,0x00FF0033},   //wf not grant to tx
    {0x50000818,0x00FF0001},   
    {0x5000081C,0xFF000F00},   
    {0x50000820,0x00000000},   
    {0x50000824,0x0000F0FE},   
    {0x50000828,0x00100F10},   
    {0x50000838,0xFFFFFFFF},   
    {0x5000083C,0xFFFFFFFF},   
#endif

#ifdef WF_PAT_CFG_2012_05_19
    {0x50000800,0xFC003E05},   
    {0x50000804,0x00000000},   
    {0x50000808,0xA5000013},   //no pre_active protect
    {0x5000080c,0x000001C0},   
    {0x50000810,0xFFCC0F01},   
    {0x50000814,0xFFFF0F03},   //0xFFFF0F33
    {0x50000818,0x00FF0001},   
    {0x5000081C,0xFF000F00},   
    {0x50000820,0xFF000F00},   
    {0x50000824,0x0000F0FE},   
    {0x50000828,0x00100F10},   
    {0x50000838,0xFFFFFFFF},   
    {0x5000083C,0xFFFFFFFF},   
#endif

#ifdef  FORCE_WF
    {0x50000800,0xFC003E05},
    {0x50000804,0x00000000},
    {0x50000838,0xF8003f2A},
    {0x5000083c,0x00000003},
    {0x50000808,0xfe00001b},
    {0x50000810,0x00000000},
    {0x50000814,0x00000000},
    {0x50000818,0x00000000},
    {0x5000081C,0x00000000},
    {0x50000820,0x00000000},
    {0x50000824,0xffffffff},
    {0x50000828,0x00100F10},
#endif

#ifdef WF_PAT_CFG_2012_04_15    /*pta config*/                
    {0x50000800,0xFC003E05}, //tx_pri hi bits ctrl&mgmt package
    {0x50000804,0x00000000}, //tx_pri hi bits																												 as hi pri
    {0x50000808,0xA500001B}, //sig_mode and protect time																												
    {0x5000080c,0x000001C0}, //sigWire mode																												
    {0x50000810,0xFFCC0F01}, //Lut bt																												
    {0x50000814,0xFFFF0F33}, //Lut wf																												
    {0x50000818,0x00FF0001}, //antSel0 for wl_rx																												
    {0x5000081C,0xFF000F00}, //antSel1 for wl_tx      																												
    {0x50000820,0xFF000F00}, //antSel2 for wl_pa	
    //{0x50000838,0xFFFFFFFF}, //rx_pri low bits as high pri
	//{0x5000083C,0xFFFFFFFF}, //rx_pri high  bits as high pri																											
#endif							

    {  0x30010004, 0x0000f77c },    //intn config
    {  0x30010010, 0x00007dff },    //intn config
    //item111:ver_b_wf_dig_2011_10_09
    {  0x30010000, 0x780369AF },   //disable tports wait  100ms;
    {  0x30000010, 0x7000FFFF },//wait 500ms;
    {  0x5000050c, 0x00008000 },// for association power save
};

u32 wifi_notch_data[][2] =
{
    //ch 1
    {0x001008d0, 0x50090040},   
    {0x001008d4, 0x057213a2},   
    {0x001008d8, 0x50090044},   
    {0x001008dc, 0x10000000},  
    //ch 2 
    {0x00100910, 0x50090040},   
    {0x00100914, 0x10000000},   
    {0x00100918, 0x50090044},   
    {0x0010091c, 0x10000000},   
    //ch 3
    {0x00100950, 0x50090040},   
    {0x00100954, 0x10000000},   
    {0x00100958, 0x50090044},   
    {0x0010095c, 0x10000000},   
    //ch 4
    {0x00100990, 0x50090040},   
    {0x00100994, 0x10000000},   
    {0x00100998, 0x50090044},   
    {0x0010099c, 0x10000000},   
    //ch 5
    {0x001009d0, 0x50090040},   
    {0x001009d4, 0x076794b4},   
    {0x001009d8, 0x50090044},   
    {0x001009dc, 0x10000000},   
    //ch 6
    {0x00100a10, 0x50090040},   
    {0x00100a14, 0x077c71de},   
    {0x00100a18, 0x50090044},   
    {0x00100a1c, 0x046d242e},   
    //ch 7
    {0x00100a50, 0x50090040},   
    {0x00100a54, 0x10000000},   
    {0x00100a58, 0x50090044},   
    {0x00100a5c, 0x057e7ec0},   
    //ch 8
    {0x00100a90, 0x50090040},   
    {0x00100a94, 0x077c7e22},   
    {0x00100a98, 0x50090044},   
    {0x00100a9c, 0x046d2bd2},   
    //ch 9
    {0x00100ad0, 0x50090040},   
    {0x00100ad4, 0x10000000},   
    {0x00100ad8, 0x50090044},   
    {0x00100adc, 0x10000000},   
    //ch 10
    {0x00100b10, 0x50090040},   
    {0x00100b14, 0x10000000},   
    {0x00100b18, 0x50090044},   
    {0x00100b1c, 0x10000000},   
    //ch 11
    {0x00100b50, 0x50090040},   
    {0x00100b54, 0x10000000},   
    {0x00100b58, 0x50090044},   
    {0x00100b5c, 0x10000000},   
    //ch 12
    {0x00100b90, 0x50090040},   
    {0x00100b94, 0x07764310},   
    {0x00100b98, 0x50090044},   
    {0x00100b9c, 0x10000000},   
    //ch 13
    {0x00100bd0, 0x50090040},   
    {0x00100bd4, 0x056794b4},   
    {0x00100bd8, 0x50090044},   
    {0x00100bdc, 0x10000000},   
    //ch 14
    {0x00100c10, 0x50090040},   
    {0x00100c14, 0x0779c279},   
    {0x00100c18, 0x50090044},   
    {0x00100c1c, 0x0779cd87},   
};

u32 wifi_notch_data_E[][2] =
{
  // For Verion E
    //ch 1
    {0x001007CC, 0x50090040},   
    {0x001007D0, 0x057213a2},   
    {0x001007D4, 0x50090044},   
    {0x001007D8, 0x10000000},  
    //ch 2 
    {0x001007FC, 0x50090040},   
    {0x00100800, 0x10000000},   
    {0x00100804, 0x50090044},   
    {0x00100808, 0x10000000},   
    //ch 3
    {0x0010082C, 0x50090040},   
    {0x00100830, 0x10000000},   
    {0x00100834, 0x50090044},   
    {0x00100838, 0x10000000},   
    //ch 4
    {0x0010085C, 0x50090040},   
    {0x00100860, 0x10000000},   
    {0x00100864, 0x50090044},   
    {0x00100868, 0x10000000},   
    //ch 5
    {0x0010088C, 0x50090040},   
    {0x00100890, 0x076794b4},   
    {0x00100894, 0x50090044},   
    {0x00100898, 0x10000000},   
    //ch 6
    {0x001008BC, 0x50090040},   
    {0x001008C0, 0x077c71de},   
    {0x001008C4, 0x50090044},   
    {0x001008C8, 0x046d242e},   
    //ch 7
    {0x001008EC, 0x50090040},   
    {0x001008F0, 0x10000000}, 
    {0x001008F4, 0x50090044},   
    {0x001008F8, 0x057e7140},   
    //ch 8
    {0x0010091C, 0x50090040},   
    {0x00100920, 0x077c7e22},   
    {0x00100924, 0x50090044},   
    {0x00100928, 0x046d2bd2},   
    //ch 9
    {0x0010094C, 0x50090040},   
    {0x00100950, 0x10000000},   
    {0x00100954, 0x50090044},   
    {0x00100958, 0x10000000},   
    //ch 10
    {0x0010097C, 0x50090040},   
    {0x00100980, 0x10000000},   
    {0x00100984, 0x50090044},   
    {0x00100988, 0x10000000},   
    //ch 11
    {0x001009AC, 0x50090040},   
    {0x001009B0, 0x10000000},   
    {0x001009B4, 0x50090044},   
    {0x001009B8, 0x10000000},   
    //ch 12
    {0x001009DC, 0x50090040},   
    {0x001009E0, 0x07764310},   
    {0x001009E4, 0x50090044},   
    {0x001009E8, 0x10000000},   
    //ch 13
    {0x00100A0C, 0x50090040},   
    {0x00100A10, 0x056794b4},   
    {0x00100A14, 0x50090044},   
    {0x00100A18, 0x10000000},
    //ch 14
    {0x00100A3C, 0x50090040},   
    {0x00100A40, 0x0779c279},   
    {0x00100A44, 0x50090044},   
    {0x00100A4c, 0x0779cd87},
};

//common sdio clock open  
const u32 wifi_core_data_wake[][2] = 
{
    {0x3001003c, 0x2e00a000},
};
//sleep sdio switch
const u32 wifi_core_data_sleep[][2] =
{
    {0x3001003c, 0x2e00a100},
};

int rda5890_get_fw_version_polling(struct rda5890_private *priv, unsigned int* version)
{
    int ret = -1;
    char wid_req[255];
    unsigned short wid_req_len = 6;
    char wid_rsp[32];
    unsigned short wid_rsp_len = 32;
    unsigned short wid;
    char wid_msg_id = priv->wid_msg_id++;
    unsigned char *ptr_payload;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_DEBUG,
	"%s <<< \n", __func__);

    wid_req[0] = 'Q';
    wid_req[1] = wid_msg_id;

    wid_req[2] = (char)(wid_req_len&0x00FF);
    wid_req[3] = (char)((wid_req_len&0xFF00) >> 8);

    wid = WID_SYS_FW_VER;
    wid_req[4] = (char)(wid&0x00FF);
    wid_req[5] = (char)((wid&0xFF00) >> 8);

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"__func__ <<<\n" );

	ret = rda5890_wid_request_polling(priv, wid_req, wid_req_len, 
                                        wid_rsp, &wid_rsp_len);
	if (ret) {
		goto out;
	}

    ret = rda5890_check_wid_response(priv->wid_rsp, priv->wid_rsp_len, wid, wid_msg_id, 
                4, &ptr_payload);

    if(!ret)
    {
        memcpy((unsigned char *)version, ptr_payload, 4);
    }
    else
        *version = 0;

	RDA5890_DBGLAP(RDA5890_DA_WID, RDA5890_DL_TRACE,
		"__func__ version: %x >>> \n" , *version);

out:
	return ret;
}


int rda5890_write_sdio32_polling(struct rda5890_private *priv,const unsigned int (*data)[2], unsigned int size)
{
	int count = size, index = 0;
	int ret = 0;
	
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	            "%s >>> \n", __func__);	
	
	for(index = 0; index < count/8; index ++)  //each time write five init data
	{
		ret = rda5890_set_core_init_polling(priv, data[8*index], 8);
        if(ret < 0)
        goto err;
	}

	if(count%8 > 0)
	{
		ret = rda5890_set_core_init_polling(priv, data[8*index], count%8);
        if(ret < 0)
        goto err;
	}	
err:
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	        "%s :ret=%d <<< \n", __func__, ret);
	return ret;
}

int rda5890_write_sdio32(struct rda5890_private *priv, const unsigned int (*data)[2], unsigned int size)
{
	int count = size, index = 0;
	int ret = 0;
	
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	            "%s >>> \n", __func__);	
	
	for(index = 0; index < count/8; index ++)  //each time write five init data
	{
		ret = rda5890_set_core_init(priv, data[8*index], 8);
        if(ret < 0)
            goto err;
	}
	
	if(count%8 > 0)
	{
		ret = rda5890_set_core_init(priv, data[8*index], count%8);
        if(ret < 0)
            goto err;
	}	
err:
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	        "%s :ret=%d <<< \n", __func__, ret);
	return ret;
}

int rda5890_write_sdio8_polling(struct rda5890_private *priv, const unsigned char (*data)[2], unsigned int size)
{
	int count = size, index = 0;
	int ret = 0;
	
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	            "%s >>> \n", __func__);	
	
	for(index = 0; index < count/8; index ++)  //each time write five init data
	{
		ret = rda5890_set_core_patch_polling(priv, data[8*index], 8);
	    if(ret < 0)
	        goto err;
	}

	if(count%8 > 0)
	{
		ret = rda5890_set_core_patch_polling(priv, data[8*index], count%8);
	    if(ret < 0)
	        goto err;
	}	
err:
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	        "%s :ret=%d <<< \n", __func__, ret);
	return ret;
}


int rda5890_write_sdio8(struct rda5890_private *priv ,const unsigned char (*data)[2], unsigned int size)
{
	int count = size, index = 0;
	int ret = 0;
	
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	            "%s >>> \n", __func__);	
	
	for(index = 0; index < count/8; index ++)  //each time write five init data
	{
		ret = rda5890_set_core_patch(priv, data[8*index], 8);
	  if(ret < 0)
	      goto err;
	}
	
	if(count%8 > 0)
	{
		ret = rda5890_set_core_patch(priv, data[8*index], count%8);
	  if(ret < 0)
	      goto err;
	}	
err:
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	        "%s :ret=%d <<< \n", __func__, ret);
	return ret;
}

int rda5890_sdio_patch_core_32(struct rda5890_private *priv)
{
    int ret = 0;

    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
                "%s >>> \n", __func__);	

    if(priv->version == 7)
    ret = rda5890_write_sdio32_polling(priv,wifi_core_patch_data_32, 
    									sizeof(wifi_core_patch_data_32)/sizeof(wifi_core_patch_data_32[0]));
    else if(priv->version == 4 || priv->version == 5)
        ret = rda5890_write_sdio32_polling(priv,wifi_core_patch_data_32_E, 
    									sizeof(wifi_core_patch_data_32_E)/sizeof(wifi_core_patch_data_32_E[0]));

    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
            "%s <<< \n", __func__);
    return ret;
}


int  rda5890_sdio_init_core(struct rda5890_private *priv)
{
	int ret = 0;
	  
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	              "%s <<< \n", __func__);	
    if(priv->version == 7)
	ret = rda5890_write_sdio32(priv, wifi_core_init_data_32, 
															sizeof(wifi_core_init_data_32)/sizeof(wifi_core_init_data_32[0]));  
	else if(priv->version == 4 || priv->version == 5)
        ret = rda5890_write_sdio32(priv, wifi_core_init_data_32_E, 
								   sizeof(wifi_core_init_data_32_E)/sizeof(wifi_core_init_data_32_E[0]));   
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	        "%s >>> \n", __func__);
	
	return ret;
}

int rda5890_sdio_init_core_polling(struct rda5890_private *priv)
{
	int ret = 0;
	  
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	              "%s <<< \n", __func__);	
	if(priv->version == 7)              
	ret = rda5890_write_sdio32_polling(priv, wifi_core_init_data_32, 
	                                   sizeof(wifi_core_init_data_32)/sizeof(wifi_core_init_data_32[0]));  
	else if(priv->version == 4 || priv->version == 5)
        ret = rda5890_write_sdio32_polling(priv, wifi_core_init_data_32_E, 
	                                     sizeof(wifi_core_init_data_32_E)/sizeof(wifi_core_init_data_32_E[0]));  
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	          "%s >>> \n", __func__);
	  return ret;
}


int rda5890_sdio_patch_core_8(struct rda5890_private *priv)
{
    int ret = 0;
    //for patch in byte mode
    
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
                "%s <<< \n", __func__);	

	  ret = rda5890_write_sdio8(priv, wifi_core_patch_data_8, 
	                            sizeof(wifi_core_patch_data_8)/sizeof(wifi_core_patch_data_8[0]));
  
    //for patch in wake continue clock mode
    ret = rda5890_sdio_core_wake_mode(priv);
    if(ret < 0)
        goto err;

err:
    
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
            "%s >>> \n", __func__);
    return ret;
}

int rda5890_sdio_patch_core_8_polling(struct rda5890_private *priv)
{
    int ret = 0;
    //for patch in byte mode
    
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
                "%s <<< \n", __func__);	

	ret = rda5890_write_sdio8_polling(priv, wifi_core_patch_data_8,
                                      sizeof(wifi_core_patch_data_8)/sizeof(wifi_core_patch_data_8[0]));
    if(ret < 0)
        goto err; 
          
    //for patch in wake continue clock mode
    ret = rda5890_sdio_core_wake_mode_polling(priv);
    if(ret < 0)
        goto err;  

err:
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
            "%s >>> \n", __func__);	
    return ret;
}

int rda5890_sdio_set_default_notch(struct rda5890_private *priv)
{
    int ret = 0;

    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
                "%s <<< \n", __func__);	

    if(priv->version == 7) 
	  ret = rda5890_write_sdio32(priv, wifi_notch_data, 
	                             sizeof(wifi_notch_data)/sizeof(wifi_notch_data[0]));
    else if(priv->version == 4 || priv->version == 5) 
        ret = rda5890_write_sdio32(priv, wifi_notch_data_E, 
	                             sizeof(wifi_notch_data_E)/sizeof(wifi_notch_data_E[0]));
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
                "%s <<< \n", __func__);	   
    return ret;
}

int rda5890_sdio_set_default_notch_polling(struct rda5890_private *priv)
{
	int ret = 0;
	
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	            "%s <<< \n", __func__);	
	if(priv->version == 7) 
	ret = rda5890_write_sdio32_polling(priv, wifi_notch_data, 
	                                   sizeof(wifi_notch_data)/sizeof(wifi_notch_data[0]));
	else if(priv->version == 4 || priv->version == 5)
        ret = rda5890_write_sdio32_polling(priv, wifi_notch_data_E, 
	                                   sizeof(wifi_notch_data_E)/sizeof(wifi_notch_data_E[0]));
	RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
	            "%s <<< \n", __func__);	   
	return ret;
}


//
unsigned char rssi_switch_default_data_u8[][2] =
{
        {0x25, 0x00}
};

void rda5890_sdio_set_notch_by_channel(struct rda5890_private *priv, unsigned int channel)
{
#if 0
    int count = 0, index = 0;

    if(priv->version == 7)
        count  = sizeof(wifi_notch_data)/(sizeof(wifi_notch_data[0]) * 4);
    else if(priv->version == 4 || priv->version == 5)
        count  = sizeof(wifi_notch_data_E)/(sizeof(wifi_notch_data_E[0]) * 4);
    channel = channel % count;
	
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
                "%s >>> \n", __func__);

    if(channel > 1)
	    channel -= 1;

    if(priv->version == 7)
        {
            rda5890_set_core_init(priv, wifi_notch_data[4*channel], 4);
            rda5890_set_core_patch(priv, rssi_switch_default_data_u8, 1);
        }
    else if(priv->version == 4 || priv->version == 5)
        {
            rda5890_set_core_init(priv, wifi_notch_data_E[4*channel], 4);
            rda5890_set_core_patch(priv, rssi_switch_default_data_u8, 1);
        }
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
                "%s <<< ch = %d \n", __func__, channel + 1);
#endif
}

int rda5890_sdio_core_wake_mode(struct rda5890_private *priv)
{
    int ret = 0;

    ret = rda5890_write_sdio32(priv, wifi_core_data_wake, 
                               sizeof(wifi_core_data_wake)/sizeof(wifi_core_data_wake[0]));
   
    return ret;
}

int rda5890_sdio_core_wake_mode_polling(struct rda5890_private *priv)
{
    int ret = 0;

    ret = rda5890_write_sdio32_polling(priv, wifi_core_data_wake, 
                               sizeof(wifi_core_data_wake)/sizeof(wifi_core_data_wake[0]));   
    return ret;
}

int rda5890_sdio_core_sleep_mode(struct rda5890_private *priv)
{
    int ret = 0;

    ret = rda5890_write_sdio32(priv,wifi_core_data_sleep, 
                               sizeof(wifi_core_data_sleep)/sizeof(wifi_core_data_sleep[0]));
    return ret;
}

extern void export_wifi_eirq_enable();
int rda5890_sdio_init(struct rda5890_private *priv)
{
    int ret = 0;
    unsigned long para = 0;

    sdio_init_complete = 0;
    sdio_patch_complete = 0;

    printk(KERN_INFO " rda5890_sdio_init <<< \n");	

    ret = rda5890_sdio_patch_core_32(priv);
    if(ret < 0)
        goto err;
    
    printk(KERN_INFO "sdio_patch_complete wid_msg =  %d \n", priv->wid_msg_id);
    rda5890_shedule_timeout(10);   //10ms delay
    sdio_patch_complete = 1;  

#if 0
    ret = rda5890_sdio_init_core(priv);
    if(ret < 0)
        goto err;

    ret = rda5890_sdio_patch_core_8(priv);
    if(ret < 0)
        goto err;
    
    ret = rda5890_sdio_set_default_notch(priv);
    if(ret < 0)
        goto err;
#else

    ret = rda5890_sdio_init_core_polling(priv);
    if(ret < 0)
        goto err;

    ret = rda5890_sdio_patch_core_8_polling(priv);
    if(ret < 0)
        goto err;
        
    ret = rda5890_sdio_set_default_notch_polling(priv);
    if(ret < 0)
        goto err;
    
#endif
    export_wifi_eirq_enable();
    
#if 0
	rda5890_generic_set_ulong(priv, WID_MEMORY_ADDRESS, 0x30010008);	
	rda5890_generic_get_ulong(priv, WID_MEMORY_ACCESS_32BIT, &para);
	printk(KERN_INFO "rda5890_sdio_init para = %x \n", para);
	para |= 0x200;
	rda5890_generic_set_ulong(priv, WID_MEMORY_ACCESS_32BIT, para);
	
	rda5890_generic_set_ulong(priv, WID_MEMORY_ADDRESS, 0x30010010);
	rda5890_generic_get_ulong(priv, WID_MEMORY_ACCESS_32BIT, &para);
	printk(KERN_INFO "rda5890_sdio_init para1 = %x \n", para);
	para &= ~0x200;
	rda5890_generic_set_ulong(priv, WID_MEMORY_ACCESS_32BIT, para);
#endif

    sdio_init_complete = 1;

err:
    return ret;
}

//rssi > 200
u32 rssi_switch_data_u32[][2] = 
{
    {0x50090040, 0x10000000},
    {0x50090044, 0x10000000},
};

u8 rssi_switch_data_u8[][2] = 
{
    {0x25, 0x18}
};

void rda5890_rssi_up_to_200(struct rda5890_private *priv)
{
#if 0
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
                "%s >>> \n", __func__);
    rda5890_set_core_init(priv, rssi_switch_data_u32, 2);
    rda5890_set_core_patch(priv, rssi_switch_data_u8, 1);	

    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_DEBUG,
                "%s <<< \n", __func__);
#endif    
}

u32 rda5990_assoc_power_save_data32[][2] =
{
    {  0x5000050c, 0x00008000 }// for association power save
};

void rda5990_assoc_power_save(struct rda5890_private *priv)
{
    rda5890_set_core_init(priv, rda5990_assoc_power_save_data32, 1);
}

unsigned char is_sdio_init_complete(void)
{
    return sdio_init_complete;
}
unsigned char is_sdio_patch_complete(void) //after patch complete need check write flow
{
    return sdio_patch_complete;
}

#ifdef WIFI_TEST_MODE

static unsigned int wifi_test_mode_rx_notch_32[][2] =
{
	//item:notch_filter_5
	{0x50090040,0x076794b4},//8m
	{0x50090044,0x10000000},
	//item:notch_filter_6
	{0x50090040,0x077c71de},//3m
	{0x50090044,0x046d242e},//7m
	//item:notch_filter_7
	{0x50090040,0x077e7ec0},//2m
	{0x50090044,0x077e7140},//-2m
	//item:notch_filter_8
	{0x50090040,0x077c7e22},//3m
	{0x50090044,0x046d2bd2},//7m
	//item:notch_filter_c
	{0x50090040,0x07764310},//5m
	{0x50090044,0x10000000},
	//item:notch_filter_e
	{0x50090040,0x0779c279},//4m
	{0x50090044,0x0779cd87},//-4m
	//item:disable_notch
	{0x50090040,0x10000000},
	{0x50090044,0x10000000},
};

static unsigned int wifi_test_mode_agc_patch32[][2] =
{
	{0x50000600,0x0000501a},//write 1a(52) to 28h hightolow                   
	{0x50000600,0x0000520d},//write 0d(26) to 29h hightomid                   
	{0x50000600,0x00006a1e},//35h reg coarse2 upper window from 0d to 1a for l
	//;50000600H,32'h00009890;//4ch reg unlock upper threshold from 70 to 90  
	{0x50000600,0x00009a38},//4dh reg unlock lower threshold from 78 to 38    
	{0x50000600,0x00007207},//39h reg change vga gain ,9 -> 7 for big signal  
	{0x50000600,0x0001c8f5},//e4h reg change hpf coeff to f5                  
	{0x50000600,0x00004200},//21h reg add fine gain 0db                       
	{0x50000600,0x00004610},//23h reg change maxgain index as agc table       
	{0x50000600,0x0000900e},//48h reg unlock lower threshold change from 0a to
	{0x50000600,0x00004a00},//25h reg pecket dection threshold                
	{0x50000600,0x000040a8},//20h reg add  fine itr2 98->a8                   
	{0x50000600,0x00007e05},//3f reg rssi window for fine itr2 0->5           
	{0x50000600,0x00008237},//41 reg fine itr1 nextstate 4->3                 
	{0x50000600,0x00008440},//42 reg fine itr2 nextstate 0->4 settle time 0->d
	{0x50000600,0x0000b6a9},//5b reg change GreatN rssi avg count from 1 to 8 
} ;

static unsigned int rda5990_test_mode_digital32[][2] =
{ 
	//item111:ver_D_wf_dig_20120208               
	{0x30010000,0x780369AF},   //disable tports   
	//wait 100ms;                                 
	{0x30000010,0x7000FFFF},                      
	//item:agc_table_20110921                  
	{0x50090054,0x00000001},//enable update       
	{0x50090200,0x00000000},                      
	{0x50090204,0x00000000},                      
	{0x50090208,0x00000002},                      
	{0x5009020c,0x00000004},                      
	{0x50090210,0x00000006},                      
	{0x50090214,0x00000008},                      
	{0x50090218,0x0000000a},                      
	{0x5009021c,0x00000040},                      
	{0x50090220,0x00000042},                      
	{0x50090224,0x00000044},                      
	{0x50090228,0x00000046},                      
	{0x5009022c,0x00000048},                      
	{0x50090230,0x0000004a},                      
	{0x50090234,0x00000080},                      
	{0x50090238,0x00000082},                      
	{0x5009023c,0x00000084},                      
	{0x50090240,0x00000086},                      
	{0x50090244,0x00000088},                      
	{0x50090248,0x0000008a},                      
	{0x5009024c,0x000000c0},                      
	{0x50090250,0x000000c2},                      
	{0x50090254,0x000000c4},                      
	{0x50090258,0x000000c6},                      
	{0x5009025c,0x000000c8},                      
	{0x50090260,0x000000ca},                      
	{0x50090264,0x00000100},                      
	{0x50090268,0x00000102},                      
	{0x5009026c,0x00000104},                      
	{0x50090270,0x00000106},                      
	{0x50090274,0x00000108},                      
	{0x50090278,0x00000140},                      
	{0x5009027c,0x00000142},//lna =0 end          
	{0x50090280,0x00000080},                      
	{0x50090284,0x00000082},                      
	{0x50090288,0x00000084},                      
	{0x5009028c,0x00000086},                      
	{0x50090290,0x00000088},                      
	{0x50090294,0x0000008a},                      
	{0x50090298,0x000000c0},                      
	{0x5009029c,0x000000c2},                      
	{0x500902a0,0x000000c4},                      
	{0x500902a4,0x000000c6},                      
	{0x500902a8,0x000000c8},                      
	{0x500902ac,0x000000ca},                      
	{0x500902b0,0x00000100},                      
	{0x500902b4,0x00000102},                      
	{0x500902b8,0x00000104},                      
	{0x500902bc,0x00000106},                      
	{0x500902c0,0x00000108},                      
	{0x500902c4,0x00000140},                      
	{0x500902c8,0x00000142},                      
	{0x500902cc,0x00000144},                      
	{0x500902d0,0x00000146},                      
	{0x500902d4,0x00000148},                      
	{0x500902d8,0x00000180},                      
	{0x500902dc,0x00000182},                      
	{0x500902e0,0x00000184},                      
	{0x500902e4,0x000001c0},                      
	{0x500902e8,0x000001c2},                      
	{0x500902ec,0x000001c4},                      
	{0x500902f0,0x000001c6},                      
	{0x500902f4,0x000001c8},                      
	{0x500902f8,0x000001ca},                      
	{0x500902fc,0x000001cc},// lna = 01  end      
	{0x50090300,0x00000102},                      
	{0x50090304,0x00000104},                      
	{0x50090308,0x00000106},                      
	{0x5009030c,0x00000108},                      
	{0x50090310,0x00000140},                      
	{0x50090314,0x00000142},                      
	{0x50090318,0x00000144},                      
	{0x5009031c,0x00000146},                      
	{0x50090320,0x00000148},                      
	{0x50090324,0x00000180},                      
	{0x50090328,0x00000182},                      
	{0x5009032c,0x00000184},                      
	{0x50090330,0x000001c0},                      
	{0x50090334,0x000001c2},                      
	{0x50090338,0x000001c4},                      
	{0x5009033c,0x000001c6},                      
	{0x50090340,0x000001c8},                      
	{0x50090344,0x000001c9},                      
	{0x50090348,0x000001c9},                      
	{0x5009034c,0x000001c9},                      
	{0x50090350,0x000001c9},                      
	{0x50090354,0x000001c9},                      
	{0x50090358,0x000001c9},                      
	{0x5009035c,0x000001c9},                      
	{0x50090360,0x000001c9},                      
	{0x50090364,0x000001c9},                      
	{0x50090368,0x000001c9},                      
	{0x5009036c,0x000001c9},                      
	{0x50090370,0x000001c9},                      
	{0x50090374,0x000001c9},                      
	{0x50090378,0x000001c9},                      
	{0x5009037c,0x000001c9},                      
	{0x50090054,0x00000000},//disable update      
	{0x50000808,0x65000013}, // disable prerx_prio
	//pta config                                  
	{0x50000810,0xFFCD0F01},  //rx beacon priority
};
 
int rda5890_set_test_mode(struct rda5890_private *priv)
{
	int ret = 0;
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_CRIT,
                "%s >>> \n", __func__);

    sdio_init_complete = 0;
    sdio_patch_complete = 0;
    ret = rda5890_sdio_patch_core_32(priv);
    if(ret < 0)
        goto err;
    sdio_patch_complete = 1;
  
	ret = rda5890_write_sdio8_polling(priv, wifi_core_patch_data_8, 
                                     sizeof(wifi_core_patch_data_8)/sizeof(wifi_core_patch_data_8[0]));
    if(ret < 0)
        goto err; 
    
    ret = rda5890_write_sdio32_polling(priv, rda5990_test_mode_digital32, 
                                       sizeof(rda5990_test_mode_digital32)/sizeof(rda5990_test_mode_digital32[0]));
 	  if(ret < 0)
        goto err;
        
    ret = rda5890_write_sdio32_polling(priv, wifi_test_mode_agc_patch32, 
                                      sizeof(wifi_test_mode_agc_patch32)/sizeof(wifi_test_mode_agc_patch32[0]));
 	  if(ret < 0)
        goto err;
        
    ret = rda5890_write_sdio32_polling(priv, wifi_test_mode_rx_notch_32, 
                                       sizeof(wifi_test_mode_rx_notch_32)/sizeof(wifi_test_mode_rx_notch_32[0]));
    if(ret < 0)
        goto err;
    

    ret = rda5890_sdio_set_default_notch_polling(priv);
    if(ret < 0)
        goto err;    

    export_wifi_eirq_enable();
    sdio_init_complete = 1;
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_CRIT,
                "%s <<< \n", __func__);
    return ret;
    
err:
    RDA5890_DBGLAP(RDA5890_DA_SDIO, RDA5890_DL_CRIT, "rda5890_set_test_mode err!! \n" ); 
    return ret;
} 
#endif

 