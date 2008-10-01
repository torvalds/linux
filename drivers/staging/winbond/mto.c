//============================================================================
//  MTO.C -
//
//  Description:
//    MAC Throughput Optimization for W89C33 802.11g WLAN STA.
//
//    The following MIB attributes or internal variables will be affected
//    while the MTO is being executed:
//       dot11FragmentationThreshold,
//       dot11RTSThreshold,
//       transmission rate and PLCP preamble type,
//       CCA mode,
//       antenna diversity.
//
//  Revision history:
//  --------------------------------------------------------------------------
//           20031227  UN20 Pete Chao
//                     First draft
//  20031229           Turbo                copy from PD43
//  20040210           Kevin                revised
//  Copyright (c) 2003 Winbond Electronics Corp. All rights reserved.
//============================================================================

// LA20040210_DTO kevin
#include "os_common.h"

// Declare SQ3 to rate and fragmentation threshold table
// Declare fragmentation thresholds table
#define MTO_MAX_SQ3_LEVELS                      14
#define MTO_MAX_FRAG_TH_LEVELS                  5
#define MTO_MAX_DATA_RATE_LEVELS                12

u16 MTO_Frag_Th_Tbl[MTO_MAX_FRAG_TH_LEVELS] =
{
    256, 384, 512, 768, 1536
};

u8  MTO_SQ3_Level[MTO_MAX_SQ3_LEVELS] =
{
    0, 26, 30, 32, 34, 35, 37, 42, 44, 46, 54, 62, 78, 81
};
u8  MTO_SQ3toRate[MTO_MAX_SQ3_LEVELS] =
{
    0, 1, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};
u8  MTO_SQ3toFrag[MTO_MAX_SQ3_LEVELS] =
{
    0, 2, 2, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4
};

// One Exchange Time table
//
u16 MTO_One_Exchange_Time_Tbl_l[MTO_MAX_FRAG_TH_LEVELS][MTO_MAX_DATA_RATE_LEVELS] =
{
    { 2554, 1474,  822,    0,    0,  636,    0,    0,    0,    0,    0,    0},
    { 3578, 1986, 1009,    0,    0,  729,    0,    0,    0,    0,    0,    0},
    { 4602, 2498, 1195,    0,    0,  822,    0,    0,    0,    0,    0,    0},
    { 6650, 3522, 1567,    0,    0, 1009,    0,    0,    0,    0,    0,    0},
    {12794, 6594, 2684,    0,    0, 1567,    0,    0,    0,    0,    0,    0}
};

u16 MTO_One_Exchange_Time_Tbl_s[MTO_MAX_FRAG_TH_LEVELS][MTO_MAX_DATA_RATE_LEVELS] =
{
    {    0, 1282,  630,  404,  288,  444,  232,  172,  144,  116,  100,   96},
    {    0, 1794,  817,  572,  400,  537,  316,  228,  188,  144,  124,  116},
    {    0, 2306, 1003,  744,  516,  630,  400,  288,  228,  172,  144,  136},
    {    0, 3330, 1375, 1084,  744,  817,  572,  400,  316,  228,  188,  172},
    {    0, 6402, 2492, 2108, 1424, 1375, 1084,  740,  572,  400,  316,  284}
};

#define MTO_ONE_EXCHANGE_TIME(preamble_type, frag_th_lvl, data_rate_lvl) \
            (preamble_type) ?   MTO_One_Exchange_Time_Tbl_s[frag_th_lvl][data_rate_lvl] : \
                                MTO_One_Exchange_Time_Tbl_l[frag_th_lvl][data_rate_lvl]

// Declare data rate table
//The following table will be changed at anytime if the opration rate supported by AP don't
//match the table
u8  MTO_Data_Rate_Tbl[MTO_MAX_DATA_RATE_LEVELS] =
{
    2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108
};

