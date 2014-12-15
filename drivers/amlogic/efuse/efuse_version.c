#include <linux/types.h>
#include <mach/am_regs.h>
#include "efuse_regs.h"
#include <linux/amlogic/efuse.h>


/**
 * efuse version 0.1 (for M3 ) 
 * M3 efuse: read all free efuse data maybe fail on addr 0 and addr 0x40
 * so M3 EFUSE layout avoid using 0 and 0x40
title				offset			datasize			checksize			totalsize			
reserved 		0					0						0						4
usid				4					33					2						35
mac_wifi		39				6						1						7
mac_bt		46				6						1						7
mac				53				6						1						7
licence			60				3						1						4
reserved 		64				0						0						4
hdcp			68				300					10					310
reserved		378				0						0						2
version		380				3						1						4    (version+machid, version=1)
*/

static efuseinfo_item_t efuseinfo_v0[] = 
{
	{
		.title = "licence",
		.id = EFUSE_LICENCE_ID,
		.offset = 0,
		.enc_len = 4,			
		.data_len = 3,			
		.bch_en = 1,
		.bch_reverse = 1,
	},
	{
		.title = "mac",
		.id = EFUSE_MAC_ID,
		.offset = 4,
		.enc_len = 7,
		.data_len = 6,		
		.bch_en = 1,
		.bch_reverse = 1,
	},
	{
		.title = "hdcp",
		.id = EFUSE_HDCP_ID,
		.offset = 12,
		.enc_len = 310,
		.data_len = 300,		
		.bch_en = 1,
		.bch_reverse = 1,
	},
	{
		.title = "mac_bt",
		.id = EFUSE_MAC_BT_ID,
		.offset = 322,
		.enc_len = 7,
		.data_len = 6,		
		.bch_en = 1,
		.bch_reverse = 1,
	},
	{
		.title = "mac_wifi",
		.id = EFUSE_MAC_WIFI_ID,
		.offset = 330,
		.enc_len = 7,
		.data_len = 6,		
		.bch_en = 1,
		.bch_reverse = 1,
	},
	{
		.title = "usid",
		.id = EFUSE_USID_ID,
		.offset = V0_EFUSE_USID_OFFSET, //337,
		.enc_len = V0_EFUSE_USID_ENC_LEN, //43,
		.data_len = V0_EFUSE_USID_DATA_LEN, //41,		
		.bch_en = V0_EFUSE_USID_BCH_EN, //1,
		.bch_reverse = V0_EFUSE_USID_BCH_REVERSE, //1,
	},	
	{
		.title= "version",     //1B(version=0)+2B(machid)
		.id = EFUSE_VERSION_ID,
		.offset=EFUSE_VERSION_OFFSET, //380,
		.enc_len = EFUSE_VERSION_ENC_LEN, //4,
		.data_len = EFUSE_VERSION_DATA_LEN, //3,		
		.bch_en = EFUSE_VERSION_BCH_EN, //1,
		.bch_reverse = EFUSE_VERSION_BCH_REVERSE, //0,
	},
};