//The Stardard_Data_Rate_Tbl and Level2PerTbl table is used to indirectly retreive PER
//information from Rate_PER_TBL
//The default settings is AP can support full rate set.
static u8  Stardard_Data_Rate_Tbl[MTO_MAX_DATA_RATE_LEVELS] =
{
	2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108
};
static u8  Level2PerTbl[MTO_MAX_DATA_RATE_LEVELS] =
{
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};
//How many kind of tx rate can be supported by AP
//DTO will change Rate between MTO_Data_Rate_Tbl[0] and MTO_Data_Rate_Tbl[MTO_DataRateAvailableLevel-1]
static u8  MTO_DataRateAvailableLevel = MTO_MAX_DATA_RATE_LEVELS;
//Smoothed PER table for each different RATE based on packet length of 1514
static int Rate_PER_TBL[91][MTO_MAX_DATA_RATE_LEVELS] = {
//        1M    2M    5.5M  11M   6M    9M    12M     18M    24M    36M    48M   54M
/* 0%  */{ 93,   177,  420,  538,  690,  774,  1001,  1401,  1768,  2358,  2838,  3039},
/* 1%  */{ 92,   176,  416,  533,  683,  767,  992,   1389,  1752,  2336,  2811,  3010},
/* 2%  */{ 91,   174,  412,  528,  675,  760,  983,   1376,  1735,  2313,  2783,  2979},
/* 3%  */{ 90,   172,  407,  523,  667,  753,  973,   1363,  1719,  2290,  2755,  2948},
/* 4%  */{ 90,   170,  403,  518,  659,  746,  964,   1350,  1701,  2266,  2726,  2916},
/* 5%  */{ 89,   169,  398,  512,  651,  738,  954,   1336,  1684,  2242,  2696,  2884},
/* 6%  */{ 88,   167,  394,  507,  643,  731,  944,   1322,  1666,  2217,  2665,  2851},
/* 7%  */{ 87,   165,  389,  502,  635,  723,  935,   1308,  1648,  2192,  2634,  2817},
/* 8%  */{ 86,   163,  384,  497,  626,  716,  924,   1294,  1629,  2166,  2602,  2782},
/* 9%  */{ 85,   161,  380,  491,  618,  708,  914,   1279,  1611,  2140,  2570,  2747},
/* 10% */{ 84,   160,  375,  486,  609,  700,  904,   1265,  1591,  2113,  2537,  2711},
/* 11% */{ 83,   158,  370,  480,  600,  692,  894,   1250,  1572,  2086,  2503,  2675},
/* 12% */{ 82,   156,  365,  475,  592,  684,  883,   1234,  1552,  2059,  2469,  2638},
/* 13% */{ 81,   154,  360,  469,  583,  676,  872,   1219,  1532,  2031,  2435,  2600},
/* 14% */{ 80,   152,  355,  464,  574,  668,  862,   1204,  1512,  2003,  2400,  2562},
/* 15% */{ 79,   150,  350,  458,  565,  660,  851,   1188,  1492,  1974,  2365,  2524},
/* 16% */{ 78,   148,  345,  453,  556,  652,  840,   1172,  1471,  1945,  2329,  2485},
/* 17% */{ 77,   146,  340,  447,  547,  643,  829,   1156,  1450,  1916,  2293,  2446},
/* 18% */{ 76,   144,  335,  441,  538,  635,  818,   1140,  1429,  1887,  2256,  2406},
/* 19% */{ 75,   143,  330,  436,  529,  627,  807,   1124,  1408,  1857,  2219,  2366},
/* 20% */{ 74,   141,  325,  430,  520,  618,  795,   1107,  1386,  1827,  2182,  2326},
/* 21% */{ 73,   139,  320,  424,  510,  610,  784,   1091,  1365,  1797,  2145,  2285},
/* 22% */{ 72,   137,  314,  418,  501,  601,  772,   1074,  1343,  1766,  2107,  2244},
/* 23% */{ 71,   135,  309,  412,  492,  592,  761,   1057,  1321,  1736,  2069,  2203},
/* 24% */{ 70,   133,  304,  407,  482,  584,  749,   1040,  1299,  1705,  2031,  2161},
/* 25% */{ 69,   131,  299,  401,  473,  575,  738,   1023,  1277,  1674,  1992,  2120},
/* 26% */{ 68,   129,  293,  395,  464,  566,  726,   1006,  1254,  1642,  1953,  2078},
/* 27% */{ 67,   127,  288,  389,  454,  557,  714,   989,   1232,  1611,  1915,  2035},
/* 28% */{ 66,   125,  283,  383,  445,  549,  703,   972,   1209,  1579,  1876,  1993},
/* 29% */{ 65,   123,  278,  377,  436,  540,  691,   955,   1187,  1548,  1836,  1951},
/* 30% */{ 64,   121,  272,  371,  426,  531,  679,   937,   1164,  1516,  1797,  1908},
/* 31% */{ 63,   119,  267,  365,  417,  522,  667,   920,   1141,  1484,  1758,  1866},
/* 32% */{ 62,   117,  262,  359,  407,  513,  655,   902,   1118,  1453,  1719,  1823},
/* 33% */{ 61,   115,  256,  353,  398,  504,  643,   885,   1095,  1421,  1679,  1781},
/* 34% */{ 60,   113,  251,  347,  389,  495,  631,   867,   1072,  1389,  1640,  1738},
/* 35% */{ 59,   111,  246,  341,  379,  486,  619,   850,   1049,  1357,  1600,  1695},
/* 36% */{ 58,   108,  240,  335,  370,  477,  607,   832,   1027,  1325,  1561,  1653},
/* 37% */{ 57,   106,  235,  329,  361,  468,  595,   815,   1004,  1293,  1522,  1610},
/* 38% */{ 56,   104,  230,  323,  351,  459,  584,   797,   981,   1261,  1483,  1568},
/* 39% */{ 55,   102,  224,  317,  342,  450,  572,   780,   958,   1230,  1443,  1526},
/* 40% */{ 54,   100,  219,  311,  333,  441,  560,   762,   935,   1198,  1404,  1484},
/* 41% */{ 53,   98,   214,  305,  324,  432,  548,   744,   912,   1166,  1366,  1442},
/* 42% */{ 52,   96,   209,  299,  315,  423,  536,   727,   889,   1135,  1327,  1400},
/* 43% */{ 51,   94,   203,  293,  306,  414,  524,   709,   866,   1104,  1289,  1358},
/* 44% */{ 50,   92,   198,  287,  297,  405,  512,   692,   844,   1072,  1250,  1317},
/* 45% */{ 49,   90,   193,  281,  288,  396,  500,   675,   821,   1041,  1212,  1276},
/* 46% */{ 48,   88,   188,  275,  279,  387,  488,   657,   799,   1011,  1174,  1236},
/* 47% */{ 47,   86,   183,  269,  271,  378,  476,   640,   777,   980,   1137,  1195},
/* 48% */{ 46,   84,   178,  262,  262,  369,  464,   623,   754,   949,   1100,  1155},
/* 49% */{ 45,   82,   173,  256,  254,  360,  452,   606,   732,   919,   1063,  1116},
/* 50% */{ 44,   80,   168,  251,  245,  351,  441,   589,   710,   889,   1026,  1076},
/* 51% */{ 43,   78,   163,  245,  237,  342,  429,   572,   689,   860,   990,   1038},
/* 52% */{ 42,   76,   158,  239,  228,  333,  417,   555,   667,   830,   955,   999},
/* 53% */{ 41,   74,   153,  233,  220,  324,  406,   538,   645,   801,   919,   961},
/* 54% */{ 40,   72,   148,  227,  212,  315,  394,   522,   624,   773,   884,   924},
/* 55% */{ 39,   70,   143,  221,  204,  307,  383,   505,   603,   744,   850,   887},
/* 56% */{ 38,   68,   138,  215,  196,  298,  371,   489,   582,   716,   816,   851},
/* 57% */{ 37,   67,   134,  209,  189,  289,  360,   473,   562,   688,   783,   815},
/* 58% */{ 36,   65,   129,  203,  181,  281,  349,   457,   541,   661,   750,   780},
/* 59% */{ 35,   63,   124,  197,  174,  272,  338,   441,   521,   634,   717,   745},
/* 60% */{ 34,   61,   120,  192,  166,  264,  327,   425,   501,   608,   686,   712},
/* 61% */{ 33,   59,   115,  186,  159,  255,  316,   409,   482,   582,   655,   678},
/* 62% */{ 32,   57,   111,  180,  152,  247,  305,   394,   462,   556,   624,   646},
/* 63% */{ 31,   55,   107,  174,  145,  238,  294,   379,   443,   531,   594,   614},
/* 64% */{ 30,   53,   102,  169,  138,  230,  283,   364,   425,   506,   565,   583},
/* 65% */{ 29,   52,   98,   163,  132,  222,  273,   349,   406,   482,   536,   553},
/* 66% */{ 28,   50,   94,   158,  125,  214,  262,   334,   388,   459,   508,   523},
/* 67% */{ 27,   48,   90,   152,  119,  206,  252,   320,   370,   436,   481,   495},
/* 68% */{ 26,   46,   86,   147,  113,  198,  242,   306,   353,   413,   455,   467},
/* 69% */{ 26,   44,   82,   141,  107,  190,  231,   292,   336,   391,   429,   440},
/* 70% */{ 25,   43,   78,   136,  101,  182,  221,   278,   319,   370,   405,   414},
/* 71% */{ 24,   41,   74,   130,  95,   174,  212,   265,   303,   350,   381,   389},
/* 72% */{ 23,   39,   71,   125,  90,   167,  202,   252,   287,   329,   358,   365},
/* 73% */{ 22,   37,   67,   119,  85,   159,  192,   239,   271,   310,   335,   342},
/* 74% */{ 21,   36,   63,   114,  80,   151,  183,   226,   256,   291,   314,   320},
/* 75% */{ 20,   34,   60,   109,  75,   144,  174,   214,   241,   273,   294,   298},
/* 76% */{ 19,   32,   57,   104,  70,   137,  164,   202,   227,   256,   274,   278},
/* 77% */{ 18,   31,   53,   99,   66,   130,  155,   190,   213,   239,   256,   259},
/* 78% */{ 17,   29,   50,   94,   62,   122,  146,   178,   200,   223,   238,   241},
/* 79% */{ 16,   28,   47,   89,   58,   115,  138,   167,   187,   208,   222,   225},
/* 80% */{ 16,   26,   44,   84,   54,   109,  129,   156,   175,   194,   206,   209},
/* 81% */{ 15,   24,   41,   79,   50,   102,  121,   146,   163,   180,   192,   194},
/* 82% */{ 14,   23,   39,   74,   47,   95,   113,   136,   151,   167,   178,   181},
/* 83% */{ 13,   21,   36,   69,   44,   89,   105,   126,   140,   155,   166,   169},
/* 84% */{ 12,   20,   33,   64,   41,   82,   97,    116,   130,   144,   155,   158},
/* 85% */{ 11,   19,   31,   60,   39,   76,   89,    107,   120,   134,   145,   149},
/* 86% */{ 11,   17,   29,   55,   36,   70,   82,    98,    110,   125,   136,   140},
/* 87% */{ 10,   16,   26,   51,   34,   64,   75,    90,    102,   116,   128,   133},
/* 88% */{ 9,    14,   24,   46,   32,   58,   68,    81,    93,    108,   121,   128},
/* 89% */{ 8,    13,   22,   42,   31,   52,   61,    74,    86,    102,   116,   124},
/* 90% */{ 7,    12,   21,   37,   29,   46,   54,    66,    79,    96,    112,   121}
};