static efuseinfo_item_t efuseinfo_v1[] =
{
    {
        .title = "usid",
        .id = EFUSE_USID_ID,
        .offset = V1_EFUSE_USID_OFFSET, //4,
        .enc_len = V1_EFUSE_USID_ENC_LEN, //35,
        .data_len = V1_EFUSE_USID_DATA_LEN, //33,
        .bch_en = V1_EFUSE_USID_BCH_EN,  //1,
        .bch_reverse = V1_EFUSE_USID_BCH_REVERSE, //0,
    },
    {
        .title = "mac_wifi",
        .id = EFUSE_MAC_WIFI_ID,
        .offset = 39,
        .enc_len = 7,
        .data_len = 6,
        .bch_en = 1,
        .bch_reverse = 0,
    },
    {
        .title = "mac_bt",
        .id = EFUSE_MAC_BT_ID,
        .offset = 46,
        .enc_len = 7,
        .data_len = 6,
        .bch_en = 1,
        .bch_reverse = 0,
    },
    {
        .title = "mac",
        .id = EFUSE_MAC_ID,
        .offset = 53,
        .enc_len = 7,
        .data_len = 6,
        .bch_en = 1,
        .bch_reverse = 0,
    },
    {
        .title = "licence",
        .id = EFUSE_LICENCE_ID,
        .offset = 60,
        .enc_len = 4,
        .data_len = 3,
        .bch_en = 1,
        .bch_reverse = 1,
    },
    {
        .title = "hdcp",
        .id = EFUSE_HDCP_ID,
        .offset = 68,
        .enc_len = 310,
        .data_len = 300,
        .bch_en = 1,
        .bch_reverse = 0,
    },
    {
        .title= "version",     //1B(version=1)+2B(machid)
        .id = EFUSE_VERSION_ID,
        .offset= EFUSE_VERSION_OFFSET, //380,
        .enc_len = EFUSE_VERSION_ENC_LEN, //4,
        .data_len = EFUSE_VERSION_DATA_LEN, //3,
        .bch_en = EFUSE_VERSION_BCH_EN, //1,
        .bch_reverse = EFUSE_VERSION_BCH_REVERSE, //0,
    },
};
#ifdef CONFIG_ARCH_MESON3
static efuseinfo_item_t efuseinfo_v3[] =
{
    {
        .title = "usid",
        .id = EFUSE_USID_ID,
        .offset = V1_EFUSE_USID_OFFSET, //4,
        .enc_len = V1_EFUSE_USID_ENC_LEN, //35,
        .data_len = V1_EFUSE_USID_DATA_LEN, //33,
        .bch_en = V1_EFUSE_USID_BCH_EN,  //1,
        .bch_reverse = V1_EFUSE_USID_BCH_REVERSE, //0,
    },
    {
        .title = "mac_wifi",
        .id = EFUSE_MAC_WIFI_ID,
        .offset = 39,
        .enc_len = 7,
        .data_len = 6,
        .bch_en = 1,
        .bch_reverse = 0,
    },
    {
        .title = "mac_bt",
        .id = EFUSE_MAC_BT_ID,
        .offset = 46,
        .enc_len = 7,
        .data_len = 6,
        .bch_en = 1,
        .bch_reverse = 0,
    },
    {
        .title = "mac",
        .id = EFUSE_MAC_ID,
        .offset = 53,
        .enc_len = 7,
        .data_len = 6,
        .bch_en = 1,
        .bch_reverse = 0,
    },
    {
        .title = "licence",
        .id = EFUSE_LICENCE_ID,
        .offset = 60,
        .enc_len = 4,
        .data_len = 3,
        .bch_en = 1,
        .bch_reverse = 1,
    },

    {
        .title= "version",     //1B(version=1)+2B(machid)
        .id = EFUSE_VERSION_ID,
        .offset= EFUSE_VERSION_OFFSET, //380,
        .enc_len = EFUSE_VERSION_ENC_LEN, //4,
        .data_len = EFUSE_VERSION_DATA_LEN, //3,
        .bch_en = EFUSE_VERSION_BCH_EN, //1,
        .bch_reverse = EFUSE_VERSION_BCH_REVERSE, //0,
    },
};
#endif
// after M6
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6

static efuseinfo_item_t efuseinfo_v3[] =
{
    {
        .title = "licence",
        .id = EFUSE_LICENCE_ID,
        .offset = 0,
        .enc_len = 3,
        .data_len = 3,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "version",   // include machid
        .id = EFUSE_VERSION_ID,
        .offset = V2_EFUSE_VERSION_OFFSET, //3,
        .enc_len = V2_EFUSE_VERSION_ENC_LEN, //1,
        .data_len = V2_EFUSE_VERSION_DATA_LEN, //1,
        .bch_en = V2_EFUSE_VERSION_BCH_EN, //0,
        .bch_reverse = V2_EFUSE_VERSION_BCH_REVERSE, //0,
    },
    {
        .title = "customerid",   // include machid
        .id = EFUSE_CUSTOMER_ID,
        .offset = 4,
        .enc_len = 4,
        .data_len = 4,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "rsakey",
        .offset = 8,
        .id = EFUSE_RSA_KEY_ID,
        .enc_len = 128,
        .data_len = 128,
        .bch_en = 0,
        .bch_reverse = 0,
    },

    {
        .title = "mac",    //for the main network interface
        .id = EFUSE_MAC_ID,
        .offset = 436,
        .enc_len = 6,
        .data_len = 6,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "mac_bt",  //for the second network interface or bt
        .id = EFUSE_MAC_BT_ID,
        .offset = 442,
        .enc_len = 6,
        .data_len = 6,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "mac_wifi", //for the second network interface or bt
        .id = EFUSE_MAC_WIFI_ID,
        .offset = 448,
        .enc_len = 6,
        .data_len = 6,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "usid",
        .id = EFUSE_USID_ID,
        .offset = V2_EFUSE_USID_OFFSET, //454,
        .enc_len = V2_EFUSE_USID_ENC_LEN, //58,
        .data_len = V2_EFUSE_USID_DATA_LEN, //58,
        .bch_en = V2_EFUSE_USID_BCH_EN, //0,
        .bch_reverse = V2_EFUSE_USID_BCH_REVERSE, //0,
    },
    {
        .title = "machineid",
        .id = EFUSE_MACHINEID_ID,
        .offset = 502, //502,
        .enc_len = 4, //4,
        .data_len = 4, //4,
        .bch_en = 0, //0,
        .bch_reverse = 0, //0,
    },
};

static efuseinfo_item_t efuseinfo_v4[] = {
    {
        .title = "licence",
        .id = EFUSE_LICENCE_ID,
        .offset = 0,
        .enc_len = 3,
        .data_len = 3,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "version",   // include machid
        .id = EFUSE_VERSION_ID,
        .offset = V2_EFUSE_VERSION_OFFSET, //3,
        .enc_len = V2_EFUSE_VERSION_ENC_LEN, //1,
        .data_len = V2_EFUSE_VERSION_DATA_LEN, //1,
        .bch_en = V2_EFUSE_VERSION_BCH_EN, //0,
        .bch_reverse = V2_EFUSE_VERSION_BCH_REVERSE, //0,
    },
    {
        .title = "customerid",   // include machid
        .id = EFUSE_CUSTOMER_ID,
        .offset = 4,
        .enc_len = 4,
        .data_len = 4,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "rsakey",
        .id = EFUSE_RSA_KEY_ID,
        .offset = 8,
        .enc_len = 128,
        .data_len = 128,
        .bch_en = 0,
        .bch_reverse = 0,
    },
	{
        .title = "secu-boot",
        .offset = 136,
        .enc_len = 156,
        .data_len = 156,
        .bch_en = 0,
        .bch_reverse = 0,
    },    
    {
        .title = "reserved",
        .offset = 292,
        .enc_len = 112,
        .data_len = 112,
        .bch_en = 0,
        .bch_reverse = 0,
    },    
    {
        .title = "storagekey",
        .offset = 404,
        .enc_len = 32,
        .data_len = 32,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "mac",    //for the main network interface
        .id = EFUSE_MAC_ID,
        .offset = 436,
        .enc_len = 6,
        .data_len = 6,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "mac_bt",  //for the second network interface or bt
        .id = EFUSE_MAC_BT_ID,
        .offset = 442,
        .enc_len = 6,
        .data_len = 6,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "mac_wifi", //for the second network interface or bt
	.id = EFUSE_MAC_WIFI_ID,
        .offset = 448,
        .enc_len = 6,
        .data_len = 6,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "usid",
	.id = EFUSE_USID_ID,
        .offset = V2_EFUSE_USID_OFFSET,// 454,
        .enc_len = V2_EFUSE_USID_ENC_LEN, //58,
        .data_len = V2_EFUSE_USID_DATA_LEN, //58,
        .bch_en = V2_EFUSE_USID_BCH_EN, //0,
        .bch_reverse = V2_EFUSE_USID_BCH_REVERSE, //0,
    },
    {
        .title = "machineid",
	.id = EFUSE_MACHINEID_ID,
        .offset = 502,
        .enc_len = 4,
        .data_len = 4,
        .bch_en = 0,
        .bch_reverse = 0,
    },
    {
        .title = "reserved",
        .offset = 506,
        .enc_len = 6,
        .data_len = 6,
        .bch_en = 0,
        .bch_reverse = 0,
    },
        
};