#define RSSIBUF_NUM 10
#define RSSI2RATE_SIZE 9

static TXRETRY_REC TxRateRec={MTO_MAX_DATA_RATE_LEVELS - 1, 0};   //new record=>TxRateRec
static int TxRetryRate;
//static int SQ3, BSS_PK_CNT, NIDLESLOT, SLOT_CNT, INTERF_CNT, GAP_CNT, DS_EVM;
static s32 RSSIBuf[RSSIBUF_NUM]={-70, -70, -70, -70, -70, -70, -70, -70, -70, -70};
static s32 RSSISmoothed=-700;
static int RSSIBufIndex=0;
static u8 max_rssi_rate;
static int rate_tbl[13] = {0,1,2,5,11,6,9,12,18,24,36,48,54};
//[WKCHEN]static core_data_t *pMTOcore_data=NULL;

static int TotalTxPkt = 0;
static int TotalTxPktRetry = 0;
static int TxPktPerAnt[3] = {0,0,0};
static int RXRSSIANT[3] ={-70,-70,-70};
static int TxPktRetryPerAnt[3] = {0,0,0};
//static int TxDominateFlag=FALSE;
static u8 old_antenna[4]={1 ,0 ,1 ,0};
static int retryrate_rec[MTO_MAX_DATA_RATE_LEVELS];//this record the retry rate at different data rate

static int PeriodTotalTxPkt = 0;
static int PeriodTotalTxPktRetry = 0;

typedef struct
{
	s32 RSSI;
	u8  TxRate;
}RSSI2RATE;

static RSSI2RATE RSSI2RateTbl[RSSI2RATE_SIZE] =
{
	{-740, 108},  // 54M
	{-760, 96},  // 48M
	{-820, 72},  // 36M
	{-850, 48},  // 24M
	{-870, 36},  // 18M
	{-890, 24},  // 12M
	{-900, 12},  // 6M
	{-920, 11}, // 5.5M
	{-950, 4}, // 2M
};
static u8 untogglecount;
static u8 last_rate_ant; //this is used for antenna backoff-hh

u8	boSparseTxTraffic = FALSE;

void MTO_Init(MTO_FUNC_INPUT);
void AntennaToggleInitiator(MTO_FUNC_INPUT);
void AntennaToggleState(MTO_FUNC_INPUT);
void TxPwrControl(MTO_FUNC_INPUT);
void GetFreshAntennaData(MTO_FUNC_INPUT);
void TxRateReductionCtrl(MTO_FUNC_INPUT);
/** 1.1.31.1000 Turbo modify */
//void MTO_SetDTORateRange(int type);
void MTO_SetDTORateRange(MTO_FUNC_INPUT, u8 *pRateArray, u8 ArraySize);
void MTO_SetTxCount(MTO_FUNC_INPUT, u8 t0, u8 index);
void MTO_TxFailed(MTO_FUNC_INPUT);
void SmoothRSSI(s32 new_rssi);
void hal_get_dto_para(MTO_FUNC_INPUT, char *buffer);
u8 CalcNewRate(MTO_FUNC_INPUT, u8 old_rate, u32 retry_cnt, u32 tx_frag_cnt);
u8 GetMaxRateLevelFromRSSI(void);
u8 MTO_GetTxFallbackRate(MTO_FUNC_INPUT);
int Divide(int a, int b);
void multiagc(MTO_FUNC_INPUT, u8 high_gain_mode);

//===========================================================================
//  MTO_Init --
//
//  Description:
//    Set DTO Tx Rate Scope because different AP could have different Rate set.
//    After our staion join with AP, LM core will call this function to initialize
//    Tx Rate table.
//
//  Arguments:
//    pRateArray      - The pointer to the Tx Rate Array by the following order
//                    - 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108
//                    - DTO won't check whether rate order is invalid or not
//    ArraySize       - The array size to indicate how many tx rate we can choose
//
//  sample code:
//	{
//		u8 RateArray[4] = {2, 4, 11, 22};
//		MTO_SetDTORateRange(RateArray, 4);
//	}
//
//  Return Value:
//    None
//============================================================================
void MTO_SetDTORateRange(MTO_FUNC_INPUT,u8 *pRateArray, u8 ArraySize)
{
	u8	i, j=0;

	for(i=0;i<ArraySize;i++)
	{
		if(pRateArray[i] == 22)
			break;
	}
	if(i < ArraySize) //we need adjust the order of rate list because 11Mbps rate exists
	{
		for(;i>0;i--)
		{
			if(pRateArray[i-1] <= 11)
				break;
			pRateArray[i] = pRateArray[i-1];
		}
		pRateArray[i] = 22;
		MTO_OFDM_RATE_LEVEL() = i;
	}
	else
	{
		for(i=0; i<ArraySize; i++)
		{
			if (pRateArray[i] >= 12)
				break;
		}
		MTO_OFDM_RATE_LEVEL() = i;
	}

	for(i=0;i<ArraySize;i++)
	{
		MTO_Data_Rate_Tbl[i] = pRateArray[i];
		for(;j<MTO_MAX_DATA_RATE_LEVELS;j++)
		{
			if(Stardard_Data_Rate_Tbl[j] == pRateArray[i])
				break;
		}
		Level2PerTbl[i] = j;
		#ifdef _PE_DTO_DUMP_
		WBDEBUG(("[MTO]:Op Rate[%d]: %d\n",i, MTO_Data_Rate_Tbl[i]));
		#endif
	}
	MTO_DataRateAvailableLevel = ArraySize;
	if( MTO_DATA().RatePolicy ) // 0 means that no registry setting
	{
		if( MTO_DATA().RatePolicy == 1 )
			TxRateRec.tx_rate = 0;	//ascent
		else
			TxRateRec.tx_rate = MTO_DataRateAvailableLevel -1 ;	//descent
	}
	else
	{
		if( MTO_INITTXRATE_MODE )
			TxRateRec.tx_rate = 0;	//ascent
		else
			TxRateRec.tx_rate = MTO_DataRateAvailableLevel -1 ;	//descent
	}
	TxRateRec.tx_retry_rate = 0;
	//set default rate for initial use
	MTO_RATE_LEVEL() = TxRateRec.tx_rate;
	MTO_FALLBACK_RATE_LEVEL() = MTO_RATE_LEVEL();
}

//===========================================================================
//  MTO_Init --
//
//  Description:
//    Initialize MTO parameters.
//
//    This function should be invoked during system initialization.
//
//  Arguments:
//    Adapter      - The pointer to the Miniport Adapter Context
//
//  Return Value:
//    None
//============================================================================
void MTO_Init(MTO_FUNC_INPUT)
{
    int i;
	//WBDEBUG(("[MTO] -> MTO_Init()\n"));
	//[WKCHEN]pMTOcore_data = pcore_data;
// 20040510 Turbo add for global variable
    MTO_TMR_CNT()       = 0;
    MTO_TOGGLE_STATE()  = TOGGLE_STATE_IDLE;
    MTO_TX_RATE_REDUCTION_STATE() = RATE_CHGSTATE_IDLE;
    MTO_BACKOFF_TMR()   = 0;
    MTO_LAST_RATE()     = 11;
    MTO_CO_EFFICENT()   = 0;

    //MTO_TH_FIXANT()     = MTO_DEFAULT_TH_FIXANT;
    MTO_TH_CNT()        = MTO_DEFAULT_TH_CNT;
    MTO_TH_SQ3()        = MTO_DEFAULT_TH_SQ3;
    MTO_TH_IDLE_SLOT()  = MTO_DEFAULT_TH_IDLE_SLOT;
    MTO_TH_PR_INTERF()  = MTO_DEFAULT_TH_PR_INTERF;

    MTO_TMR_AGING()     = MTO_DEFAULT_TMR_AGING;
    MTO_TMR_PERIODIC()  = MTO_DEFAULT_TMR_PERIODIC;

    //[WKCHEN]MTO_CCA_MODE_SETUP()= (u8) hal_get_cca_mode(MTO_HAL());
    //[WKCHEN]MTO_CCA_MODE()      = MTO_CCA_MODE_SETUP();

    //MTO_PREAMBLE_TYPE() = MTO_PREAMBLE_LONG;
    MTO_PREAMBLE_TYPE() = MTO_PREAMBLE_SHORT;   // for test

    MTO_ANT_SEL()       = hal_get_antenna_number(MTO_HAL());
    MTO_ANT_MAC()       = MTO_ANT_SEL();
    MTO_CNT_ANT(0)      = 0;
    MTO_CNT_ANT(1)      = 0;
    MTO_SQ_ANT(0)       = 0;
    MTO_SQ_ANT(1)       = 0;
    MTO_ANT_DIVERSITY() = MTO_ANTENNA_DIVERSITY_ON;
    //CardSet_AntennaDiversity(Adapter, MTO_ANT_DIVERSITY());
    //PLMESetAntennaDiversity( Adapter, MTO_ANT_DIVERSITY());

    MTO_AGING_TIMEOUT() = 0;//MTO_TMR_AGING() / MTO_TMR_PERIODIC();

    // The following parameters should be initialized to the values set by user
    //
    //MTO_RATE_LEVEL()            = 10;
    MTO_RATE_LEVEL()            = 0;
	MTO_FALLBACK_RATE_LEVEL()	= MTO_RATE_LEVEL();
    MTO_FRAG_TH_LEVEL()         = 4;
    /** 1.1.23.1000 Turbo modify from -1 to +1
	MTO_RTS_THRESHOLD()         = MTO_FRAG_TH() - 1;
    MTO_RTS_THRESHOLD_SETUP()   = MTO_FRAG_TH() - 1;
	*/
	MTO_RTS_THRESHOLD()         = MTO_FRAG_TH() + 1;
    MTO_RTS_THRESHOLD_SETUP()   = MTO_FRAG_TH() + 1;
    // 1.1.23.1000 Turbo add for mto change preamble from 0 to 1
	MTO_RATE_CHANGE_ENABLE()    = 1;
    MTO_FRAG_CHANGE_ENABLE()    = 0;          // 1.1.29.1000 Turbo add don't support frag
	//The default valud of ANTDIV_DEFAULT_ON will be decided by EEPROM
	//#ifdef ANTDIV_DEFAULT_ON
    //MTO_ANT_DIVERSITY_ENABLE()  = 1;
	//#else
    //MTO_ANT_DIVERSITY_ENABLE()  = 0;
	//#endif
    MTO_POWER_CHANGE_ENABLE()   = 1;
	MTO_PREAMBLE_CHANGE_ENABLE()= 1;
    MTO_RTS_CHANGE_ENABLE()     = 0;          // 1.1.29.1000 Turbo add don't support frag
    // 20040512 Turbo add
	//old_antenna[0] = 1;
	//old_antenna[1] = 0;
	//old_antenna[2] = 1;
	//old_antenna[3] = 0;
	for (i=0;i<MTO_MAX_DATA_RATE_LEVELS;i++)
		retryrate_rec[i]=5;

	MTO_TXFLOWCOUNT() = 0;
	//--------- DTO threshold parameters -------------
	//MTOPARA_PERIODIC_CHECK_CYCLE() = 50;
	MTOPARA_PERIODIC_CHECK_CYCLE() = 10;
	MTOPARA_RSSI_TH_FOR_ANTDIV() = 10;
	MTOPARA_TXCOUNT_TH_FOR_CALC_RATE() = 50;
	MTOPARA_TXRATE_INC_TH()	= 10;
	MTOPARA_TXRATE_DEC_TH() = 30;
	MTOPARA_TXRATE_EQ_TH() = 40;
	MTOPARA_TXRATE_BACKOFF() = 12;
	MTOPARA_TXRETRYRATE_REDUCE() = 6;
	if ( MTO_TXPOWER_FROM_EEPROM == 0xff)
	{
		switch( MTO_HAL()->phy_type)
		{
			case RF_AIROHA_2230:
			case RF_AIROHA_2230S: // 20060420 Add this
				MTOPARA_TXPOWER_INDEX() = 46; // MAX-8 // @@ Only for AL 2230
				break;
			case RF_AIROHA_7230:
				MTOPARA_TXPOWER_INDEX() = 49;
				break;
			case RF_WB_242:
				MTOPARA_TXPOWER_INDEX() = 10;
				break;
			case RF_WB_242_1:
				MTOPARA_TXPOWER_INDEX() = 24; // ->10 20060316.1 modify
				break;
		}
	}
	else	//follow the setting from EEPROM
		MTOPARA_TXPOWER_INDEX() = MTO_TXPOWER_FROM_EEPROM;
	hal_set_rf_power(MTO_HAL(), (u8)MTOPARA_TXPOWER_INDEX());
	//------------------------------------------------

	// For RSSI turning 20060808.4 Cancel load from EEPROM
	MTO_DATA().RSSI_high = -41;
	MTO_DATA().RSSI_low = -60;
}

//---------------------------------------------------------------------------//
static u32 DTO_Rx_Info[13][3];
static u32 DTO_RxCRCFail_Info[13][3];
static u32 AntennaToggleBkoffTimer=5;
typedef struct{
	int RxRate;
	int RxRatePkts;
	int index;
}RXRATE_ANT;
RXRATE_ANT RxRatePeakAnt[3];

#define ANT0    0
#define ANT1    1
#define OLD_ANT 2

void SearchPeakRxRate(int index)
{
	int i;
	RxRatePeakAnt[index].RxRatePkts=0;
	//Find out the best rx rate which is used on different antenna
	for(i=1;i<13;i++)
	{
		if(DTO_Rx_Info[i][index] > (u32) RxRatePeakAnt[index].RxRatePkts)
		{
			RxRatePeakAnt[index].RxRatePkts = DTO_Rx_Info[i][index];
			RxRatePeakAnt[index].RxRate = rate_tbl[i];
			RxRatePeakAnt[index].index = i;
		}
	}
}

void ResetDTO_RxInfo(int index, MTO_FUNC_INPUT)
{
	int i;

	#ifdef _PE_DTO_DUMP_
	WBDEBUG(("ResetDTOrx\n"));
	#endif

	for(i=0;i<13;i++)
		DTO_Rx_Info[i][index] = MTO_HAL()->rx_ok_count[i];

	for(i=0;i<13;i++)
		DTO_RxCRCFail_Info[i][index] = MTO_HAL()->rx_err_count[i];

	TotalTxPkt = 0;
	TotalTxPktRetry = 0;
}