#endif///endif efuseinfo version3
static efuseinfo_item_t efuseinfo_v2[] = 
{
	{
		.title = "licence",
		.id = EFUSE_LICENCE_ID,
		.offset = 0,
		.enc_len = 3,
		.data_len = 3,		
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "version",   // include machid
		.id = EFUSE_VERSION_ID,
		.offset = V2_EFUSE_VERSION_OFFSET, //3,
		.enc_len = V2_EFUSE_VERSION_ENC_LEN, //1,
		.data_len = V2_EFUSE_VERSION_DATA_LEN, //1,		
		.bch_en = V2_EFUSE_VERSION_BCH_EN, //0,	
		.bch_reverse = V2_EFUSE_VERSION_BCH_REVERSE, //0,
	},
	{
		.title = "customerid",   // include machid
		.id = EFUSE_CUSTOMER_ID,
		.offset = 4,
		.enc_len = 4,
		.data_len = 4,		
		.bch_en = 0,	
		.bch_reverse = 0,
	},
	{
		.title = "rsakey",
		.offset = 8,
		.id = EFUSE_RSA_KEY_ID,
		.enc_len = 128,
		.data_len = 128,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "hdcp",
		.offset=136,
		.id = EFUSE_HDCP_ID,
		.enc_len = 300,
		.data_len = 300,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "mac",    //for the main network interface
		.id = EFUSE_MAC_ID,
		.offset = 436,
		.enc_len = 6,
		.data_len = 6,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "mac_bt",  //for the second network interface or bt
		.id = EFUSE_MAC_BT_ID,
		.offset = 442,
		.enc_len = 6,
		.data_len = 6,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "mac_wifi", //for the second network interface or bt
		.id = EFUSE_MAC_WIFI_ID,
		.offset = 448,
		.enc_len = 6,
		.data_len = 6,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "usid",   
		.id = EFUSE_USID_ID,
		.offset = V2_EFUSE_USID_OFFSET, //454,
		.enc_len = V2_EFUSE_USID_ENC_LEN, //58,
		.data_len = V2_EFUSE_USID_DATA_LEN, //58,
		.bch_en = V2_EFUSE_USID_BCH_EN, //0,
		.bch_reverse = V2_EFUSE_USID_BCH_REVERSE, //0,
	},
	{
		.title = "machineid",   
		.id = EFUSE_MACHINEID_ID,
		.offset = 502, //502,
		.enc_len = 4, //4,
		.data_len = 4, //4,
		.bch_en = 0, //0,
		.bch_reverse = 0, //0,
	},
};

//m8 efuse layout according to haixiang.bao allocation
static efuseinfo_item_t efuseinfo_M8_serialNum_v1[] = 
{
	{
		.title = "licence",
		.id = EFUSE_LICENCE_ID,
		.offset = 0,
		.enc_len = 4,
		.data_len = 4,		
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "nandextcmd",
		.id = EFUSE_NANDEXTCMD_ID,
		.offset = 4,
		.enc_len = 16,
		.data_len = 16,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "mac",  //for the main network interface
		.id = EFUSE_MAC_ID,
		.offset = 436,
		.enc_len = 6,
		.data_len = 6,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "mac_bt",  //for the second network interface or bt
		.id = EFUSE_MAC_BT_ID,
		.offset = 442,
		.enc_len = 6,
		.data_len = 6,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "mac_wifi", //for the second network interface or bt
		.id = EFUSE_MAC_WIFI_ID,
		.offset = 448,
		.enc_len = 6,
		.data_len = 6,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "usid",
		.id = EFUSE_USID_ID,
		.offset = 454,
		.enc_len = 48,
		.data_len = 48,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "version",
		.id = EFUSE_VERSION_ID,
		.offset = M8_EFUSE_VERSION_OFFSET, //509
		.enc_len = M8_EFUSE_VERSION_ENC_LEN,
		.data_len = M8_EFUSE_VERSION_DATA_LEN,
		.bch_en = M8_EFUSE_VERSION_BCH_EN,
		.bch_reverse = M8_EFUSE_VERSION_BCH_REVERSE,
	},
};