void GetDTO_RxInfo(int index, MTO_FUNC_INPUT)
{
	int i;

	#ifdef _PE_DTO_DUMP_
	WBDEBUG(("GetDTOrx\n"));
	#endif

	//PDEBUG(("[MTO]:DTO_Rx_Info[%d]=%d, rx_ok_count=%d\n", index, DTO_Rx_Info[0][index], phw_data->rx_ok_count[0]));
	for(i=0;i<13;i++)
		DTO_Rx_Info[i][index] = abs(MTO_HAL()->rx_ok_count[i] - DTO_Rx_Info[i][index]);
	if(DTO_Rx_Info[0][index]==0) DTO_Rx_Info[0][index] = 1;

	for(i=0;i<13;i++)
		DTO_RxCRCFail_Info[i][index] = MTO_HAL()->rx_err_count[i] - DTO_RxCRCFail_Info[i][index];

	TxPktPerAnt[index] = TotalTxPkt;
	TxPktRetryPerAnt[index] = TotalTxPktRetry;
	TotalTxPkt = 0;
	TotalTxPktRetry = 0;
}

void OutputDebugInfo(int index1, int index2)
{
	#ifdef _PE_DTO_DUMP_
	WBDEBUG(("[HHDTO]:Total Rx (%d)\t\t(%d) \n ", DTO_Rx_Info[0][index1], DTO_Rx_Info[0][index2]));
    WBDEBUG(("[HHDTO]:RECEIVE RSSI: (%d)\t\t(%d) \n ", RXRSSIANT[index1], RXRSSIANT[index2]));
	WBDEBUG(("[HHDTO]:TX packet correct rate: (%d)%%\t\t(%d)%% \n ",Divide(TxPktPerAnt[index1]*100,TxPktRetryPerAnt[index1]), Divide(TxPktPerAnt[index2]*100,TxPktRetryPerAnt[index2])));
	#endif
	{
		int tmp1, tmp2;
		#ifdef _PE_DTO_DUMP_
		WBDEBUG(("[HHDTO]:Total Tx (%d)\t\t(%d) \n ", TxPktPerAnt[index1], TxPktPerAnt[index2]));
		WBDEBUG(("[HHDTO]:Total Tx retry (%d)\t\t(%d) \n ", TxPktRetryPerAnt[index1], TxPktRetryPerAnt[index2]));
		#endif
		tmp1 = TxPktPerAnt[index1] + DTO_Rx_Info[0][index1];
		tmp2 = TxPktPerAnt[index2] + DTO_Rx_Info[0][index2];
		#ifdef _PE_DTO_DUMP_
		WBDEBUG(("[HHDTO]:Total Tx+RX (%d)\t\t(%d) \n ", tmp1, tmp2));
		#endif
	}
}

unsigned char TxDominate(int index)
{
	int tmp;

	tmp = TxPktPerAnt[index] + DTO_Rx_Info[0][index];

	if(Divide(TxPktPerAnt[index]*100, tmp) > 40)
		return TRUE;
	else
		return FALSE;
}

unsigned char CmpTxRetryRate(int index1, int index2)
{
	int tx_retry_rate1, tx_retry_rate2;
	tx_retry_rate1 = Divide((TxPktRetryPerAnt[index1] - TxPktPerAnt[index1])*100, TxPktRetryPerAnt[index1]);
	tx_retry_rate2 = Divide((TxPktRetryPerAnt[index2] - TxPktPerAnt[index2])*100, TxPktRetryPerAnt[index2]);
	#ifdef _PE_DTO_DUMP_
	WBDEBUG(("[MTO]:TxRetry Ant0: (%d%%)  Ant1: (%d%%) \n ", tx_retry_rate1, tx_retry_rate2));
	#endif

	if(tx_retry_rate1 > tx_retry_rate2)
		return TRUE;
	else
		return FALSE;
}

void GetFreshAntennaData(MTO_FUNC_INPUT)
{
    u8      x;

	x = hal_get_antenna_number(MTO_HAL());
	//hal_get_bss_pk_cnt(MTO_HAL());
	//hal_get_est_sq3(MTO_HAL(), 1);
	old_antenna[0] = x;
	//if this is the function for timer
	ResetDTO_RxInfo(x, MTO_FUNC_INPUT_DATA);
	if(AntennaToggleBkoffTimer)
			AntennaToggleBkoffTimer--;
	if (abs(last_rate_ant-MTO_RATE_LEVEL())>1)  //backoff timer reset
		AntennaToggleBkoffTimer=0;

	if (MTO_ANT_DIVERSITY() != MTO_ANTENNA_DIVERSITY_ON ||
		MTO_ANT_DIVERSITY_ENABLE() != 1)
	AntennaToggleBkoffTimer=1;
	#ifdef _PE_DTO_DUMP_
	WBDEBUG(("[HHDTO]:**last data rate=%d,now data rate=%d**antenna toggle timer=%d",last_rate_ant,MTO_RATE_LEVEL(),AntennaToggleBkoffTimer));
	#endif
	last_rate_ant=MTO_RATE_LEVEL();
	if(AntennaToggleBkoffTimer==0)
	{
		MTO_TOGGLE_STATE() = TOGGLE_STATE_WAIT0;
		#ifdef _PE_DTO_DUMP_
		WBDEBUG(("[HHDTO]:===state is starting==for antenna toggle==="));
		#endif
	}
	else
		MTO_TOGGLE_STATE() = TOGGLE_STATE_IDLE;

	if ((MTO_BACKOFF_TMR()!=0)&&(MTO_RATE_LEVEL()>MTO_DataRateAvailableLevel - 3))
	{
		MTO_TOGGLE_STATE() = TOGGLE_STATE_IDLE;
		#ifdef _PE_DTO_DUMP_
		WBDEBUG(("[HHDTO]:===the data rate is %d (good)and will not toogle  ===",MTO_DATA_RATE()>>1));
		#endif
	}


}

int WB_PCR[2]; //packet correct rate

void AntennaToggleState(MTO_FUNC_INPUT)
{
    int decideantflag = 0;
	u8      x;
	s32     rssi;

	if(MTO_ANT_DIVERSITY_ENABLE() != 1)
		return;
	x = hal_get_antenna_number(MTO_HAL());
	switch(MTO_TOGGLE_STATE())
	{

	   //Missing.....
	   case TOGGLE_STATE_IDLE:
	 case TOGGLE_STATE_BKOFF:
	             break;;

		case TOGGLE_STATE_WAIT0://========
	               GetDTO_RxInfo(x, MTO_FUNC_INPUT_DATA);
			sme_get_rssi(MTO_FUNC_INPUT_DATA, &rssi);
			RXRSSIANT[x] = rssi;
			#ifdef _PE_DTO_DUMP_
			WBDEBUG(("[HHDTO] **wait0==== Collecting Ant%d--rssi=%d\n", x,RXRSSIANT[x]));
			#endif

			//change antenna and reset the data at changed antenna
			x = (~x) & 0x01;
			MTO_ANT_SEL() = x;
			hal_set_antenna_number(MTO_HAL(), MTO_ANT_SEL());
			LOCAL_ANTENNA_NO() = x;

			MTO_TOGGLE_STATE() = TOGGLE_STATE_WAIT1;//go to wait1
			ResetDTO_RxInfo(x, MTO_FUNC_INPUT_DATA);
			break;
		case TOGGLE_STATE_WAIT1://=====wait1
			//MTO_CNT_ANT(x) = hal_get_bss_pk_cnt(MTO_HAL());
			//RXRSSIANT[x] = hal_get_rssi(MTO_HAL());
			sme_get_rssi(MTO_FUNC_INPUT_DATA, &rssi);
			RXRSSIANT[x] = rssi;
			GetDTO_RxInfo(x, MTO_FUNC_INPUT_DATA);
			#ifdef _PE_DTO_DUMP_
			WBDEBUG(("[HHDTO] **wait1==== Collecting Ant%d--rssi=%d\n", x,RXRSSIANT[x]));
			#endif
			MTO_TOGGLE_STATE() = TOGGLE_STATE_MAKEDESISION;
			break;
		case TOGGLE_STATE_MAKEDESISION:
			#ifdef _PE_DTO_DUMP_
			WBDEBUG(("[HHDTO]:Ant--0-----------------1---\n"));
			OutputDebugInfo(ANT0,ANT1);
			#endif
			//PDEBUG(("[HHDTO] **decision====\n "));

			//=====following is the decision produrce
			//
			//    first: compare the rssi if difference >10
			//           select the larger one
			//           ,others go to second
			//    second: comapre the tx+rx packet count if difference >100
			//            use larger total packets antenna
			//    third::compare the tx PER if packets>20
			//                           if difference >5% using the bigger one
			//
			//    fourth:compare the RX PER if packets>20
			//                    if PER difference <5%
			//                   using old antenna
			//
			//
			if (abs(RXRSSIANT[ANT0]-RXRSSIANT[ANT1]) > MTOPARA_RSSI_TH_FOR_ANTDIV())//====rssi_th
			{
				if (RXRSSIANT[ANT0]>RXRSSIANT[ANT1])
				{
					decideantflag=1;
					MTO_ANT_MAC() = ANT0;
				}
				else
				{
					decideantflag=1;
					MTO_ANT_MAC() = ANT1;
				}
				#ifdef _PE_DTO_DUMP_
				WBDEBUG(("Select antenna by RSSI\n"));
				#endif
			}
			else if  (abs(TxPktPerAnt[ANT0] + DTO_Rx_Info[0][ANT0]-TxPktPerAnt[ANT1]-DTO_Rx_Info[0][ANT1])<50)//=====total packet_th
			{
				#ifdef _PE_DTO_DUMP_
				WBDEBUG(("Total tx/rx is close\n"));
				#endif
				if (TxDominate(ANT0) && TxDominate(ANT1))
				{
					if ((TxPktPerAnt[ANT0]>10) && (TxPktPerAnt[ANT1]>10))//====tx packet_th
					{
						WB_PCR[ANT0]=Divide(TxPktPerAnt[ANT0]*100,TxPktRetryPerAnt[ANT0]);
						WB_PCR[ANT1]=Divide(TxPktPerAnt[ANT1]*100,TxPktRetryPerAnt[ANT1]);
						if (abs(WB_PCR[ANT0]-WB_PCR[ANT1])>5)// tx PER_th
						{
							#ifdef _PE_DTO_DUMP_
							WBDEBUG(("Decide by Tx correct rate\n"));
							#endif
							if (WB_PCR[ANT0]>WB_PCR[ANT1])
							{
								decideantflag=1;
								MTO_ANT_MAC() = ANT0;
							}
							else
							{
								decideantflag=1;
								MTO_ANT_MAC() = ANT1;
							}
						}
						else
						{
							decideantflag=0;
							untogglecount++;
							MTO_ANT_MAC() = old_antenna[0];
						}
					}
					else
					{
						decideantflag=0;
						MTO_ANT_MAC() = old_antenna[0];
					}
				}
				else if ((DTO_Rx_Info[0][ANT0]>10)&&(DTO_Rx_Info[0][ANT1]>10))//rx packet th
				{
					#ifdef _PE_DTO_DUMP_
					WBDEBUG(("Decide by Rx\n"));
					#endif
					if (abs(DTO_Rx_Info[0][ANT0] - DTO_Rx_Info[0][ANT1])>50)
					{
						if (DTO_Rx_Info[0][ANT0] > DTO_Rx_Info[0][ANT1])
						{
							decideantflag=1;
							MTO_ANT_MAC() = ANT0;
						}
						else
						{
							decideantflag=1;
							MTO_ANT_MAC() = ANT1;
						}
					}
					else
					{
						decideantflag=0;
						untogglecount++;
						MTO_ANT_MAC() = old_antenna[0];
					}
				}
				else
				{
					decideantflag=0;
					MTO_ANT_MAC() = old_antenna[0];
				}
			}
			else if ((TxPktPerAnt[ANT0]+DTO_Rx_Info[0][ANT0])>(TxPktPerAnt[ANT1]+DTO_Rx_Info[0][ANT1]))//use more packekts
			{
				#ifdef _PE_DTO_DUMP_
				WBDEBUG(("decide by total tx/rx : ANT 0\n"));
				#endif

				decideantflag=1;
				MTO_ANT_MAC() = ANT0;
			}
			else
			{
				#ifdef _PE_DTO_DUMP_
				WBDEBUG(("decide by total tx/rx : ANT 1\n"));
				#endif
				decideantflag=1;
				MTO_ANT_MAC() = ANT1;

			}
			//this is force ant toggle
			if (decideantflag==1)
				untogglecount=0;

			untogglecount=untogglecount%4;
			if (untogglecount==3) //change antenna
				MTO_ANT_MAC() = ((~old_antenna[0]) & 0x1);
			#ifdef _PE_DTO_DUMP_
			WBDEBUG(("[HHDTO]:==================untoggle-count=%d",untogglecount));
			#endif




			//PDEBUG(("[HHDTO] **********************************DTO ENABLE=%d",MTO_ANT_DIVERSITY_ENABLE()));
			if(MTO_ANT_DIVERSITY_ENABLE() == 1)
			{
					MTO_ANT_SEL() = MTO_ANT_MAC();
					hal_set_antenna_number(MTO_HAL(), MTO_ANT_SEL());
					LOCAL_ANTENNA_NO() = MTO_ANT_SEL();
					#ifdef _PE_DTO_DUMP_
					WBDEBUG(("[HHDTO] ==decision==*******antflag=%d******************selected antenna=%d\n",decideantflag,MTO_ANT_SEL()));
					#endif
			}
			if (decideantflag)
			{
				old_antenna[3]=old_antenna[2];//store antenna info
				old_antenna[2]=old_antenna[1];
				old_antenna[1]=old_antenna[0];
				old_antenna[0]= MTO_ANT_MAC();
			}
			#ifdef _PE_DTO_DUMP_
			WBDEBUG(("[HHDTO]:**old antenna=[%d][%d][%d][%d]\n",old_antenna[0],old_antenna[1],old_antenna[2],old_antenna[3]));
			#endif
			if (old_antenna[0]!=old_antenna[1])
				AntennaToggleBkoffTimer=0;
			else if (old_antenna[1]!=old_antenna[2])
				AntennaToggleBkoffTimer=1;
			else if (old_antenna[2]!=old_antenna[3])
				AntennaToggleBkoffTimer=2;
			else
				AntennaToggleBkoffTimer=4;

			#ifdef _PE_DTO_DUMP_
			WBDEBUG(("[HHDTO]:**back off timer=%d",AntennaToggleBkoffTimer));
			#endif

			ResetDTO_RxInfo(MTO_ANT_MAC(), MTO_FUNC_INPUT_DATA);
			if (AntennaToggleBkoffTimer==0 && decideantflag)
				MTO_TOGGLE_STATE() = TOGGLE_STATE_WAIT0;
			else
				MTO_TOGGLE_STATE() = TOGGLE_STATE_IDLE;
			break;
	}

}