//M6TVD layout
static efuseinfo_item_t efuseinfo_m6tvd_serialNum_v1[] = 
{
	{
		.title = "licence",
		.id = EFUSE_LICENCE_ID,
		.offset = 0,
		.enc_len = 4,
		.data_len = 4,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "mac",    //for the main network interface
		.id = EFUSE_MAC_ID,
		.offset = 436,
		.enc_len = 6,
		.data_len = 6,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "mac_bt",  //for the second network interface or bt
		.id = EFUSE_MAC_BT_ID,
		.offset = 442,
		.enc_len = 6,
		.data_len = 6,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "mac_wifi", //for the second network interface or bt
		.id = EFUSE_MAC_WIFI_ID,
		.offset = 448,
		.enc_len = 6,
		.data_len = 6,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "usid",
		.id = EFUSE_USID_ID,
		.offset = 454,
		.enc_len = 48,
		.data_len = 48,
		.bch_en = 0,
		.bch_reverse = 0,
	},
	{
		.title = "version",
		.id = EFUSE_VERSION_ID,
		.offset = M6TVD_EFUSE_VERSION_OFFSET, //509
		.enc_len = M6TVD_EFUSE_VERSION_ENC_LEN, //1
		.data_len = M6TVD_EFUSE_VERSION_DATA_LEN,//1
		.bch_en = M6TVD_EFUSE_VERSION_BCH_EN, //0
		.bch_reverse = M6TVD_EFUSE_VERSION_BCH_REVERSE, //0
	},
};


efuseinfo_t efuseinfo[] = 
{
	{
		.efuseinfo_version = efuseinfo_v0,
		.size = sizeof(efuseinfo_v0)/sizeof(efuseinfo_item_t),
		.version =0,	
	},
	{
		.efuseinfo_version = efuseinfo_v1,
		.size = sizeof(efuseinfo_v1)/sizeof(efuseinfo_item_t),
		.version =1,		
	},
	{
		.efuseinfo_version = efuseinfo_v2,
		.size = sizeof(efuseinfo_v2)/sizeof(efuseinfo_item_t),
		.version =2,		
	},
	{
		.efuseinfo_version = efuseinfo_v3,
		.size = sizeof(efuseinfo_v3)/sizeof(efuseinfo_item_t),
		.version =3,
	},
	{
		.efuseinfo_version = efuseinfo_v4,
		.size = sizeof(efuseinfo_v4) / sizeof(efuseinfo_item_t),
		.version = 4,
	},
	{
		.efuseinfo_version = efuseinfo_M8_serialNum_v1,
		.size = sizeof(efuseinfo_M8_serialNum_v1)/sizeof(efuseinfo_item_t),
		.version =M8_EFUSE_VERSION_SERIALNUM_V1,
	},
	{
		.efuseinfo_version = efuseinfo_m6tvd_serialNum_v1,
		.size = sizeof(efuseinfo_m6tvd_serialNum_v1)/sizeof(efuseinfo_item_t),
		.version =M6TVD_EFUSE_VERSION_SERIALNUM_V1,
	},
};

int efuseinfo_num = sizeof(efuseinfo)/sizeof(efuseinfo_t);
int efuse_active_version = -1;
unsigned efuse_active_customerid = 0;
pfn efuse_getinfoex = 0;
pfn efuse_getinfoex_byPos = 0;
pfn efuse_getinfoex_byTitle = 0;