void multiagc(MTO_FUNC_INPUT, u8 high_gain_mode )
{
	s32		rssi;
	hw_data_t	*pHwData = MTO_HAL();

	sme_get_rssi(MTO_FUNC_INPUT_DATA, &rssi);

	if( (RF_WB_242 == pHwData->phy_type) ||
		(RF_WB_242_1 == pHwData->phy_type) ) // 20060619.5 Add
	{
		if (high_gain_mode==1)
		{
			//hw_set_dxx_reg(phw_data, 0x0C, 0xf8f52230);
			//hw_set_dxx_reg(phw_data, 0x20, 0x06C43440);
			Wb35Reg_Write( pHwData, 0x100C, 0xF2F32232 ); // 940916 0xf8f52230 );
			Wb35Reg_Write( pHwData, 0x1020, 0x04cb3440 ); // 940915 0x06C43440
		}
		else if (high_gain_mode==0)
		{
			//hw_set_dxx_reg(phw_data, 0x0C, 0xEEEE000D);
			//hw_set_dxx_reg(phw_data, 0x20, 0x06c41440);
			Wb35Reg_Write( pHwData, 0x100C, 0xEEEE000D );
			Wb35Reg_Write( pHwData, 0x1020, 0x04cb1440 ); // 940915 0x06c41440
		}
		#ifdef _PE_DTO_DUMP_
		WBDEBUG(("[HHDTOAGC] **rssi=%d, high gain mode=%d", rssi, high_gain_mode));
		#endif
	}
}

void TxPwrControl(MTO_FUNC_INPUT)
{
    s32     rssi;
	hw_data_t	*pHwData = MTO_HAL();

	sme_get_rssi(MTO_FUNC_INPUT_DATA, &rssi);
	if( (RF_WB_242 == pHwData->phy_type) ||
		(RF_WB_242_1 == pHwData->phy_type) ) // 20060619.5 Add
	{
		static u8 high_gain_mode; //this is for winbond RF switch LNA
		                          //using different register setting

		if (high_gain_mode==1)
		{
			if( rssi > MTO_DATA().RSSI_high )
			{
				//hw_set_dxx_reg(phw_data, 0x0C, 0xf8f52230);
				//hw_set_dxx_reg(phw_data, 0x20, 0x05541640);
				high_gain_mode=0;
			}
			else
			{
				//hw_set_dxx_reg(phw_data, 0x0C, 0xf8f51830);
				//hw_set_dxx_reg(phw_data, 0x20, 0x05543E40);
				high_gain_mode=1;
			}
		}
		else //if (high_gain_mode==0)
		{
			if( rssi < MTO_DATA().RSSI_low )
			{
				//hw_set_dxx_reg(phw_data, 0x0C, 0xf8f51830);
				//hw_set_dxx_reg(phw_data, 0x20, 0x05543E40);
				high_gain_mode=1;
			}
			else
			{
				//hw_set_dxx_reg(phw_data, 0x0C, 0xf8f52230);
				//hw_set_dxx_reg(phw_data, 0x20, 0x05541640);
				high_gain_mode=0;
			}
		}

		// Always high gain 20051014. Using the initial value only.
		multiagc(MTO_FUNC_INPUT_DATA, high_gain_mode);
	}
}


u8 CalcNewRate(MTO_FUNC_INPUT, u8 old_rate, u32 retry_cnt, u32 tx_frag_cnt)
{
	int i;
	u8 new_rate;
    u32 retry_rate;
	int TxThrouput1, TxThrouput2, TxThrouput3, BestThroupht;

	if(tx_frag_cnt < MTOPARA_TXCOUNT_TH_FOR_CALC_RATE()) //too few packets transmit
	{
		return 0xff;
	}
	retry_rate = Divide(retry_cnt * 100, tx_frag_cnt);

	if(retry_rate > 90) retry_rate = 90; //always truncate to 90% due to lookup table size
	#ifdef _PE_DTO_DUMP_
	WBDEBUG(("##### Current level =%d, Retry count =%d, Frag count =%d\n",
						old_rate, retry_cnt, tx_frag_cnt));
	WBDEBUG(("*##* Retry rate =%d, throughput =%d\n",
						retry_rate, Rate_PER_TBL[retry_rate][old_rate]));
	WBDEBUG(("TxRateRec.tx_rate =%d, Retry rate = %d, throughput = %d\n",
						TxRateRec.tx_rate, TxRateRec.tx_retry_rate,
						Rate_PER_TBL[TxRateRec.tx_retry_rate][Level2PerTbl[TxRateRec.tx_rate]]));
	WBDEBUG(("old_rate-1 =%d, Retry rate = %d, throughput = %d\n",
						old_rate-1, retryrate_rec[old_rate-1],
						Rate_PER_TBL[retryrate_rec[old_rate-1]][old_rate-1]));
	WBDEBUG(("old_rate+1 =%d, Retry rate = %d, throughput = %d\n",
						old_rate+1, retryrate_rec[old_rate+1],
						Rate_PER_TBL[retryrate_rec[old_rate+1]][old_rate+1]));
	#endif

	//following is for record the retry rate at the different data rate
	if (abs(retry_rate-retryrate_rec[old_rate])<50)//---the per TH
		retryrate_rec[old_rate] = retry_rate; //update retry rate
	else
	{
		for (i=0;i<MTO_DataRateAvailableLevel;i++) //reset all retry rate
			retryrate_rec[i]=0;
		retryrate_rec[old_rate] = retry_rate;
		#ifdef _PE_DTO_DUMP_
		WBDEBUG(("Reset retry rate table\n"));
		#endif
	}

	if(TxRateRec.tx_rate > old_rate)   //Decrease Tx Rate
	{
		TxThrouput1 = Rate_PER_TBL[TxRateRec.tx_retry_rate][Level2PerTbl[TxRateRec.tx_rate]];
		TxThrouput2 = Rate_PER_TBL[retry_rate][Level2PerTbl[old_rate]];
		if(TxThrouput1 > TxThrouput2)
		{
			new_rate = TxRateRec.tx_rate;
			BestThroupht = TxThrouput1;
		}
		else
		{
			new_rate = old_rate;
			BestThroupht = TxThrouput2;
		}
		if((old_rate > 0) &&(retry_rate>MTOPARA_TXRATE_DEC_TH()))   //Min Rate
		{
			TxThrouput3 = Rate_PER_TBL[retryrate_rec[old_rate-1]][Level2PerTbl[old_rate-1]];
			if(BestThroupht < TxThrouput3)
			{
				new_rate = old_rate - 1;
				#ifdef _PE_DTO_DUMP_
				WBDEBUG(("--------\n"));
				#endif
				BestThroupht = TxThrouput3;
			}
		}
	}
	else if(TxRateRec.tx_rate < old_rate)  //Increase Tx Rate
	{
		TxThrouput1 = Rate_PER_TBL[TxRateRec.tx_retry_rate][Level2PerTbl[TxRateRec.tx_rate]];
		TxThrouput2 = Rate_PER_TBL[retry_rate][Level2PerTbl[old_rate]];
		if(TxThrouput1 > TxThrouput2)
		{
			new_rate = TxRateRec.tx_rate;
			BestThroupht = TxThrouput1;
		}
		else
		{
			new_rate = old_rate;
			BestThroupht = TxThrouput2;
		}
		if ((old_rate < MTO_DataRateAvailableLevel - 1)&&(retry_rate<MTOPARA_TXRATE_INC_TH()))
		{
			//TxThrouput3 = Rate_PER_TBL[retryrate_rec[old_rate+1]][Level2PerTbl[old_rate+1]];
			if (retryrate_rec[old_rate+1] > MTOPARA_TXRETRYRATE_REDUCE())
				TxThrouput3 = Rate_PER_TBL[retryrate_rec[old_rate+1]-MTOPARA_TXRETRYRATE_REDUCE()][Level2PerTbl[old_rate+1]];
			else
				TxThrouput3 = Rate_PER_TBL[retryrate_rec[old_rate+1]][Level2PerTbl[old_rate+1]];
			if(BestThroupht < TxThrouput3)
			{
				new_rate = old_rate + 1;
				#ifdef _PE_DTO_DUMP_
				WBDEBUG(("++++++++++\n"));
				#endif
				BestThroupht = TxThrouput3;
			}
		}
	}
	else  //Tx Rate no change
	{
		TxThrouput2 = Rate_PER_TBL[retry_rate][Level2PerTbl[old_rate]];
		new_rate = old_rate;
		BestThroupht = TxThrouput2;

		if (retry_rate <MTOPARA_TXRATE_EQ_TH())    //th for change higher rate
		{
			if(old_rate < MTO_DataRateAvailableLevel - 1)
			{
				//TxThrouput3 = Rate_PER_TBL[retryrate_rec[old_rate+1]][Level2PerTbl[old_rate+1]];
				if (retryrate_rec[old_rate+1] > MTOPARA_TXRETRYRATE_REDUCE())
					TxThrouput3 = Rate_PER_TBL[retryrate_rec[old_rate+1]-MTOPARA_TXRETRYRATE_REDUCE()][Level2PerTbl[old_rate+1]];
				else
					TxThrouput3 = Rate_PER_TBL[retryrate_rec[old_rate+1]][Level2PerTbl[old_rate+1]];
				if(BestThroupht < TxThrouput3)
				{
					new_rate = old_rate + 1;
					BestThroupht = TxThrouput3;
					#ifdef _PE_DTO_DUMP_
					WBDEBUG(("=++++++++++\n"));
					#endif
				}
			}
		}
		else
		if(old_rate > 0)   //Min Rate
		{
			TxThrouput3 = Rate_PER_TBL[retryrate_rec[old_rate-1]][Level2PerTbl[old_rate-1]];
			if(BestThroupht < TxThrouput3)
			{
				new_rate = old_rate - 1;
				#ifdef _PE_DTO_DUMP_
				WBDEBUG(("=--------\n"));
				#endif
				BestThroupht = TxThrouput3;
			}
		}
	}

	if (!LOCAL_IS_IBSS_MODE())
	{
	max_rssi_rate = GetMaxRateLevelFromRSSI();
	#ifdef _PE_DTO_DUMP_
	WBDEBUG(("[MTO]:RSSI2Rate=%d\n", MTO_Data_Rate_Tbl[max_rssi_rate]));
	#endif
	if(new_rate > max_rssi_rate)
		new_rate = max_rssi_rate;
	}

	//save new rate;
	TxRateRec.tx_rate = old_rate;
	TxRateRec.tx_retry_rate = (u8) retry_rate;
	TxRetryRate = retry_rate;
	return new_rate;
}

void SmoothRSSI(s32 new_rssi)
{
	RSSISmoothed = RSSISmoothed + new_rssi - RSSIBuf[RSSIBufIndex];
	RSSIBuf[RSSIBufIndex] = new_rssi;
	RSSIBufIndex = (RSSIBufIndex + 1) % 10;
}

u8 GetMaxRateLevelFromRSSI(void)
{
	u8 i;
	u8 TxRate;

	for(i=0;i<RSSI2RATE_SIZE;i++)
	{
		if(RSSISmoothed > RSSI2RateTbl[i].RSSI)
			break;
	}
	#ifdef _PE_DTO_DUMP_
	WBDEBUG(("[MTO]:RSSI=%d\n", Divide(RSSISmoothed, 10)));
	#endif
	if(i < RSSI2RATE_SIZE)
		TxRate = RSSI2RateTbl[i].TxRate;
	else
		TxRate = 2;  //divided by 2 = 1Mbps

	for(i=MTO_DataRateAvailableLevel-1;i>0;i--)
	{
		if(TxRate >=MTO_Data_Rate_Tbl[i])
			break;
	}
	return i;
}

//===========================================================================
//  Description:
//      If we enable DTO, we will ignore the tx count with different tx rate from
//      DTO rate. This is because when we adjust DTO tx rate, there could be some
//      packets in the tx queue with previous tx rate
void MTO_SetTxCount(MTO_FUNC_INPUT, u8 tx_rate, u8 index)
{
	MTO_TXFLOWCOUNT()++;
	if ((MTO_ENABLE==1) && (MTO_RATE_CHANGE_ENABLE()==1))
	{
		if(tx_rate == MTO_DATA_RATE())
		{
			if (index == 0)
			{
				if (boSparseTxTraffic)
					MTO_HAL()->dto_tx_frag_count += MTOPARA_PERIODIC_CHECK_CYCLE();
				else
					MTO_HAL()->dto_tx_frag_count += 1;
			}
			else
			{
				if (index<8)
				{
					MTO_HAL()->dto_tx_retry_count += index;
					MTO_HAL()->dto_tx_frag_count += (index+1);
				}
				else
				{
					MTO_HAL()->dto_tx_retry_count += 7;
					MTO_HAL()->dto_tx_frag_count += 7;
				}
			}
		}
		else if(MTO_DATA_RATE()>48 && tx_rate ==48)
		{//ALFRED
			if (index<3) //for reduciing data rate scheme ,
				         //do not calcu different data rate
						 //3 is the reducing data rate at retry
			{
				MTO_HAL()->dto_tx_retry_count += index;
				MTO_HAL()->dto_tx_frag_count += (index+1);
			}
			else
			{
				MTO_HAL()->dto_tx_retry_count += 3;
				MTO_HAL()->dto_tx_frag_count += 3;
			}

		}
	}
	else
	{
		MTO_HAL()->dto_tx_retry_count += index;
		MTO_HAL()->dto_tx_frag_count += (index+1);
	}
	TotalTxPkt ++;
	TotalTxPktRetry += (index+1);

	PeriodTotalTxPkt ++;
	PeriodTotalTxPktRetry += (index+1);
}

u8 MTO_GetTxFallbackRate(MTO_FUNC_INPUT)
{
	return MTO_DATA_FALLBACK_RATE();
}


//===========================================================================
//  MTO_TxFailed --
//
//  Description:
//    Failure of transmitting a packet indicates that certain MTO parmeters
//    may need to be adjusted. This function is called when NIC just failed
//    to transmit a packet or when MSDULifeTime expired.
//
//  Arguments:
//    Adapter      - The pointer to the Miniport Adapter Context
//
//  Return Value:
//    None
//============================================================================
void MTO_TxFailed(MTO_FUNC_INPUT)
{
	return;
}

int Divide(int a, int b)
{
	if (b==0) b=1;
	return a/b;
}


