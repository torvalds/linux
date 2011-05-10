/*++
Copyright-c Realtek Semiconductor Corp. All rights reserved.

Module Name:
	r8192U_dm.c

Abstract:
	HW dynamic mechanism.

Major Change History:
	When      	Who				What
	----------	--------------- -------------------------------
	2008-05-14	amy                     create version 0 porting from windows code.

--*/
#include "r8192E.h"
#include "r8192E_dm.h"
#include "r8192E_hw.h"
#include "r819xE_phy.h"
#include "r819xE_phyreg.h"
#include "r8190_rtl8256.h"

#define DRV_NAME "rtl819xE"

//
// Indicate different AP vendor for IOT issue.
//
static const u32 edca_setting_DL[HT_IOT_PEER_MAX] =
{ 0x5e4322, 	0x5e4322, 	0x5e4322, 	0x604322, 	0xa44f, 	0x5e4322,	0x5e4322};
static const u32 edca_setting_UL[HT_IOT_PEER_MAX] =
{ 0x5e4322, 	0xa44f,		0x5e4322,  	0x604322, 	0x5e4322, 	0x5e4322, 	0x5e4322};

#define RTK_UL_EDCA 0xa44f
#define RTK_DL_EDCA 0x5e4322


dig_t	dm_digtable;
// For Dynamic Rx Path Selection by Signal Strength
DRxPathSel	DM_RxPathSelTable;

void dm_gpio_change_rf_callback(struct work_struct *work);

// DM --> Rate Adaptive
static void dm_check_rate_adaptive(struct r8192_priv *priv);

// DM --> Bandwidth switch
static void dm_init_bandwidth_autoswitch(struct r8192_priv *priv);
static void dm_bandwidth_autoswitch(struct r8192_priv *priv);

// DM --> TX power control
static void dm_check_txpower_tracking(struct r8192_priv *priv);

// DM --> Dynamic Init Gain by RSSI
static void dm_dig_init(struct r8192_priv *priv);
static void dm_ctrl_initgain_byrssi(struct r8192_priv *priv);
static void dm_ctrl_initgain_byrssi_highpwr(struct r8192_priv *priv);
static void dm_ctrl_initgain_byrssi_by_driverrssi(struct r8192_priv *priv);
static void dm_ctrl_initgain_byrssi_by_fwfalse_alarm(struct r8192_priv *priv);
static void dm_initial_gain(struct r8192_priv *priv);
static void dm_pd_th(struct r8192_priv *priv);
static void dm_cs_ratio(struct r8192_priv *priv);

static void dm_init_ctstoself(struct r8192_priv *priv);
// DM --> EDCA turboe mode control
static void dm_check_edca_turbo(struct r8192_priv *priv);
static void dm_init_edca_turbo(struct r8192_priv *priv);

// DM --> HW RF control
static void dm_check_rfctrl_gpio(struct r8192_priv *priv);

// DM --> Check current RX RF path state
static void dm_check_rx_path_selection(struct r8192_priv *priv);
static void dm_init_rxpath_selection(struct r8192_priv *priv);
static void dm_rxpath_sel_byrssi(struct r8192_priv *priv);

// DM --> Fsync for broadcom ap
static void dm_init_fsync(struct r8192_priv *priv);
static void dm_deInit_fsync(struct r8192_priv *priv);

static void dm_check_txrateandretrycount(struct r8192_priv *priv);
static void dm_check_fsync(struct r8192_priv *priv);


/*---------------------Define of Tx Power Control For Near/Far Range --------*/   //Add by Jacken 2008/02/18
static void dm_init_dynamic_txpower(struct r8192_priv *priv);
static void dm_dynamic_txpower(struct r8192_priv *priv);

// DM --> For rate adaptive and DIG, we must send RSSI to firmware
static void dm_send_rssi_tofw(struct r8192_priv *priv);
static void dm_ctstoself(struct r8192_priv *priv);

static void dm_fsync_timer_callback(unsigned long data);

/*
 * Prepare SW resource for HW dynamic mechanism.
 * This function is only invoked at driver intialization once.
 */
void init_hal_dm(struct r8192_priv *priv)
{
	// Undecorated Smoothed Signal Strength, it can utilized to dynamic mechanism.
	priv->undecorated_smoothed_pwdb = -1;

	//Initial TX Power Control for near/far range , add by amy 2008/05/15, porting from windows code.
	dm_init_dynamic_txpower(priv);
	init_rate_adaptive(priv);
	//dm_initialize_txpower_tracking(dev);
	dm_dig_init(priv);
	dm_init_edca_turbo(priv);
	dm_init_bandwidth_autoswitch(priv);
	dm_init_fsync(priv);
	dm_init_rxpath_selection(priv);
	dm_init_ctstoself(priv);
	INIT_DELAYED_WORK(&priv->gpio_change_rf_wq,  dm_gpio_change_rf_callback);

}

void deinit_hal_dm(struct r8192_priv *priv)
{
	dm_deInit_fsync(priv);
}

void hal_dm_watchdog(struct r8192_priv *priv)
{

	/*Add by amy 2008/05/15 ,porting from windows code.*/
	dm_check_rate_adaptive(priv);
	dm_dynamic_txpower(priv);
	dm_check_txrateandretrycount(priv);

	dm_check_txpower_tracking(priv);

	dm_ctrl_initgain_byrssi(priv);
	dm_check_edca_turbo(priv);
	dm_bandwidth_autoswitch(priv);

	dm_check_rfctrl_gpio(priv);
	dm_check_rx_path_selection(priv);
	dm_check_fsync(priv);

	// Add by amy 2008-05-15 porting from windows code.
	dm_send_rssi_tofw(priv);
	dm_ctstoself(priv);
}


/*
  * Decide Rate Adaptive Set according to distance (signal strength)
  *	01/11/2008	MHC		Modify input arguments and RATR table level.
  *	01/16/2008	MHC		RF_Type is assigned in ReadAdapterInfo(). We must call
  *						the function after making sure RF_Type.
  */
void init_rate_adaptive(struct r8192_priv *priv)
{
	prate_adaptive pra = &priv->rate_adaptive;

	pra->ratr_state = DM_RATR_STA_MAX;
	pra->high2low_rssi_thresh_for_ra = RateAdaptiveTH_High;
	pra->low2high_rssi_thresh_for_ra20M = RateAdaptiveTH_Low_20M+5;
	pra->low2high_rssi_thresh_for_ra40M = RateAdaptiveTH_Low_40M+5;

	pra->high_rssi_thresh_for_ra = RateAdaptiveTH_High+5;
	pra->low_rssi_thresh_for_ra20M = RateAdaptiveTH_Low_20M;
	pra->low_rssi_thresh_for_ra40M = RateAdaptiveTH_Low_40M;

	if(priv->CustomerID == RT_CID_819x_Netcore)
		pra->ping_rssi_enable = 1;
	else
		pra->ping_rssi_enable = 0;
	pra->ping_rssi_thresh_for_ra = 15;


	if (priv->rf_type == RF_2T4R)
	{
		// 07/10/08 MH Modify for RA smooth scheme.
		/* 2008/01/11 MH Modify 2T RATR table for different RSSI. 080515 porting by amy from windows code.*/
		pra->upper_rssi_threshold_ratr		= 	0x8f0f0000;
		pra->middle_rssi_threshold_ratr		= 	0x8f0ff000;
		pra->low_rssi_threshold_ratr		= 	0x8f0ff001;
		pra->low_rssi_threshold_ratr_40M	= 	0x8f0ff005;
		pra->low_rssi_threshold_ratr_20M	= 	0x8f0ff001;
		pra->ping_rssi_ratr	= 	0x0000000d;//cosa add for test
	}
	else if (priv->rf_type == RF_1T2R)
	{
		pra->upper_rssi_threshold_ratr		= 	0x000f0000;
		pra->middle_rssi_threshold_ratr		= 	0x000ff000;
		pra->low_rssi_threshold_ratr		= 	0x000ff001;
		pra->low_rssi_threshold_ratr_40M	= 	0x000ff005;
		pra->low_rssi_threshold_ratr_20M	= 	0x000ff001;
		pra->ping_rssi_ratr	= 	0x0000000d;//cosa add for test
	}

}


static void dm_check_rate_adaptive(struct r8192_priv *priv)
{
	PRT_HIGH_THROUGHPUT	pHTInfo = priv->ieee80211->pHTInfo;
	prate_adaptive			pra = (prate_adaptive)&priv->rate_adaptive;
	u32						currentRATR, targetRATR = 0;
	u32						LowRSSIThreshForRA = 0, HighRSSIThreshForRA = 0;
	bool						bshort_gi_enabled = false;
	static u8					ping_rssi_state=0;


	if(!priv->up)
	{
		RT_TRACE(COMP_RATE, "<---- dm_check_rate_adaptive(): driver is going to unload\n");
		return;
	}

	if(pra->rate_adaptive_disabled)//this variable is set by ioctl.
		return;

	// TODO: Only 11n mode is implemented currently,
	if( !(priv->ieee80211->mode == WIRELESS_MODE_N_24G ||
		 priv->ieee80211->mode == WIRELESS_MODE_N_5G))
		 return;

	if( priv->ieee80211->state == IEEE80211_LINKED )
	{
	//	RT_TRACE(COMP_RATE, "dm_CheckRateAdaptive(): \t");

		//
		// Check whether Short GI is enabled
		//
		bshort_gi_enabled = (pHTInfo->bCurTxBW40MHz && pHTInfo->bCurShortGI40MHz) ||
			(!pHTInfo->bCurTxBW40MHz && pHTInfo->bCurShortGI20MHz);


		pra->upper_rssi_threshold_ratr =
				(pra->upper_rssi_threshold_ratr & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;

		pra->middle_rssi_threshold_ratr =
				(pra->middle_rssi_threshold_ratr & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;

		if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
		{
			pra->low_rssi_threshold_ratr =
				(pra->low_rssi_threshold_ratr_40M & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;
		}
		else
		{
			pra->low_rssi_threshold_ratr =
			(pra->low_rssi_threshold_ratr_20M & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;
		}
		//cosa add for test
		pra->ping_rssi_ratr =
				(pra->ping_rssi_ratr & (~BIT31)) | ((bshort_gi_enabled)? BIT31:0) ;

		/* 2007/10/08 MH We support RA smooth scheme now. When it is the first
		   time to link with AP. We will not change upper/lower threshold. If
		   STA stay in high or low level, we must change two different threshold
		   to prevent jumping frequently. */
		if (pra->ratr_state == DM_RATR_STA_HIGH)
		{
			HighRSSIThreshForRA 	= pra->high2low_rssi_thresh_for_ra;
			LowRSSIThreshForRA	= (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)?
					(pra->low_rssi_thresh_for_ra40M):(pra->low_rssi_thresh_for_ra20M);
		}
		else if (pra->ratr_state == DM_RATR_STA_LOW)
		{
			HighRSSIThreshForRA	= pra->high_rssi_thresh_for_ra;
			LowRSSIThreshForRA 	= (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)?
					(pra->low2high_rssi_thresh_for_ra40M):(pra->low2high_rssi_thresh_for_ra20M);
		}
		else
		{
			HighRSSIThreshForRA	= pra->high_rssi_thresh_for_ra;
			LowRSSIThreshForRA	= (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)?
					(pra->low_rssi_thresh_for_ra40M):(pra->low_rssi_thresh_for_ra20M);
		}

		if(priv->undecorated_smoothed_pwdb >= (long)HighRSSIThreshForRA)
		{
			pra->ratr_state = DM_RATR_STA_HIGH;
			targetRATR = pra->upper_rssi_threshold_ratr;
		}else if(priv->undecorated_smoothed_pwdb >= (long)LowRSSIThreshForRA)
		{
			pra->ratr_state = DM_RATR_STA_MIDDLE;
			targetRATR = pra->middle_rssi_threshold_ratr;
		}else
		{
			pra->ratr_state = DM_RATR_STA_LOW;
			targetRATR = pra->low_rssi_threshold_ratr;
		}

			//cosa add for test
		if(pra->ping_rssi_enable)
		{
			//pHalData->UndecoratedSmoothedPWDB = 19;
			if(priv->undecorated_smoothed_pwdb < (long)(pra->ping_rssi_thresh_for_ra+5))
			{
				if( (priv->undecorated_smoothed_pwdb < (long)pra->ping_rssi_thresh_for_ra) ||
					ping_rssi_state )
				{
					pra->ratr_state = DM_RATR_STA_LOW;
					targetRATR = pra->ping_rssi_ratr;
					ping_rssi_state = 1;
				}
			}
			else
			{
				ping_rssi_state = 0;
			}
		}

		// For RTL819X, if pairwisekey = wep/tkip, we support only MCS0~7.
		if(priv->ieee80211->GetHalfNmodeSupportByAPsHandler(priv->ieee80211))
			targetRATR &=  0xf00fffff;

		//
		// Check whether updating of RATR0 is required
		//
		currentRATR = read_nic_dword(priv, RATR0);
		if( targetRATR !=  currentRATR )
		{
			u32 ratr_value;
			ratr_value = targetRATR;
			RT_TRACE(COMP_RATE,"currentRATR = %x, targetRATR = %x\n", currentRATR, targetRATR);
			if(priv->rf_type == RF_1T2R)
			{
				ratr_value &= ~(RATE_ALL_OFDM_2SS);
			}
			write_nic_dword(priv, RATR0, ratr_value);
			write_nic_byte(priv, UFWP, 1);

			pra->last_ratr = targetRATR;
		}

	}
	else
	{
		pra->ratr_state = DM_RATR_STA_MAX;
	}

}


static void dm_init_bandwidth_autoswitch(struct r8192_priv *priv)
{
	priv->ieee80211->bandwidth_auto_switch.threshold_20Mhzto40Mhz = BW_AUTO_SWITCH_LOW_HIGH;
	priv->ieee80211->bandwidth_auto_switch.threshold_40Mhzto20Mhz = BW_AUTO_SWITCH_HIGH_LOW;
	priv->ieee80211->bandwidth_auto_switch.bforced_tx20Mhz = false;
	priv->ieee80211->bandwidth_auto_switch.bautoswitch_enable = false;

}


static void dm_bandwidth_autoswitch(struct r8192_priv *priv)
{
	if(priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20 ||!priv->ieee80211->bandwidth_auto_switch.bautoswitch_enable){
		return;
	}else{
		if(priv->ieee80211->bandwidth_auto_switch.bforced_tx20Mhz == false){//If send packets in 40 Mhz in 20/40
			if(priv->undecorated_smoothed_pwdb <= priv->ieee80211->bandwidth_auto_switch.threshold_40Mhzto20Mhz)
				priv->ieee80211->bandwidth_auto_switch.bforced_tx20Mhz = true;
		}else{//in force send packets in 20 Mhz in 20/40
			if(priv->undecorated_smoothed_pwdb >= priv->ieee80211->bandwidth_auto_switch.threshold_20Mhzto40Mhz)
				priv->ieee80211->bandwidth_auto_switch.bforced_tx20Mhz = false;

		}
	}
}

//OFDM default at 0db, index=6.
static const u32 OFDMSwingTable[OFDM_Table_Length] = {
	0x7f8001fe,	// 0, +6db
	0x71c001c7,	// 1, +5db
	0x65400195,	// 2, +4db
	0x5a400169,	// 3, +3db
	0x50800142,	// 4, +2db
	0x47c0011f,	// 5, +1db
	0x40000100,	// 6, +0db ===> default, upper for higher temperature, lower for low temperature
	0x390000e4,	// 7, -1db
	0x32c000cb,	// 8, -2db
	0x2d4000b5,	// 9, -3db
	0x288000a2,	// 10, -4db
	0x24000090,	// 11, -5db
	0x20000080,	// 12, -6db
	0x1c800072,	// 13, -7db
	0x19800066,	// 14, -8db
	0x26c0005b,	// 15, -9db
	0x24400051,	// 16, -10db
	0x12000048,	// 17, -11db
	0x10000040	// 18, -12db
};
static const u8 CCKSwingTable_Ch1_Ch13[CCK_Table_length][8] = {
	{0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04},	// 0, +0db ===> CCK40M default
	{0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03},	// 1, -1db
	{0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03},	// 2, -2db
	{0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03},	// 3, -3db
	{0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02},	// 4, -4db
	{0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02},	// 5, -5db
	{0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02},	// 6, -6db ===> CCK20M default
	{0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02},	// 7, -7db
	{0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01},	// 8, -8db
	{0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01},	// 9, -9db
	{0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01},	// 10, -10db
	{0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01}	// 11, -11db
};

static const u8 CCKSwingTable_Ch14[CCK_Table_length][8] = {
	{0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00},	// 0, +0db  ===> CCK40M default
	{0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00},	// 1, -1db
	{0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00},	// 2, -2db
	{0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00},	// 3, -3db
	{0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00},	// 4, -4db
	{0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00},	// 5, -5db
	{0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00},	// 6, -6db  ===> CCK20M default
	{0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00},	// 7, -7db
	{0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00},	// 8, -8db
	{0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00},	// 9, -9db
	{0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00},	// 10, -10db
	{0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00}	// 11, -11db
};

#define		Pw_Track_Flag				0x11d
#define		Tssi_Mea_Value				0x13c
#define		Tssi_Report_Value1			0x134
#define		Tssi_Report_Value2			0x13e
#define		FW_Busy_Flag				0x13f
static void dm_TXPowerTrackingCallback_TSSI(struct r8192_priv *priv)
{
	bool						bHighpowerstate, viviflag = FALSE;
	DCMD_TXCMD_T			tx_cmd;
	u8					powerlevelOFDM24G;
	int	    				i =0, j = 0, k = 0;
	u8						RF_Type, tmp_report[5]={0, 0, 0, 0, 0};
	u32						Value;
	u8						Pwr_Flag;
	u16					Avg_TSSI_Meas, TSSI_13dBm, Avg_TSSI_Meas_from_driver=0;
//	bool rtStatus = true;
	u32						delta=0;
	RT_TRACE(COMP_POWER_TRACKING,"%s()\n",__FUNCTION__);
//	write_nic_byte(priv, 0x1ba, 0);
	write_nic_byte(priv, Pw_Track_Flag, 0);
	write_nic_byte(priv, FW_Busy_Flag, 0);
	priv->ieee80211->bdynamic_txpower_enable = false;
	bHighpowerstate = priv->bDynamicTxHighPower;

	powerlevelOFDM24G = (u8)(priv->Pwr_Track>>24);
	RF_Type = priv->rf_type;
	Value = (RF_Type<<8) | powerlevelOFDM24G;

	RT_TRACE(COMP_POWER_TRACKING, "powerlevelOFDM24G = %x\n", powerlevelOFDM24G);

	for(j = 0; j<=30; j++)
{	//fill tx_cmd

	tx_cmd.Op		= TXCMD_SET_TX_PWR_TRACKING;
	tx_cmd.Length	= 4;
	tx_cmd.Value		= Value;
	cmpk_message_handle_tx(priv, (u8*)&tx_cmd, DESC_PACKET_TYPE_INIT, sizeof(DCMD_TXCMD_T));
	mdelay(1);

	for(i = 0;i <= 30; i++)
	{
		Pwr_Flag = read_nic_byte(priv, Pw_Track_Flag);

		if (Pwr_Flag == 0)
		{
			mdelay(1);
			continue;
		}

		Avg_TSSI_Meas = read_nic_word(priv, Tssi_Mea_Value);

		if(Avg_TSSI_Meas == 0)
		{
			write_nic_byte(priv, Pw_Track_Flag, 0);
			write_nic_byte(priv, FW_Busy_Flag, 0);
			return;
		}

		for(k = 0;k < 5; k++)
		{
			if(k !=4)
				tmp_report[k] = read_nic_byte(priv, Tssi_Report_Value1+k);
			else
				tmp_report[k] = read_nic_byte(priv, Tssi_Report_Value2);

			RT_TRACE(COMP_POWER_TRACKING, "TSSI_report_value = %d\n", tmp_report[k]);
		}

		//check if the report value is right
		for(k = 0;k < 5; k++)
		{
			if(tmp_report[k] <= 20)
			{
				viviflag =TRUE;
				break;
			}
		}
		if(viviflag ==TRUE)
		{
			write_nic_byte(priv, Pw_Track_Flag, 0);
			viviflag = FALSE;
			RT_TRACE(COMP_POWER_TRACKING, "we filted this data\n");
			for(k = 0;k < 5; k++)
				tmp_report[k] = 0;
			break;
		}

		for(k = 0;k < 5; k++)
		{
			Avg_TSSI_Meas_from_driver += tmp_report[k];
		}

		Avg_TSSI_Meas_from_driver = Avg_TSSI_Meas_from_driver*100/5;
		RT_TRACE(COMP_POWER_TRACKING, "Avg_TSSI_Meas_from_driver = %d\n", Avg_TSSI_Meas_from_driver);
		TSSI_13dBm = priv->TSSI_13dBm;
		RT_TRACE(COMP_POWER_TRACKING, "TSSI_13dBm = %d\n", TSSI_13dBm);

		//if(abs(Avg_TSSI_Meas_from_driver - TSSI_13dBm) <= E_FOR_TX_POWER_TRACK)
		// For MacOS-compatible
		if(Avg_TSSI_Meas_from_driver > TSSI_13dBm)
			delta = Avg_TSSI_Meas_from_driver - TSSI_13dBm;
		else
			delta = TSSI_13dBm - Avg_TSSI_Meas_from_driver;

		if(delta <= E_FOR_TX_POWER_TRACK)
		{
			priv->ieee80211->bdynamic_txpower_enable = TRUE;
			write_nic_byte(priv, Pw_Track_Flag, 0);
			write_nic_byte(priv, FW_Busy_Flag, 0);
			RT_TRACE(COMP_POWER_TRACKING, "tx power track is done\n");
			RT_TRACE(COMP_POWER_TRACKING, "priv->rfa_txpowertrackingindex = %d\n", priv->rfa_txpowertrackingindex);
			RT_TRACE(COMP_POWER_TRACKING, "priv->rfa_txpowertrackingindex_real = %d\n", priv->rfa_txpowertrackingindex_real);
			RT_TRACE(COMP_POWER_TRACKING, "priv->CCKPresentAttentuation_difference = %d\n", priv->CCKPresentAttentuation_difference);
			RT_TRACE(COMP_POWER_TRACKING, "priv->CCKPresentAttentuation = %d\n", priv->CCKPresentAttentuation);
			return;
		}
		else
		{
			if(Avg_TSSI_Meas_from_driver < TSSI_13dBm - E_FOR_TX_POWER_TRACK)
			{
				if (RF_Type == RF_2T4R)
				{

						if((priv->rfa_txpowertrackingindex > 0) &&(priv->rfc_txpowertrackingindex > 0))
				{
					priv->rfa_txpowertrackingindex--;
					if(priv->rfa_txpowertrackingindex_real > 4)
					{
						priv->rfa_txpowertrackingindex_real--;
						rtl8192_setBBreg(priv, rOFDM0_XATxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfa_txpowertrackingindex_real].txbbgain_value);
					}

					priv->rfc_txpowertrackingindex--;
					if(priv->rfc_txpowertrackingindex_real > 4)
					{
						priv->rfc_txpowertrackingindex_real--;
						rtl8192_setBBreg(priv, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfc_txpowertrackingindex_real].txbbgain_value);
					}
						}
						else
						{
								rtl8192_setBBreg(priv, rOFDM0_XATxIQImbalance, bMaskDWord, priv->txbbgain_table[4].txbbgain_value);
								rtl8192_setBBreg(priv, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[4].txbbgain_value);
				}
			}
			else
			{
						if(priv->rfc_txpowertrackingindex > 0)
						{
							priv->rfc_txpowertrackingindex--;
							if(priv->rfc_txpowertrackingindex_real > 4)
							{
								priv->rfc_txpowertrackingindex_real--;
								rtl8192_setBBreg(priv, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfc_txpowertrackingindex_real].txbbgain_value);
							}
						}
						else
							rtl8192_setBBreg(priv, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[4].txbbgain_value);
				}
			}
			else
			{
				if (RF_Type == RF_2T4R)
				{
					if((priv->rfa_txpowertrackingindex < TxBBGainTableLength - 1) &&(priv->rfc_txpowertrackingindex < TxBBGainTableLength - 1))
				{
					priv->rfa_txpowertrackingindex++;
					priv->rfa_txpowertrackingindex_real++;
					rtl8192_setBBreg(priv, rOFDM0_XATxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfa_txpowertrackingindex_real].txbbgain_value);
					priv->rfc_txpowertrackingindex++;
					priv->rfc_txpowertrackingindex_real++;
					rtl8192_setBBreg(priv, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfc_txpowertrackingindex_real].txbbgain_value);
				}
					else
					{
						rtl8192_setBBreg(priv, rOFDM0_XATxIQImbalance, bMaskDWord, priv->txbbgain_table[TxBBGainTableLength - 1].txbbgain_value);
						rtl8192_setBBreg(priv, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[TxBBGainTableLength - 1].txbbgain_value);
			}
				}
				else
				{
					if(priv->rfc_txpowertrackingindex < (TxBBGainTableLength - 1))
					{
							priv->rfc_txpowertrackingindex++;
							priv->rfc_txpowertrackingindex_real++;
							rtl8192_setBBreg(priv, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[priv->rfc_txpowertrackingindex_real].txbbgain_value);
					}
					else
							rtl8192_setBBreg(priv, rOFDM0_XCTxIQImbalance, bMaskDWord, priv->txbbgain_table[TxBBGainTableLength - 1].txbbgain_value);
				}
			}
			if (RF_Type == RF_2T4R)
			priv->CCKPresentAttentuation_difference
				= priv->rfa_txpowertrackingindex - priv->rfa_txpowertracking_default;
			else
				priv->CCKPresentAttentuation_difference
					= priv->rfc_txpowertrackingindex - priv->rfc_txpowertracking_default;

			if(priv->CurrentChannelBW == HT_CHANNEL_WIDTH_20)
				priv->CCKPresentAttentuation
				= priv->CCKPresentAttentuation_20Mdefault + priv->CCKPresentAttentuation_difference;
			else
				priv->CCKPresentAttentuation
				= priv->CCKPresentAttentuation_40Mdefault + priv->CCKPresentAttentuation_difference;

			if(priv->CCKPresentAttentuation > (CCKTxBBGainTableLength-1))
					priv->CCKPresentAttentuation = CCKTxBBGainTableLength-1;
			if(priv->CCKPresentAttentuation < 0)
					priv->CCKPresentAttentuation = 0;

			if(1)
			{
				if(priv->ieee80211->current_network.channel == 14 && !priv->bcck_in_ch14)
				{
					priv->bcck_in_ch14 = TRUE;
					dm_cck_txpower_adjust(priv, priv->bcck_in_ch14);
				}
				else if(priv->ieee80211->current_network.channel != 14 && priv->bcck_in_ch14)
				{
					priv->bcck_in_ch14 = FALSE;
					dm_cck_txpower_adjust(priv, priv->bcck_in_ch14);
				}
				else
					dm_cck_txpower_adjust(priv, priv->bcck_in_ch14);
			}
		RT_TRACE(COMP_POWER_TRACKING, "priv->rfa_txpowertrackingindex = %d\n", priv->rfa_txpowertrackingindex);
		RT_TRACE(COMP_POWER_TRACKING, "priv->rfa_txpowertrackingindex_real = %d\n", priv->rfa_txpowertrackingindex_real);
		RT_TRACE(COMP_POWER_TRACKING, "priv->CCKPresentAttentuation_difference = %d\n", priv->CCKPresentAttentuation_difference);
		RT_TRACE(COMP_POWER_TRACKING, "priv->CCKPresentAttentuation = %d\n", priv->CCKPresentAttentuation);

		if (priv->CCKPresentAttentuation_difference <= -12||priv->CCKPresentAttentuation_difference >= 24)
		{
			priv->ieee80211->bdynamic_txpower_enable = TRUE;
			write_nic_byte(priv, Pw_Track_Flag, 0);
			write_nic_byte(priv, FW_Busy_Flag, 0);
			RT_TRACE(COMP_POWER_TRACKING, "tx power track--->limited\n");
			return;
		}


	}
		write_nic_byte(priv, Pw_Track_Flag, 0);
		Avg_TSSI_Meas_from_driver = 0;
		for(k = 0;k < 5; k++)
			tmp_report[k] = 0;
		break;
	}
	write_nic_byte(priv, FW_Busy_Flag, 0);
}
		priv->ieee80211->bdynamic_txpower_enable = TRUE;
		write_nic_byte(priv, Pw_Track_Flag, 0);
}

static void dm_TXPowerTrackingCallback_ThermalMeter(struct r8192_priv *priv)
{
#define ThermalMeterVal	9
	u32 tmpRegA, TempCCk;
	u8 tmpOFDMindex, tmpCCKindex, tmpCCK20Mindex, tmpCCK40Mindex, tmpval;
	int i =0, CCKSwingNeedUpdate=0;

	if(!priv->btxpower_trackingInit)
	{
		//Query OFDM default setting
		tmpRegA = rtl8192_QueryBBReg(priv, rOFDM0_XATxIQImbalance, bMaskDWord);
		for(i=0; i<OFDM_Table_Length; i++)	//find the index
		{
			if(tmpRegA == OFDMSwingTable[i])
			{
				priv->OFDM_index= (u8)i;
				RT_TRACE(COMP_POWER_TRACKING, "Initial reg0x%x = 0x%x, OFDM_index=0x%x\n",
					rOFDM0_XATxIQImbalance, tmpRegA, priv->OFDM_index);
			}
		}

		//Query CCK default setting From 0xa22
		TempCCk = rtl8192_QueryBBReg(priv, rCCK0_TxFilter1, bMaskByte2);
		for(i=0 ; i<CCK_Table_length ; i++)
		{
			if(TempCCk == (u32)CCKSwingTable_Ch1_Ch13[i][0])
			{
				priv->CCK_index =(u8) i;
				RT_TRACE(COMP_POWER_TRACKING, "Initial reg0x%x = 0x%x, CCK_index=0x%x\n",
					rCCK0_TxFilter1, TempCCk, priv->CCK_index);
				break;
			}
		}
		priv->btxpower_trackingInit = TRUE;
		//pHalData->TXPowercount = 0;
		return;
	}

	// read and filter out unreasonable value
	tmpRegA = rtl8192_phy_QueryRFReg(priv, RF90_PATH_A, 0x12, 0x078);	// 0x12: RF Reg[10:7]
	RT_TRACE(COMP_POWER_TRACKING, "Readback ThermalMeterA = %d\n", tmpRegA);
	if(tmpRegA < 3 || tmpRegA > 13)
		return;
	if(tmpRegA >= 12)	// if over 12, TP will be bad when high temperature
		tmpRegA = 12;
	RT_TRACE(COMP_POWER_TRACKING, "Valid ThermalMeterA = %d\n", tmpRegA);
	priv->ThermalMeter[0] = ThermalMeterVal;	//We use fixed value by Bryant's suggestion
	priv->ThermalMeter[1] = ThermalMeterVal;	//We use fixed value by Bryant's suggestion

	//Get current RF-A temperature index
	if(priv->ThermalMeter[0] >= (u8)tmpRegA)	//lower temperature
	{
		tmpOFDMindex = tmpCCK20Mindex = 6+(priv->ThermalMeter[0]-(u8)tmpRegA);
		tmpCCK40Mindex = tmpCCK20Mindex - 6;
		if(tmpOFDMindex >= OFDM_Table_Length)
			tmpOFDMindex = OFDM_Table_Length-1;
		if(tmpCCK20Mindex >= CCK_Table_length)
			tmpCCK20Mindex = CCK_Table_length-1;
		if(tmpCCK40Mindex >= CCK_Table_length)
			tmpCCK40Mindex = CCK_Table_length-1;
	}
	else
	{
		tmpval = ((u8)tmpRegA - priv->ThermalMeter[0]);
		if(tmpval >= 6)								// higher temperature
			tmpOFDMindex = tmpCCK20Mindex = 0;		// max to +6dB
		else
			tmpOFDMindex = tmpCCK20Mindex = 6 - tmpval;
		tmpCCK40Mindex = 0;
	}

	if(priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)	//40M
		tmpCCKindex = tmpCCK40Mindex;
	else
		tmpCCKindex = tmpCCK20Mindex;

	//record for bandwidth swith
	priv->Record_CCK_20Mindex = tmpCCK20Mindex;
	priv->Record_CCK_40Mindex = tmpCCK40Mindex;
	RT_TRACE(COMP_POWER_TRACKING, "Record_CCK_20Mindex / Record_CCK_40Mindex = %d / %d.\n",
		priv->Record_CCK_20Mindex, priv->Record_CCK_40Mindex);

	if(priv->ieee80211->current_network.channel == 14 && !priv->bcck_in_ch14)
	{
		priv->bcck_in_ch14 = TRUE;
		CCKSwingNeedUpdate = 1;
	}
	else if(priv->ieee80211->current_network.channel != 14 && priv->bcck_in_ch14)
	{
		priv->bcck_in_ch14 = FALSE;
		CCKSwingNeedUpdate = 1;
	}

	if(priv->CCK_index != tmpCCKindex)
{
		priv->CCK_index = tmpCCKindex;
		CCKSwingNeedUpdate = 1;
	}

	if(CCKSwingNeedUpdate)
	{
		dm_cck_txpower_adjust(priv, priv->bcck_in_ch14);
	}
	if(priv->OFDM_index != tmpOFDMindex)
	{
		priv->OFDM_index = tmpOFDMindex;
		rtl8192_setBBreg(priv, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[priv->OFDM_index]);
		RT_TRACE(COMP_POWER_TRACKING, "Update OFDMSwing[%d] = 0x%x\n",
			priv->OFDM_index, OFDMSwingTable[priv->OFDM_index]);
	}
	priv->txpower_count = 0;
}

void dm_txpower_trackingcallback(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work,struct delayed_work,work);
	struct r8192_priv *priv = container_of(dwork,struct r8192_priv,txpower_tracking_wq);

	if(priv->IC_Cut >= IC_VersionCut_D)
		dm_TXPowerTrackingCallback_TSSI(priv);
	else
		dm_TXPowerTrackingCallback_ThermalMeter(priv);
}


static const txbbgain_struct rtl8192_txbbgain_table[] = {
	{ 12,	0x7f8001fe },
	{ 11,	0x788001e2 },
	{ 10,	0x71c001c7 },
	{ 9,	0x6b8001ae },
	{ 8,	0x65400195 },
	{ 7,	0x5fc0017f },
	{ 6,	0x5a400169 },
	{ 5,	0x55400155 },
	{ 4,	0x50800142 },
	{ 3,	0x4c000130 },
	{ 2,	0x47c0011f },
	{ 1,	0x43c0010f },
	{ 0,	0x40000100 },
	{ -1,	0x3c8000f2 },
	{ -2,	0x390000e4 },
	{ -3,	0x35c000d7 },
	{ -4,	0x32c000cb },
	{ -5,	0x300000c0 },
	{ -6,	0x2d4000b5 },
	{ -7,	0x2ac000ab },
	{ -8,	0x288000a2 },
	{ -9,	0x26000098 },
	{ -10,	0x24000090 },
	{ -11,	0x22000088 },
	{ -12,	0x20000080 },
	{ -13,	0x1a00006c },
	{ -14,	0x1c800072 },
	{ -15,	0x18000060 },
	{ -16,	0x19800066 },
	{ -17,	0x15800056 },
	{ -18,	0x26c0005b },
	{ -19,	0x14400051 },
	{ -20,	0x24400051 },
	{ -21,	0x1300004c },
	{ -22,	0x12000048 },
	{ -23,	0x11000044 },
	{ -24,	0x10000040 },
};

/*
 * ccktxbb_valuearray[0] is 0xA22 [1] is 0xA24 ...[7] is 0xA29
 * This Table is for CH1~CH13
 */
static const ccktxbbgain_struct rtl8192_cck_txbbgain_table[] = {
	{{ 0x36, 0x35, 0x2e, 0x25, 0x1c, 0x12, 0x09, 0x04 }},
	{{ 0x33, 0x32, 0x2b, 0x23, 0x1a, 0x11, 0x08, 0x04 }},
	{{ 0x30, 0x2f, 0x29, 0x21, 0x19, 0x10, 0x08, 0x03 }},
	{{ 0x2d, 0x2d, 0x27, 0x1f, 0x18, 0x0f, 0x08, 0x03 }},
	{{ 0x2b, 0x2a, 0x25, 0x1e, 0x16, 0x0e, 0x07, 0x03 }},
	{{ 0x28, 0x28, 0x22, 0x1c, 0x15, 0x0d, 0x07, 0x03 }},
	{{ 0x26, 0x25, 0x21, 0x1b, 0x14, 0x0d, 0x06, 0x03 }},
	{{ 0x24, 0x23, 0x1f, 0x19, 0x13, 0x0c, 0x06, 0x03 }},
	{{ 0x22, 0x21, 0x1d, 0x18, 0x11, 0x0b, 0x06, 0x02 }},
	{{ 0x20, 0x20, 0x1b, 0x16, 0x11, 0x08, 0x05, 0x02 }},
	{{ 0x1f, 0x1e, 0x1a, 0x15, 0x10, 0x0a, 0x05, 0x02 }},
	{{ 0x1d, 0x1c, 0x18, 0x14, 0x0f, 0x0a, 0x05, 0x02 }},
	{{ 0x1b, 0x1a, 0x17, 0x13, 0x0e, 0x09, 0x04, 0x02 }},
	{{ 0x1a, 0x19, 0x16, 0x12, 0x0d, 0x09, 0x04, 0x02 }},
	{{ 0x18, 0x17, 0x15, 0x11, 0x0c, 0x08, 0x04, 0x02 }},
	{{ 0x17, 0x16, 0x13, 0x10, 0x0c, 0x08, 0x04, 0x02 }},
	{{ 0x16, 0x15, 0x12, 0x0f, 0x0b, 0x07, 0x04, 0x01 }},
	{{ 0x14, 0x14, 0x11, 0x0e, 0x0b, 0x07, 0x03, 0x02 }},
	{{ 0x13, 0x13, 0x10, 0x0d, 0x0a, 0x06, 0x03, 0x01 }},
	{{ 0x12, 0x12, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01 }},
	{{ 0x11, 0x11, 0x0f, 0x0c, 0x09, 0x06, 0x03, 0x01 }},
	{{ 0x10, 0x10, 0x0e, 0x0b, 0x08, 0x05, 0x03, 0x01 }},
	{{ 0x0f, 0x0f, 0x0d, 0x0b, 0x08, 0x05, 0x03, 0x01 }},
};

/*
 * ccktxbb_valuearray[0] is 0xA22 [1] is 0xA24 ...[7] is 0xA29
 * This Table is for CH14
 */
static const ccktxbbgain_struct rtl8192_cck_txbbgain_ch14_table[] = {
	{{ 0x36, 0x35, 0x2e, 0x1b, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x33, 0x32, 0x2b, 0x19, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x30, 0x2f, 0x29, 0x18, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x2d, 0x2d, 0x27, 0x17, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x2b, 0x2a, 0x25, 0x15, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x28, 0x28, 0x22, 0x14, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x26, 0x25, 0x21, 0x13, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x24, 0x23, 0x1f, 0x12, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x22, 0x21, 0x1d, 0x11, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x20, 0x20, 0x1b, 0x10, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x1f, 0x1e, 0x1a, 0x0f, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x1d, 0x1c, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x1b, 0x1a, 0x17, 0x0e, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x1a, 0x19, 0x16, 0x0d, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x18, 0x17, 0x15, 0x0c, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x17, 0x16, 0x13, 0x0b, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x16, 0x15, 0x12, 0x0b, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x14, 0x14, 0x11, 0x0a, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x13, 0x13, 0x10, 0x0a, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x12, 0x12, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x11, 0x11, 0x0f, 0x09, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x10, 0x10, 0x0e, 0x08, 0x00, 0x00, 0x00, 0x00 }},
	{{ 0x0f, 0x0f, 0x0d, 0x08, 0x00, 0x00, 0x00, 0x00 }},
};

static void dm_InitializeTXPowerTracking_TSSI(struct r8192_priv *priv)
{
	priv->txbbgain_table = rtl8192_txbbgain_table;
	priv->cck_txbbgain_table = rtl8192_cck_txbbgain_table;
	priv->cck_txbbgain_ch14_table = rtl8192_cck_txbbgain_ch14_table;

	priv->btxpower_tracking = TRUE;
	priv->txpower_count       = 0;
	priv->btxpower_trackingInit = FALSE;

}

static void dm_InitializeTXPowerTracking_ThermalMeter(struct r8192_priv *priv)
{
	// Tx Power tracking by Theremal Meter require Firmware R/W 3-wire. This mechanism
	// can be enabled only when Firmware R/W 3-wire is enabled. Otherwise, frequent r/w
	// 3-wire by driver cause RF goes into wrong state.
	if(priv->ieee80211->FwRWRF)
		priv->btxpower_tracking = TRUE;
	else
		priv->btxpower_tracking = FALSE;
	priv->txpower_count       = 0;
	priv->btxpower_trackingInit = FALSE;
}

void dm_initialize_txpower_tracking(struct r8192_priv *priv)
{
	if(priv->IC_Cut >= IC_VersionCut_D)
		dm_InitializeTXPowerTracking_TSSI(priv);
	else
		dm_InitializeTXPowerTracking_ThermalMeter(priv);
}


static void dm_CheckTXPowerTracking_TSSI(struct r8192_priv *priv)
{
	static u32 tx_power_track_counter = 0;
	RT_TRACE(COMP_POWER_TRACKING,"%s()\n",__FUNCTION__);
	if(read_nic_byte(priv, 0x11e) ==1)
		return;
	if(!priv->btxpower_tracking)
		return;
	tx_power_track_counter++;

	if (tx_power_track_counter > 90) {
		queue_delayed_work(priv->priv_wq,&priv->txpower_tracking_wq,0);
		tx_power_track_counter =0;
	}
}

static void dm_CheckTXPowerTracking_ThermalMeter(struct r8192_priv *priv)
{
	static u8 	TM_Trigger=0;

	if(!priv->btxpower_tracking)
		return;
	else
	{
		if(priv->txpower_count  <= 2)
		{
			priv->txpower_count++;
			return;
		}
	}

	if(!TM_Trigger)
	{
		//Attention!! You have to wirte all 12bits data to RF, or it may cause RF to crash
		//actually write reg0x02 bit1=0, then bit1=1.
		rtl8192_phy_SetRFReg(priv, RF90_PATH_A, 0x02, bMask12Bits, 0x4d);
		rtl8192_phy_SetRFReg(priv, RF90_PATH_A, 0x02, bMask12Bits, 0x4f);
		rtl8192_phy_SetRFReg(priv, RF90_PATH_A, 0x02, bMask12Bits, 0x4d);
		rtl8192_phy_SetRFReg(priv, RF90_PATH_A, 0x02, bMask12Bits, 0x4f);
		TM_Trigger = 1;
		return;
	}
	else {
		queue_delayed_work(priv->priv_wq,&priv->txpower_tracking_wq,0);
		TM_Trigger = 0;
	}
}

static void dm_check_txpower_tracking(struct r8192_priv *priv)
{
	if(priv->IC_Cut >= IC_VersionCut_D)
		dm_CheckTXPowerTracking_TSSI(priv);
	else
		dm_CheckTXPowerTracking_ThermalMeter(priv);
}


static void dm_CCKTxPowerAdjust_TSSI(struct r8192_priv *priv, bool bInCH14)
{
	u32 TempVal;
	//Write 0xa22 0xa23
	TempVal = 0;
	if(!bInCH14){
		//Write 0xa22 0xa23
		TempVal = 	(u32)(priv->cck_txbbgain_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[0] +
					(priv->cck_txbbgain_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[1]<<8)) ;

		rtl8192_setBBreg(priv, rCCK0_TxFilter1, bMaskHWord, TempVal);
		//Write 0xa24 ~ 0xa27
		TempVal = 0;
		TempVal = 	(u32)(priv->cck_txbbgain_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[2] +
					(priv->cck_txbbgain_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[3]<<8) +
					(priv->cck_txbbgain_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[4]<<16 )+
					(priv->cck_txbbgain_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[5]<<24));
		rtl8192_setBBreg(priv, rCCK0_TxFilter2, bMaskDWord, TempVal);
		//Write 0xa28  0xa29
		TempVal = 0;
		TempVal = 	(u32)(priv->cck_txbbgain_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[6] +
					(priv->cck_txbbgain_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[7]<<8)) ;

		rtl8192_setBBreg(priv, rCCK0_DebugPort, bMaskLWord, TempVal);
	}
	else
	{
		TempVal = 	(u32)(priv->cck_txbbgain_ch14_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[0] +
					(priv->cck_txbbgain_ch14_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[1]<<8)) ;

		rtl8192_setBBreg(priv, rCCK0_TxFilter1, bMaskHWord, TempVal);
		//Write 0xa24 ~ 0xa27
		TempVal = 0;
		TempVal = 	(u32)(priv->cck_txbbgain_ch14_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[2] +
					(priv->cck_txbbgain_ch14_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[3]<<8) +
					(priv->cck_txbbgain_ch14_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[4]<<16 )+
					(priv->cck_txbbgain_ch14_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[5]<<24));
		rtl8192_setBBreg(priv, rCCK0_TxFilter2, bMaskDWord, TempVal);
		//Write 0xa28  0xa29
		TempVal = 0;
		TempVal = 	(u32)(priv->cck_txbbgain_ch14_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[6] +
					(priv->cck_txbbgain_ch14_table[(u8)(priv->CCKPresentAttentuation)].ccktxbb_valuearray[7]<<8)) ;

		rtl8192_setBBreg(priv, rCCK0_DebugPort, bMaskLWord, TempVal);
	}


}

static void dm_CCKTxPowerAdjust_ThermalMeter(struct r8192_priv *priv,
					     bool bInCH14)
{
	u32 TempVal;

	TempVal = 0;
	if(!bInCH14)
	{
		//Write 0xa22 0xa23
		TempVal = 	CCKSwingTable_Ch1_Ch13[priv->CCK_index][0] +
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][1]<<8) ;
		rtl8192_setBBreg(priv, rCCK0_TxFilter1, bMaskHWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK not chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_TxFilter1, TempVal);
		//Write 0xa24 ~ 0xa27
		TempVal = 0;
		TempVal = 	CCKSwingTable_Ch1_Ch13[priv->CCK_index][2] +
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][3]<<8) +
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][4]<<16 )+
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][5]<<24);
		rtl8192_setBBreg(priv, rCCK0_TxFilter2, bMaskDWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK not chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_TxFilter2, TempVal);
		//Write 0xa28  0xa29
		TempVal = 0;
		TempVal = 	CCKSwingTable_Ch1_Ch13[priv->CCK_index][6] +
					(CCKSwingTable_Ch1_Ch13[priv->CCK_index][7]<<8) ;

		rtl8192_setBBreg(priv, rCCK0_DebugPort, bMaskLWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK not chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_DebugPort, TempVal);
	}
	else
	{
//		priv->CCKTxPowerAdjustCntNotCh14++;	//cosa add for debug.
		//Write 0xa22 0xa23
		TempVal = 	CCKSwingTable_Ch14[priv->CCK_index][0] +
					(CCKSwingTable_Ch14[priv->CCK_index][1]<<8) ;

		rtl8192_setBBreg(priv, rCCK0_TxFilter1, bMaskHWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_TxFilter1, TempVal);
		//Write 0xa24 ~ 0xa27
		TempVal = 0;
		TempVal = 	CCKSwingTable_Ch14[priv->CCK_index][2] +
					(CCKSwingTable_Ch14[priv->CCK_index][3]<<8) +
					(CCKSwingTable_Ch14[priv->CCK_index][4]<<16 )+
					(CCKSwingTable_Ch14[priv->CCK_index][5]<<24);
		rtl8192_setBBreg(priv, rCCK0_TxFilter2, bMaskDWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING, "CCK chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_TxFilter2, TempVal);
		//Write 0xa28  0xa29
		TempVal = 0;
		TempVal = 	CCKSwingTable_Ch14[priv->CCK_index][6] +
					(CCKSwingTable_Ch14[priv->CCK_index][7]<<8) ;

		rtl8192_setBBreg(priv, rCCK0_DebugPort, bMaskLWord, TempVal);
		RT_TRACE(COMP_POWER_TRACKING,"CCK chnl 14, reg 0x%x = 0x%x\n",
			rCCK0_DebugPort, TempVal);
	}
}

void dm_cck_txpower_adjust(struct r8192_priv *priv, bool binch14)
{
	if(priv->IC_Cut >= IC_VersionCut_D)
		dm_CCKTxPowerAdjust_TSSI(priv, binch14);
	else
		dm_CCKTxPowerAdjust_ThermalMeter(priv, binch14);
}

/* Set DIG scheme init value. */
static void dm_dig_init(struct r8192_priv *priv)
{
	/* 2007/10/05 MH Disable DIG scheme now. Not tested. */
	dm_digtable.dig_enable_flag	= true;
	dm_digtable.dig_algorithm = DIG_ALGO_BY_RSSI;
	dm_digtable.dbg_mode = DM_DBG_OFF;	//off=by real rssi value, on=by DM_DigTable.Rssi_val for new dig
	dm_digtable.dig_algorithm_switch = 0;

	/* 2007/10/04 MH Define init gain threshold. */
	dm_digtable.dig_state		= DM_STA_DIG_MAX;
	dm_digtable.dig_highpwr_state	= DM_STA_DIG_MAX;
	dm_digtable.initialgain_lowerbound_state = false;

	dm_digtable.rssi_low_thresh 	= DM_DIG_THRESH_LOW;
	dm_digtable.rssi_high_thresh 	= DM_DIG_THRESH_HIGH;

	dm_digtable.rssi_high_power_lowthresh = DM_DIG_HIGH_PWR_THRESH_LOW;
	dm_digtable.rssi_high_power_highthresh = DM_DIG_HIGH_PWR_THRESH_HIGH;

	dm_digtable.rssi_val = 50;	//for new dig debug rssi value
	dm_digtable.backoff_val = DM_DIG_BACKOFF;
	dm_digtable.rx_gain_range_max = DM_DIG_MAX;
	if(priv->CustomerID == RT_CID_819x_Netcore)
		dm_digtable.rx_gain_range_min = DM_DIG_MIN_Netcore;
	else
		dm_digtable.rx_gain_range_min = DM_DIG_MIN;

}


/*
 * Driver must monitor RSSI and notify firmware to change initial
 * gain according to different threshold. BB team provide the
 * suggested solution.
 */
static void dm_ctrl_initgain_byrssi(struct r8192_priv *priv)
{
	if (dm_digtable.dig_enable_flag == false)
		return;

	if(dm_digtable.dig_algorithm == DIG_ALGO_BY_FALSE_ALARM)
		dm_ctrl_initgain_byrssi_by_fwfalse_alarm(priv);
	else if(dm_digtable.dig_algorithm == DIG_ALGO_BY_RSSI)
		dm_ctrl_initgain_byrssi_by_driverrssi(priv);
}


static void dm_ctrl_initgain_byrssi_by_driverrssi(struct r8192_priv *priv)
{
	u8 i;
	static u8 	fw_dig=0;

	if (dm_digtable.dig_enable_flag == false)
		return;

	if(dm_digtable.dig_algorithm_switch)	// if swithed algorithm, we have to disable FW Dig.
		fw_dig = 0;
	if(fw_dig <= 3)	// execute several times to make sure the FW Dig is disabled
	{// FW DIG Off
		for(i=0; i<3; i++)
			rtl8192_setBBreg(priv, UFWP, bMaskByte1, 0x8);	// Only clear byte 1 and rewrite.
		fw_dig++;
		dm_digtable.dig_state = DM_STA_DIG_OFF;	//fw dig off.
	}

	if(priv->ieee80211->state == IEEE80211_LINKED)
		dm_digtable.cur_connect_state = DIG_CONNECT;
	else
		dm_digtable.cur_connect_state = DIG_DISCONNECT;

	if(dm_digtable.dbg_mode == DM_DBG_OFF)
		dm_digtable.rssi_val = priv->undecorated_smoothed_pwdb;

	dm_initial_gain(priv);
	dm_pd_th(priv);
	dm_cs_ratio(priv);
	if(dm_digtable.dig_algorithm_switch)
		dm_digtable.dig_algorithm_switch = 0;
	dm_digtable.pre_connect_state = dm_digtable.cur_connect_state;

}

static void dm_ctrl_initgain_byrssi_by_fwfalse_alarm(struct r8192_priv *priv)
{
	static u32 reset_cnt = 0;
	u8 i;

	if (dm_digtable.dig_enable_flag == false)
		return;

	if(dm_digtable.dig_algorithm_switch)
	{
		dm_digtable.dig_state = DM_STA_DIG_MAX;
		// Fw DIG On.
		for(i=0; i<3; i++)
			rtl8192_setBBreg(priv, UFWP, bMaskByte1, 0x1);	// Only clear byte 1 and rewrite.
		dm_digtable.dig_algorithm_switch = 0;
	}

	if (priv->ieee80211->state != IEEE80211_LINKED)
		return;

	// For smooth, we can not change DIG state.
	if ((priv->undecorated_smoothed_pwdb > dm_digtable.rssi_low_thresh) &&
		(priv->undecorated_smoothed_pwdb < dm_digtable.rssi_high_thresh))
	{
		return;
	}

	/* 1. When RSSI decrease, We have to judge if it is smaller than a threshold
		  and then execute below step. */
	if ((priv->undecorated_smoothed_pwdb <= dm_digtable.rssi_low_thresh))
	{
		/* 2008/02/05 MH When we execute silent reset, the DIG PHY parameters
		   will be reset to init value. We must prevent the condition. */
		if (dm_digtable.dig_state == DM_STA_DIG_OFF &&
			(priv->reset_count == reset_cnt))
		{
			return;
		}
		else
		{
			reset_cnt = priv->reset_count;
		}

		// If DIG is off, DIG high power state must reset.
		dm_digtable.dig_highpwr_state = DM_STA_DIG_MAX;
		dm_digtable.dig_state = DM_STA_DIG_OFF;

		// 1.1 DIG Off.
		rtl8192_setBBreg(priv, UFWP, bMaskByte1, 0x8);	// Only clear byte 1 and rewrite.

		// 1.2 Set initial gain.
		write_nic_byte(priv, rOFDM0_XAAGCCore1, 0x17);
		write_nic_byte(priv, rOFDM0_XBAGCCore1, 0x17);
		write_nic_byte(priv, rOFDM0_XCAGCCore1, 0x17);
		write_nic_byte(priv, rOFDM0_XDAGCCore1, 0x17);

		// 1.3 Lower PD_TH for OFDM.
		if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
		{
			/* 2008/01/11 MH 40MHZ 90/92 register are not the same. */
			// 2008/02/05 MH SD3-Jerry 92U/92E PD_TH are the same.
			write_nic_byte(priv, (rOFDM0_XATxAFE+3), 0x00);
		}
		else
			write_nic_byte(priv, rOFDM0_RxDetector1, 0x42);

		// 1.4 Lower CS ratio for CCK.
		write_nic_byte(priv, 0xa0a, 0x08);

		// 1.5 Higher EDCCA.
		//PlatformEFIOWrite4Byte(pAdapter, rOFDM0_ECCAThreshold, 0x325);
		return;

	}

	/* 2. When RSSI increase, We have to judge if it is larger than a threshold
		  and then execute below step.  */
	if ((priv->undecorated_smoothed_pwdb >= dm_digtable.rssi_high_thresh) )
	{
		u8 reset_flag = 0;

		if (dm_digtable.dig_state == DM_STA_DIG_ON &&
			(priv->reset_count == reset_cnt))
		{
			dm_ctrl_initgain_byrssi_highpwr(priv);
			return;
		}
		else
		{
			if (priv->reset_count != reset_cnt)
				reset_flag = 1;

			reset_cnt = priv->reset_count;
		}

		dm_digtable.dig_state = DM_STA_DIG_ON;

		// 2.1 Set initial gain.
		// 2008/02/26 MH SD3-Jerry suggest to prevent dirty environment.
		if (reset_flag == 1)
		{
			write_nic_byte(priv, rOFDM0_XAAGCCore1, 0x2c);
			write_nic_byte(priv, rOFDM0_XBAGCCore1, 0x2c);
			write_nic_byte(priv, rOFDM0_XCAGCCore1, 0x2c);
			write_nic_byte(priv, rOFDM0_XDAGCCore1, 0x2c);
		}
		else
		{
			write_nic_byte(priv, rOFDM0_XAAGCCore1, 0x20);
			write_nic_byte(priv, rOFDM0_XBAGCCore1, 0x20);
			write_nic_byte(priv, rOFDM0_XCAGCCore1, 0x20);
			write_nic_byte(priv, rOFDM0_XDAGCCore1, 0x20);
		}

		// 2.2 Higher PD_TH for OFDM.
		if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
		{
			/* 2008/01/11 MH 40MHZ 90/92 register are not the same. */
			// 2008/02/05 MH SD3-Jerry 92U/92E PD_TH are the same.
			write_nic_byte(priv, (rOFDM0_XATxAFE+3), 0x20);
		}
		else
			write_nic_byte(priv, rOFDM0_RxDetector1, 0x44);

		// 2.3 Higher CS ratio for CCK.
		write_nic_byte(priv, 0xa0a, 0xcd);

		// 2.4 Lower EDCCA.
		/* 2008/01/11 MH 90/92 series are the same. */
		//PlatformEFIOWrite4Byte(pAdapter, rOFDM0_ECCAThreshold, 0x346);

		// 2.5 DIG On.
		rtl8192_setBBreg(priv, UFWP, bMaskByte1, 0x1);	// Only clear byte 1 and rewrite.

	}

	dm_ctrl_initgain_byrssi_highpwr(priv);

}

static void dm_ctrl_initgain_byrssi_highpwr(struct r8192_priv *priv)
{
	static u32 reset_cnt_highpwr = 0;

	// For smooth, we can not change high power DIG state in the range.
	if ((priv->undecorated_smoothed_pwdb > dm_digtable.rssi_high_power_lowthresh) &&
		(priv->undecorated_smoothed_pwdb < dm_digtable.rssi_high_power_highthresh))
	{
		return;
	}

	/* 3. When RSSI >75% or <70%, it is a high power issue. We have to judge if
		  it is larger than a threshold and then execute below step.  */
	// 2008/02/05 MH SD3-Jerry Modify PD_TH for high power issue.
	if (priv->undecorated_smoothed_pwdb >= dm_digtable.rssi_high_power_highthresh)
	{
		if (dm_digtable.dig_highpwr_state == DM_STA_DIG_ON &&
			(priv->reset_count == reset_cnt_highpwr))
			return;
		else
			dm_digtable.dig_highpwr_state = DM_STA_DIG_ON;

		// 3.1 Higher PD_TH for OFDM for high power state.
		if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
		{
			write_nic_byte(priv, (rOFDM0_XATxAFE+3), 0x10);
		}
		else
			write_nic_byte(priv, rOFDM0_RxDetector1, 0x43);
	}
	else
	{
		if (dm_digtable.dig_highpwr_state == DM_STA_DIG_OFF&&
			(priv->reset_count == reset_cnt_highpwr))
			return;
		else
			dm_digtable.dig_highpwr_state = DM_STA_DIG_OFF;

		if (priv->undecorated_smoothed_pwdb < dm_digtable.rssi_high_power_lowthresh &&
			 priv->undecorated_smoothed_pwdb >= dm_digtable.rssi_high_thresh)
		{
			// 3.2 Recover PD_TH for OFDM for normal power region.
			if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
			{
				write_nic_byte(priv, (rOFDM0_XATxAFE+3), 0x20);
			}
			else
				write_nic_byte(priv, rOFDM0_RxDetector1, 0x44);
		}
	}

	reset_cnt_highpwr = priv->reset_count;

}


static void dm_initial_gain(struct r8192_priv *priv)
{
	u8					initial_gain=0;
	static u8				initialized=0, force_write=0;
	static u32			reset_cnt=0;

	if(dm_digtable.dig_algorithm_switch)
	{
		initialized = 0;
		reset_cnt = 0;
	}

	if(dm_digtable.pre_connect_state == dm_digtable.cur_connect_state)
	{
		if(dm_digtable.cur_connect_state == DIG_CONNECT)
		{
			if((dm_digtable.rssi_val+10-dm_digtable.backoff_val) > dm_digtable.rx_gain_range_max)
				dm_digtable.cur_ig_value = dm_digtable.rx_gain_range_max;
			else if((dm_digtable.rssi_val+10-dm_digtable.backoff_val) < dm_digtable.rx_gain_range_min)
				dm_digtable.cur_ig_value = dm_digtable.rx_gain_range_min;
			else
				dm_digtable.cur_ig_value = dm_digtable.rssi_val+10-dm_digtable.backoff_val;
		}
		else		//current state is disconnected
		{
			if(dm_digtable.cur_ig_value == 0)
				dm_digtable.cur_ig_value = priv->DefaultInitialGain[0];
			else
				dm_digtable.cur_ig_value = dm_digtable.pre_ig_value;
		}
	}
	else	// disconnected -> connected or connected -> disconnected
	{
		dm_digtable.cur_ig_value = priv->DefaultInitialGain[0];
		dm_digtable.pre_ig_value = 0;
	}

	// if silent reset happened, we should rewrite the values back
	if(priv->reset_count != reset_cnt)
	{
		force_write = 1;
		reset_cnt = priv->reset_count;
	}

	if(dm_digtable.pre_ig_value != read_nic_byte(priv, rOFDM0_XAAGCCore1))
		force_write = 1;

	{
		if((dm_digtable.pre_ig_value != dm_digtable.cur_ig_value)
			|| !initialized || force_write)
		{
			initial_gain = (u8)dm_digtable.cur_ig_value;
			// Set initial gain.
			write_nic_byte(priv, rOFDM0_XAAGCCore1, initial_gain);
			write_nic_byte(priv, rOFDM0_XBAGCCore1, initial_gain);
			write_nic_byte(priv, rOFDM0_XCAGCCore1, initial_gain);
			write_nic_byte(priv, rOFDM0_XDAGCCore1, initial_gain);
			dm_digtable.pre_ig_value = dm_digtable.cur_ig_value;
			initialized = 1;
			force_write = 0;
		}
	}
}

static void dm_pd_th(struct r8192_priv *priv)
{
	static u8				initialized=0, force_write=0;
	static u32			reset_cnt = 0;

	if(dm_digtable.dig_algorithm_switch)
	{
		initialized = 0;
		reset_cnt = 0;
	}

	if(dm_digtable.pre_connect_state == dm_digtable.cur_connect_state)
	{
		if(dm_digtable.cur_connect_state == DIG_CONNECT)
		{
			if (dm_digtable.rssi_val >= dm_digtable.rssi_high_power_highthresh)
				dm_digtable.curpd_thstate = DIG_PD_AT_HIGH_POWER;
			else if ((dm_digtable.rssi_val <= dm_digtable.rssi_low_thresh))
				dm_digtable.curpd_thstate = DIG_PD_AT_LOW_POWER;
			else if ((dm_digtable.rssi_val >= dm_digtable.rssi_high_thresh) &&
					(dm_digtable.rssi_val < dm_digtable.rssi_high_power_lowthresh))
				dm_digtable.curpd_thstate = DIG_PD_AT_NORMAL_POWER;
			else
				dm_digtable.curpd_thstate = dm_digtable.prepd_thstate;
		}
		else
		{
			dm_digtable.curpd_thstate = DIG_PD_AT_LOW_POWER;
		}
	}
	else	// disconnected -> connected or connected -> disconnected
	{
		dm_digtable.curpd_thstate = DIG_PD_AT_LOW_POWER;
	}

	// if silent reset happened, we should rewrite the values back
	if(priv->reset_count != reset_cnt)
	{
		force_write = 1;
		reset_cnt = priv->reset_count;
	}

	{
		if((dm_digtable.prepd_thstate != dm_digtable.curpd_thstate) ||
			(initialized<=3) || force_write)
		{
			if(dm_digtable.curpd_thstate == DIG_PD_AT_LOW_POWER)
			{
				// Lower PD_TH for OFDM.
				if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
				{
					/* 2008/01/11 MH 40MHZ 90/92 register are not the same. */
					// 2008/02/05 MH SD3-Jerry 92U/92E PD_TH are the same.
					write_nic_byte(priv, (rOFDM0_XATxAFE+3), 0x00);
				}
				else
					write_nic_byte(priv, rOFDM0_RxDetector1, 0x42);
			}
			else if(dm_digtable.curpd_thstate == DIG_PD_AT_NORMAL_POWER)
			{
				// Higher PD_TH for OFDM.
				if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
				{
					/* 2008/01/11 MH 40MHZ 90/92 register are not the same. */
					// 2008/02/05 MH SD3-Jerry 92U/92E PD_TH are the same.
					write_nic_byte(priv, (rOFDM0_XATxAFE+3), 0x20);
				}
				else
					write_nic_byte(priv, rOFDM0_RxDetector1, 0x44);
			}
			else if(dm_digtable.curpd_thstate == DIG_PD_AT_HIGH_POWER)
			{
				// Higher PD_TH for OFDM for high power state.
				if (priv->CurrentChannelBW != HT_CHANNEL_WIDTH_20)
				{
					write_nic_byte(priv, (rOFDM0_XATxAFE+3), 0x10);
				}
				else
					write_nic_byte(priv, rOFDM0_RxDetector1, 0x43);
			}
			dm_digtable.prepd_thstate = dm_digtable.curpd_thstate;
			if(initialized <= 3)
				initialized++;
			force_write = 0;
		}
	}
}

static void dm_cs_ratio(struct r8192_priv *priv)
{
	static u8				initialized=0,force_write=0;
	static u32			reset_cnt = 0;

	if(dm_digtable.dig_algorithm_switch)
	{
		initialized = 0;
		reset_cnt = 0;
	}

	if(dm_digtable.pre_connect_state == dm_digtable.cur_connect_state)
	{
		if(dm_digtable.cur_connect_state == DIG_CONNECT)
		{
			if ((dm_digtable.rssi_val <= dm_digtable.rssi_low_thresh))
				dm_digtable.curcs_ratio_state = DIG_CS_RATIO_LOWER;
			else if ((dm_digtable.rssi_val >= dm_digtable.rssi_high_thresh) )
				dm_digtable.curcs_ratio_state = DIG_CS_RATIO_HIGHER;
			else
				dm_digtable.curcs_ratio_state = dm_digtable.precs_ratio_state;
		}
		else
		{
			dm_digtable.curcs_ratio_state = DIG_CS_RATIO_LOWER;
		}
	}
	else	// disconnected -> connected or connected -> disconnected
	{
		dm_digtable.curcs_ratio_state = DIG_CS_RATIO_LOWER;
	}

	// if silent reset happened, we should rewrite the values back
	if(priv->reset_count != reset_cnt)
	{
		force_write = 1;
		reset_cnt = priv->reset_count;
	}


	if((dm_digtable.precs_ratio_state != dm_digtable.curcs_ratio_state) ||
		!initialized || force_write)
	{
		if(dm_digtable.curcs_ratio_state == DIG_CS_RATIO_LOWER)
		{
			// Lower CS ratio for CCK.
			write_nic_byte(priv, 0xa0a, 0x08);
		}
		else if(dm_digtable.curcs_ratio_state == DIG_CS_RATIO_HIGHER)
		{
			// Higher CS ratio for CCK.
			write_nic_byte(priv, 0xa0a, 0xcd);
		}
		dm_digtable.precs_ratio_state = dm_digtable.curcs_ratio_state;
		initialized = 1;
		force_write = 0;
	}
}

void dm_init_edca_turbo(struct r8192_priv *priv)
{

	priv->bcurrent_turbo_EDCA = false;
	priv->ieee80211->bis_any_nonbepkts = false;
	priv->bis_cur_rdlstate = false;
}

static void dm_check_edca_turbo(struct r8192_priv *priv)
{
	PRT_HIGH_THROUGHPUT	pHTInfo = priv->ieee80211->pHTInfo;
	//PSTA_QOS			pStaQos = pMgntInfo->pStaQos;

	// Keep past Tx/Rx packet count for RT-to-RT EDCA turbo.
	static unsigned long			lastTxOkCnt = 0;
	static unsigned long			lastRxOkCnt = 0;
	unsigned long				curTxOkCnt = 0;
	unsigned long				curRxOkCnt = 0;

	//
	// Do not be Turbo if it's under WiFi config and Qos Enabled, because the EDCA parameters
	// should follow the settings from QAP. By Bruce, 2007-12-07.
	//
	if(priv->ieee80211->state != IEEE80211_LINKED)
		goto dm_CheckEdcaTurbo_EXIT;
	// We do not turn on EDCA turbo mode for some AP that has IOT issue
	if(priv->ieee80211->pHTInfo->IOTAction & HT_IOT_ACT_DISABLE_EDCA_TURBO)
		goto dm_CheckEdcaTurbo_EXIT;

	// Check the status for current condition.
	if(!priv->ieee80211->bis_any_nonbepkts)
	{
		curTxOkCnt = priv->stats.txbytesunicast - lastTxOkCnt;
		curRxOkCnt = priv->stats.rxbytesunicast - lastRxOkCnt;
		// For RT-AP, we needs to turn it on when Rx>Tx
		if(curRxOkCnt > 4*curTxOkCnt)
		{
			if(!priv->bis_cur_rdlstate || !priv->bcurrent_turbo_EDCA)
			{
				write_nic_dword(priv, EDCAPARA_BE, edca_setting_DL[pHTInfo->IOTPeer]);
				priv->bis_cur_rdlstate = true;
			}
		}
		else
		{
			if(priv->bis_cur_rdlstate || !priv->bcurrent_turbo_EDCA)
			{
				write_nic_dword(priv, EDCAPARA_BE, edca_setting_UL[pHTInfo->IOTPeer]);
				priv->bis_cur_rdlstate = false;
			}

		}

		priv->bcurrent_turbo_EDCA = true;
	}
	else
	{
		//
		// Turn Off EDCA turbo here.
		// Restore original EDCA according to the declaration of AP.
		//
		 if(priv->bcurrent_turbo_EDCA)
		{

			{
				u8		u1bAIFS;
				u32		u4bAcParam;
				struct ieee80211_qos_parameters *qos_parameters = &priv->ieee80211->current_network.qos_data.parameters;
				u8 mode = priv->ieee80211->mode;

			// For Each time updating EDCA parameter, reset EDCA turbo mode status.
				dm_init_edca_turbo(priv);
				u1bAIFS = qos_parameters->aifs[0] * ((mode&(IEEE_G|IEEE_N_24G)) ?9:20) + aSifsTime;
				u4bAcParam = ((((u32)(qos_parameters->tx_op_limit[0]))<< AC_PARAM_TXOP_LIMIT_OFFSET)|
					(((u32)(qos_parameters->cw_max[0]))<< AC_PARAM_ECW_MAX_OFFSET)|
					(((u32)(qos_parameters->cw_min[0]))<< AC_PARAM_ECW_MIN_OFFSET)|
					((u32)u1bAIFS << AC_PARAM_AIFS_OFFSET));
				printk("===>u4bAcParam:%x, ", u4bAcParam);
			//write_nic_dword(dev, WDCAPARA_ADD[i], u4bAcParam);
				write_nic_dword(priv, EDCAPARA_BE,  u4bAcParam);

			// Check ACM bit.
			// If it is set, immediately set ACM control bit to downgrading AC for passing WMM testplan. Annie, 2005-12-13.
				{
			// TODO:  Modified this part and try to set acm control in only 1 IO processing!!

					PACI_AIFSN	pAciAifsn = (PACI_AIFSN)&(qos_parameters->aifs[0]);
					u8		AcmCtrl = read_nic_byte(priv, AcmHwCtrl );
					if( pAciAifsn->f.ACM )
					{ // ACM bit is 1.
						AcmCtrl |= AcmHw_BeqEn;
					}
					else
					{ // ACM bit is 0.
						AcmCtrl &= (~AcmHw_BeqEn);
					}

					RT_TRACE( COMP_QOS,"SetHwReg8190pci(): [HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl ) ;
					write_nic_byte(priv, AcmHwCtrl, AcmCtrl );
				}
			}
			priv->bcurrent_turbo_EDCA = false;
		}
	}


dm_CheckEdcaTurbo_EXIT:
	// Set variables for next time.
	priv->ieee80211->bis_any_nonbepkts = false;
	lastTxOkCnt = priv->stats.txbytesunicast;
	lastRxOkCnt = priv->stats.rxbytesunicast;
}

static void dm_init_ctstoself(struct r8192_priv *priv)
{
	priv->ieee80211->bCTSToSelfEnable = TRUE;
	priv->ieee80211->CTSToSelfTH = CTSToSelfTHVal;
}

static void dm_ctstoself(struct r8192_priv *priv)
{
	PRT_HIGH_THROUGHPUT	pHTInfo = priv->ieee80211->pHTInfo;
	static unsigned long				lastTxOkCnt = 0;
	static unsigned long				lastRxOkCnt = 0;
	unsigned long						curTxOkCnt = 0;
	unsigned long						curRxOkCnt = 0;

	if(priv->ieee80211->bCTSToSelfEnable != TRUE)
	{
		pHTInfo->IOTAction &= ~HT_IOT_ACT_FORCED_CTS2SELF;
		return;
	}
	/*
	1. Uplink
	2. Linksys350/Linksys300N
	3. <50 disable, >55 enable
	*/

	if(pHTInfo->IOTPeer == HT_IOT_PEER_BROADCOM)
	{
		curTxOkCnt = priv->stats.txbytesunicast - lastTxOkCnt;
		curRxOkCnt = priv->stats.rxbytesunicast - lastRxOkCnt;
		if(curRxOkCnt > 4*curTxOkCnt)	//downlink, disable CTS to self
		{
			pHTInfo->IOTAction &= ~HT_IOT_ACT_FORCED_CTS2SELF;
		}
		else	//uplink
		{
			pHTInfo->IOTAction |= HT_IOT_ACT_FORCED_CTS2SELF;
		}

		lastTxOkCnt = priv->stats.txbytesunicast;
		lastRxOkCnt = priv->stats.rxbytesunicast;
	}
}



/* Copy 8187B template for 9xseries */
static void dm_check_rfctrl_gpio(struct r8192_priv *priv)
{

	// Walk around for DTM test, we will not enable HW - radio on/off because r/w
	// page 1 register before Lextra bus is enabled cause system fails when resuming
	// from S4. 20080218, Emily

	// Stop to execute workitem to prevent S3/S4 bug.
	queue_delayed_work(priv->priv_wq,&priv->gpio_change_rf_wq,0);
}

/* PCI will not support workitem call back HW radio on-off control. */
void dm_gpio_change_rf_callback(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work,struct delayed_work,work);
	struct r8192_priv *priv = container_of(dwork,struct r8192_priv,gpio_change_rf_wq);
	u8 tmp1byte;
	RT_RF_POWER_STATE	eRfPowerStateToSet;
	bool bActuallySet = false;

	if (!priv->up) {
		RT_TRACE((COMP_INIT | COMP_POWER | COMP_RF),"dm_gpio_change_rf_callback(): Callback function breaks out!!\n");
	} else {
		// 0x108 GPIO input register is read only
		//set 0x108 B1= 1: RF-ON; 0: RF-OFF.
		tmp1byte = read_nic_byte(priv, GPI);

		eRfPowerStateToSet = (tmp1byte&BIT1) ?  eRfOn : eRfOff;

		if (priv->bHwRadioOff && (eRfPowerStateToSet == eRfOn)) {
			RT_TRACE(COMP_RF, "gpiochangeRF  - HW Radio ON\n");

			priv->bHwRadioOff = false;
			bActuallySet = true;
		} else if ((!priv->bHwRadioOff) && (eRfPowerStateToSet == eRfOff)) {
			RT_TRACE(COMP_RF, "gpiochangeRF  - HW Radio OFF\n");
			priv->bHwRadioOff = true;
			bActuallySet = true;
		}

		if (bActuallySet) {
			priv->bHwRfOffAction = 1;
			MgntActSet_RF_State(priv, eRfPowerStateToSet, RF_CHANGE_BY_HW);
			//DrvIFIndicateCurrentPhyStatus(pAdapter);
		} else {
			msleep(2000);
		}
	}
}

/* Check if Current RF RX path is enabled */
void dm_rf_pathcheck_workitemcallback(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work,struct delayed_work,work);
	struct r8192_priv *priv = container_of(dwork,struct r8192_priv,rfpath_check_wq);
	u8 rfpath = 0, i;


	/* 2008/01/30 MH After discussing with SD3 Jerry, 0xc04/0xd04 register will
	   always be the same. We only read 0xc04 now. */
	rfpath = read_nic_byte(priv, 0xc04);

	// Check Bit 0-3, it means if RF A-D is enabled.
	for (i = 0; i < RF90_PATH_MAX; i++)
	{
		if (rfpath & (0x01<<i))
			priv->brfpath_rxenable[i] = 1;
		else
			priv->brfpath_rxenable[i] = 0;
	}
	if(!DM_RxPathSelTable.Enable)
		return;

	dm_rxpath_sel_byrssi(priv);
}

static void dm_init_rxpath_selection(struct r8192_priv *priv)
{
	u8 i;

	DM_RxPathSelTable.Enable = 1;	//default enabled
	DM_RxPathSelTable.SS_TH_low = RxPathSelection_SS_TH_low;
	DM_RxPathSelTable.diff_TH = RxPathSelection_diff_TH;
	if(priv->CustomerID == RT_CID_819x_Netcore)
		DM_RxPathSelTable.cck_method = CCK_Rx_Version_2;
	else
		DM_RxPathSelTable.cck_method = CCK_Rx_Version_1;
	DM_RxPathSelTable.DbgMode = DM_DBG_OFF;
	DM_RxPathSelTable.disabledRF = 0;
	for(i=0; i<4; i++)
	{
		DM_RxPathSelTable.rf_rssi[i] = 50;
		DM_RxPathSelTable.cck_pwdb_sta[i] = -64;
		DM_RxPathSelTable.rf_enable_rssi_th[i] = 100;
	}
}

static void dm_rxpath_sel_byrssi(struct r8192_priv *priv)
{
	u8				i, max_rssi_index=0, min_rssi_index=0, sec_rssi_index=0, rf_num=0;
	u8				tmp_max_rssi=0, tmp_min_rssi=0, tmp_sec_rssi=0;
	u8				cck_default_Rx=0x2;	//RF-C
	u8				cck_optional_Rx=0x3;//RF-D
	long				tmp_cck_max_pwdb=0, tmp_cck_min_pwdb=0, tmp_cck_sec_pwdb=0;
	u8				cck_rx_ver2_max_index=0, cck_rx_ver2_min_index=0, cck_rx_ver2_sec_index=0;
	u8				cur_rf_rssi;
	long				cur_cck_pwdb;
	static u8			disabled_rf_cnt=0, cck_Rx_Path_initialized=0;
	u8				update_cck_rx_path;

	if(priv->rf_type != RF_2T4R)
		return;

	if(!cck_Rx_Path_initialized)
	{
		DM_RxPathSelTable.cck_Rx_path = (read_nic_byte(priv, 0xa07)&0xf);
		cck_Rx_Path_initialized = 1;
	}

	DM_RxPathSelTable.disabledRF = 0xf;
	DM_RxPathSelTable.disabledRF &=~ (read_nic_byte(priv, 0xc04));

	if(priv->ieee80211->mode == WIRELESS_MODE_B)
	{
		DM_RxPathSelTable.cck_method = CCK_Rx_Version_2;	//pure B mode, fixed cck version2
	}

	//decide max/sec/min rssi index
	for (i=0; i<RF90_PATH_MAX; i++)
	{
		if(!DM_RxPathSelTable.DbgMode)
			DM_RxPathSelTable.rf_rssi[i] = priv->stats.rx_rssi_percentage[i];

		if(priv->brfpath_rxenable[i])
		{
			rf_num++;
			cur_rf_rssi = DM_RxPathSelTable.rf_rssi[i];

			if(rf_num == 1)	// find first enabled rf path and the rssi values
			{	//initialize, set all rssi index to the same one
				max_rssi_index = min_rssi_index = sec_rssi_index = i;
				tmp_max_rssi = tmp_min_rssi = tmp_sec_rssi = cur_rf_rssi;
			}
			else if(rf_num == 2)
			{	// we pick up the max index first, and let sec and min to be the same one
				if(cur_rf_rssi >= tmp_max_rssi)
				{
					tmp_max_rssi = cur_rf_rssi;
					max_rssi_index = i;
				}
				else
				{
					tmp_sec_rssi = tmp_min_rssi = cur_rf_rssi;
					sec_rssi_index = min_rssi_index = i;
				}
			}
			else
			{
				if(cur_rf_rssi > tmp_max_rssi)
				{
					tmp_sec_rssi = tmp_max_rssi;
					sec_rssi_index = max_rssi_index;
					tmp_max_rssi = cur_rf_rssi;
					max_rssi_index = i;
				}
				else if(cur_rf_rssi == tmp_max_rssi)
				{	// let sec and min point to the different index
					tmp_sec_rssi = cur_rf_rssi;
					sec_rssi_index = i;
				}
				else if((cur_rf_rssi < tmp_max_rssi) &&(cur_rf_rssi > tmp_sec_rssi))
				{
					tmp_sec_rssi = cur_rf_rssi;
					sec_rssi_index = i;
				}
				else if(cur_rf_rssi == tmp_sec_rssi)
				{
					if(tmp_sec_rssi == tmp_min_rssi)
					{	// let sec and min point to the different index
						tmp_sec_rssi = cur_rf_rssi;
						sec_rssi_index = i;
					}
					else
					{
						// This case we don't need to set any index
					}
				}
				else if((cur_rf_rssi < tmp_sec_rssi) && (cur_rf_rssi > tmp_min_rssi))
				{
					// This case we don't need to set any index
				}
				else if(cur_rf_rssi == tmp_min_rssi)
				{
					if(tmp_sec_rssi == tmp_min_rssi)
					{	// let sec and min point to the different index
						tmp_min_rssi = cur_rf_rssi;
						min_rssi_index = i;
					}
					else
					{
						// This case we don't need to set any index
					}
				}
				else if(cur_rf_rssi < tmp_min_rssi)
				{
					tmp_min_rssi = cur_rf_rssi;
					min_rssi_index = i;
				}
			}
		}
	}

	rf_num = 0;
	// decide max/sec/min cck pwdb index
	if(DM_RxPathSelTable.cck_method == CCK_Rx_Version_2)
	{
		for (i=0; i<RF90_PATH_MAX; i++)
		{
			if(priv->brfpath_rxenable[i])
			{
				rf_num++;
				cur_cck_pwdb =  DM_RxPathSelTable.cck_pwdb_sta[i];

				if(rf_num == 1)	// find first enabled rf path and the rssi values
				{	//initialize, set all rssi index to the same one
					cck_rx_ver2_max_index = cck_rx_ver2_min_index = cck_rx_ver2_sec_index = i;
					tmp_cck_max_pwdb = tmp_cck_min_pwdb = tmp_cck_sec_pwdb = cur_cck_pwdb;
				}
				else if(rf_num == 2)
				{	// we pick up the max index first, and let sec and min to be the same one
					if(cur_cck_pwdb >= tmp_cck_max_pwdb)
					{
						tmp_cck_max_pwdb = cur_cck_pwdb;
						cck_rx_ver2_max_index = i;
					}
					else
					{
						tmp_cck_sec_pwdb = tmp_cck_min_pwdb = cur_cck_pwdb;
						cck_rx_ver2_sec_index = cck_rx_ver2_min_index = i;
					}
				}
				else
				{
					if(cur_cck_pwdb > tmp_cck_max_pwdb)
					{
						tmp_cck_sec_pwdb = tmp_cck_max_pwdb;
						cck_rx_ver2_sec_index = cck_rx_ver2_max_index;
						tmp_cck_max_pwdb = cur_cck_pwdb;
						cck_rx_ver2_max_index = i;
					}
					else if(cur_cck_pwdb == tmp_cck_max_pwdb)
					{	// let sec and min point to the different index
						tmp_cck_sec_pwdb = cur_cck_pwdb;
						cck_rx_ver2_sec_index = i;
					}
					else if((cur_cck_pwdb < tmp_cck_max_pwdb) &&(cur_cck_pwdb > tmp_cck_sec_pwdb))
					{
						tmp_cck_sec_pwdb = cur_cck_pwdb;
						cck_rx_ver2_sec_index = i;
					}
					else if(cur_cck_pwdb == tmp_cck_sec_pwdb)
					{
						if(tmp_cck_sec_pwdb == tmp_cck_min_pwdb)
						{	// let sec and min point to the different index
							tmp_cck_sec_pwdb = cur_cck_pwdb;
							cck_rx_ver2_sec_index = i;
						}
						else
						{
							// This case we don't need to set any index
						}
					}
					else if((cur_cck_pwdb < tmp_cck_sec_pwdb) && (cur_cck_pwdb > tmp_cck_min_pwdb))
					{
						// This case we don't need to set any index
					}
					else if(cur_cck_pwdb == tmp_cck_min_pwdb)
					{
						if(tmp_cck_sec_pwdb == tmp_cck_min_pwdb)
						{	// let sec and min point to the different index
							tmp_cck_min_pwdb = cur_cck_pwdb;
							cck_rx_ver2_min_index = i;
						}
						else
						{
							// This case we don't need to set any index
						}
					}
					else if(cur_cck_pwdb < tmp_cck_min_pwdb)
					{
						tmp_cck_min_pwdb = cur_cck_pwdb;
						cck_rx_ver2_min_index = i;
					}
				}

			}
		}
	}


	// Set CCK Rx path
	// reg0xA07[3:2]=cck default rx path, reg0xa07[1:0]=cck optional rx path.
	update_cck_rx_path = 0;
	if(DM_RxPathSelTable.cck_method == CCK_Rx_Version_2)
	{
		cck_default_Rx = cck_rx_ver2_max_index;
		cck_optional_Rx = cck_rx_ver2_sec_index;
		if(tmp_cck_max_pwdb != -64)
			update_cck_rx_path = 1;
	}

	if(tmp_min_rssi < DM_RxPathSelTable.SS_TH_low && disabled_rf_cnt < 2)
	{
		if((tmp_max_rssi - tmp_min_rssi) >= DM_RxPathSelTable.diff_TH)
		{
			//record the enabled rssi threshold
			DM_RxPathSelTable.rf_enable_rssi_th[min_rssi_index] = tmp_max_rssi+5;
			//disable the BB Rx path, OFDM
			rtl8192_setBBreg(priv, rOFDM0_TRxPathEnable, 0x1<<min_rssi_index, 0x0);	// 0xc04[3:0]
			rtl8192_setBBreg(priv, rOFDM1_TRxPathEnable, 0x1<<min_rssi_index, 0x0);	// 0xd04[3:0]
			disabled_rf_cnt++;
		}
		if(DM_RxPathSelTable.cck_method == CCK_Rx_Version_1)
		{
			cck_default_Rx = max_rssi_index;
			cck_optional_Rx = sec_rssi_index;
			if(tmp_max_rssi)
				update_cck_rx_path = 1;
		}
	}

	if(update_cck_rx_path)
	{
		DM_RxPathSelTable.cck_Rx_path = (cck_default_Rx<<2)|(cck_optional_Rx);
		rtl8192_setBBreg(priv, rCCK0_AFESetting, 0x0f000000, DM_RxPathSelTable.cck_Rx_path);
	}

	if(DM_RxPathSelTable.disabledRF)
	{
		for(i=0; i<4; i++)
		{
			if((DM_RxPathSelTable.disabledRF>>i) & 0x1)	//disabled rf
			{
				if(tmp_max_rssi >= DM_RxPathSelTable.rf_enable_rssi_th[i])
				{
					//enable the BB Rx path
					rtl8192_setBBreg(priv, rOFDM0_TRxPathEnable, 0x1<<i, 0x1);	// 0xc04[3:0]
					rtl8192_setBBreg(priv, rOFDM1_TRxPathEnable, 0x1<<i, 0x1);	// 0xd04[3:0]
					DM_RxPathSelTable.rf_enable_rssi_th[i] = 100;
					disabled_rf_cnt--;
				}
			}
		}
	}
}

/*
 * Call a workitem to check current RXRF path and Rx Path selection by RSSI.
 */
static void dm_check_rx_path_selection(struct r8192_priv *priv)
{
	queue_delayed_work(priv->priv_wq,&priv->rfpath_check_wq,0);
}

static void dm_init_fsync(struct r8192_priv *priv)
{
	priv->ieee80211->fsync_time_interval = 500;
	priv->ieee80211->fsync_rate_bitmap = 0x0f000800;
	priv->ieee80211->fsync_rssi_threshold = 30;
	priv->ieee80211->bfsync_enable = false;
	priv->ieee80211->fsync_multiple_timeinterval = 3;
	priv->ieee80211->fsync_firstdiff_ratethreshold= 100;
	priv->ieee80211->fsync_seconddiff_ratethreshold= 200;
	priv->ieee80211->fsync_state = Default_Fsync;
	priv->framesyncMonitor = 1;	// current default 0xc38 monitor on

	init_timer(&priv->fsync_timer);
	priv->fsync_timer.data = (unsigned long)priv;
	priv->fsync_timer.function = dm_fsync_timer_callback;
}


static void dm_deInit_fsync(struct r8192_priv *priv)
{
	del_timer_sync(&priv->fsync_timer);
}

static void dm_fsync_timer_callback(unsigned long data)
{
	struct r8192_priv *priv = (struct r8192_priv *)data;
	u32 rate_index, rate_count = 0, rate_count_diff=0;
	bool		bSwitchFromCountDiff = false;
	bool		bDoubleTimeInterval = false;

	if(	priv->ieee80211->state == IEEE80211_LINKED &&
		priv->ieee80211->bfsync_enable &&
		(priv->ieee80211->pHTInfo->IOTAction & HT_IOT_ACT_CDD_FSYNC))
	{
		 // Count rate 54, MCS [7], [12, 13, 14, 15]
		u32 rate_bitmap;
		for(rate_index = 0; rate_index <= 27; rate_index++)
		{
			rate_bitmap  = 1 << rate_index;
			if(priv->ieee80211->fsync_rate_bitmap &  rate_bitmap)
		 		rate_count+= priv->stats.received_rate_histogram[1][rate_index];
		}

		if(rate_count < priv->rate_record)
			rate_count_diff = 0xffffffff - rate_count + priv->rate_record;
		else
			rate_count_diff = rate_count - priv->rate_record;
		if(rate_count_diff < priv->rateCountDiffRecord)
		{

			u32 DiffNum = priv->rateCountDiffRecord - rate_count_diff;
			// Contiune count
			if(DiffNum >= priv->ieee80211->fsync_seconddiff_ratethreshold)
				priv->ContiuneDiffCount++;
			else
				priv->ContiuneDiffCount = 0;

			// Contiune count over
			if(priv->ContiuneDiffCount >=2)
			{
				bSwitchFromCountDiff = true;
				priv->ContiuneDiffCount = 0;
			}
		}
		else
		{
			// Stop contiune count
			priv->ContiuneDiffCount = 0;
		}

		//If Count diff <= FsyncRateCountThreshold
		if(rate_count_diff <= priv->ieee80211->fsync_firstdiff_ratethreshold)
		{
			bSwitchFromCountDiff = true;
			priv->ContiuneDiffCount = 0;
		}
		priv->rate_record = rate_count;
		priv->rateCountDiffRecord = rate_count_diff;
		RT_TRACE(COMP_HALDM, "rateRecord %d rateCount %d, rateCountdiff %d bSwitchFsync %d\n", priv->rate_record, rate_count, rate_count_diff , priv->bswitch_fsync);
		// if we never receive those mcs rate and rssi > 30 % then switch fsyn
		if(priv->undecorated_smoothed_pwdb > priv->ieee80211->fsync_rssi_threshold && bSwitchFromCountDiff)
		{
			bDoubleTimeInterval = true;
			priv->bswitch_fsync = !priv->bswitch_fsync;
			if(priv->bswitch_fsync)
			{
				write_nic_byte(priv,0xC36, 0x1c);
				write_nic_byte(priv, 0xC3e, 0x90);
			}
			else
			{
				write_nic_byte(priv, 0xC36, 0x5c);
				write_nic_byte(priv, 0xC3e, 0x96);
			}
		}
		else if(priv->undecorated_smoothed_pwdb <= priv->ieee80211->fsync_rssi_threshold)
		{
			if(priv->bswitch_fsync)
			{
				priv->bswitch_fsync  = false;
				write_nic_byte(priv, 0xC36, 0x5c);
				write_nic_byte(priv, 0xC3e, 0x96);
			}
		}
		if(bDoubleTimeInterval){
			if(timer_pending(&priv->fsync_timer))
				del_timer_sync(&priv->fsync_timer);
			priv->fsync_timer.expires = jiffies + MSECS(priv->ieee80211->fsync_time_interval*priv->ieee80211->fsync_multiple_timeinterval);
			add_timer(&priv->fsync_timer);
		}
		else{
			if(timer_pending(&priv->fsync_timer))
				del_timer_sync(&priv->fsync_timer);
			priv->fsync_timer.expires = jiffies + MSECS(priv->ieee80211->fsync_time_interval);
			add_timer(&priv->fsync_timer);
		}
	}
	else
	{
		// Let Register return to default value;
		if(priv->bswitch_fsync)
		{
			priv->bswitch_fsync  = false;
			write_nic_byte(priv, 0xC36, 0x5c);
			write_nic_byte(priv, 0xC3e, 0x96);
		}
		priv->ContiuneDiffCount = 0;
		write_nic_dword(priv, rOFDM0_RxDetector2, 0x465c52cd);
	}
	RT_TRACE(COMP_HALDM, "ContiuneDiffCount %d\n", priv->ContiuneDiffCount);
	RT_TRACE(COMP_HALDM, "rateRecord %d rateCount %d, rateCountdiff %d bSwitchFsync %d\n", priv->rate_record, rate_count, rate_count_diff , priv->bswitch_fsync);
}

static void dm_StartHWFsync(struct r8192_priv *priv)
{
	RT_TRACE(COMP_HALDM, "%s\n", __FUNCTION__);
	write_nic_dword(priv, rOFDM0_RxDetector2, 0x465c12cf);
	write_nic_byte(priv, 0xc3b, 0x41);
}

static void dm_EndSWFsync(struct r8192_priv *priv)
{
	RT_TRACE(COMP_HALDM, "%s\n", __FUNCTION__);
	del_timer_sync(&(priv->fsync_timer));

	// Let Register return to default value;
	if(priv->bswitch_fsync)
	{
		priv->bswitch_fsync  = false;

		write_nic_byte(priv, 0xC36, 0x40);

		write_nic_byte(priv, 0xC3e, 0x96);
	}

	priv->ContiuneDiffCount = 0;

	write_nic_dword(priv, rOFDM0_RxDetector2, 0x465c52cd);
}

static void dm_StartSWFsync(struct r8192_priv *priv)
{
	u32 			rateIndex;
	u32 			rateBitmap;

	RT_TRACE(COMP_HALDM,"%s\n", __FUNCTION__);
	// Initial rate record to zero, start to record.
	priv->rate_record = 0;
	// Initial contiune diff count to zero, start to record.
	priv->ContiuneDiffCount = 0;
	priv->rateCountDiffRecord = 0;
	priv->bswitch_fsync  = false;

	if(priv->ieee80211->mode == WIRELESS_MODE_N_24G)
	{
		priv->ieee80211->fsync_firstdiff_ratethreshold= 600;
		priv->ieee80211->fsync_seconddiff_ratethreshold = 0xffff;
	}
	else
	{
		priv->ieee80211->fsync_firstdiff_ratethreshold= 200;
		priv->ieee80211->fsync_seconddiff_ratethreshold = 200;
	}
	for(rateIndex = 0; rateIndex <= 27; rateIndex++)
	{
		rateBitmap  = 1 << rateIndex;
		if(priv->ieee80211->fsync_rate_bitmap &  rateBitmap)
			priv->rate_record += priv->stats.received_rate_histogram[1][rateIndex];
	}
	if(timer_pending(&priv->fsync_timer))
		del_timer_sync(&priv->fsync_timer);
	priv->fsync_timer.expires = jiffies + MSECS(priv->ieee80211->fsync_time_interval);
	add_timer(&priv->fsync_timer);

	write_nic_dword(priv, rOFDM0_RxDetector2, 0x465c12cd);
}

static void dm_EndHWFsync(struct r8192_priv *priv)
{
	RT_TRACE(COMP_HALDM,"%s\n", __FUNCTION__);
	write_nic_dword(priv, rOFDM0_RxDetector2, 0x465c52cd);
	write_nic_byte(priv, 0xc3b, 0x49);
}

static void dm_check_fsync(struct r8192_priv *priv)
{
#define	RegC38_Default				0
#define	RegC38_NonFsync_Other_AP	1
#define	RegC38_Fsync_AP_BCM		2
	//u32 			framesyncC34;
	static u8		reg_c38_State=RegC38_Default;
	static u32	reset_cnt=0;

	RT_TRACE(COMP_HALDM, "RSSI %d TimeInterval %d MultipleTimeInterval %d\n", priv->ieee80211->fsync_rssi_threshold, priv->ieee80211->fsync_time_interval, priv->ieee80211->fsync_multiple_timeinterval);
	RT_TRACE(COMP_HALDM, "RateBitmap 0x%x FirstDiffRateThreshold %d SecondDiffRateThreshold %d\n", priv->ieee80211->fsync_rate_bitmap, priv->ieee80211->fsync_firstdiff_ratethreshold, priv->ieee80211->fsync_seconddiff_ratethreshold);

	if(	priv->ieee80211->state == IEEE80211_LINKED &&
		(priv->ieee80211->pHTInfo->IOTAction & HT_IOT_ACT_CDD_FSYNC))
	{
		if(priv->ieee80211->bfsync_enable == 0)
		{
			switch(priv->ieee80211->fsync_state)
			{
				case Default_Fsync:
					dm_StartHWFsync(priv);
					priv->ieee80211->fsync_state = HW_Fsync;
					break;
				case SW_Fsync:
					dm_EndSWFsync(priv);
					dm_StartHWFsync(priv);
					priv->ieee80211->fsync_state = HW_Fsync;
					break;
				case HW_Fsync:
				default:
					break;
			}
		}
		else
		{
			switch(priv->ieee80211->fsync_state)
			{
				case Default_Fsync:
					dm_StartSWFsync(priv);
					priv->ieee80211->fsync_state = SW_Fsync;
					break;
				case HW_Fsync:
					dm_EndHWFsync(priv);
					dm_StartSWFsync(priv);
					priv->ieee80211->fsync_state = SW_Fsync;
					break;
				case SW_Fsync:
				default:
					break;

			}
		}
		if(priv->framesyncMonitor)
		{
			if(reg_c38_State != RegC38_Fsync_AP_BCM)
			{	//For broadcom AP we write different default value
				write_nic_byte(priv, rOFDM0_RxDetector3, 0x95);

				reg_c38_State = RegC38_Fsync_AP_BCM;
			}
		}
	}
	else
	{
		switch(priv->ieee80211->fsync_state)
		{
			case HW_Fsync:
				dm_EndHWFsync(priv);
				priv->ieee80211->fsync_state = Default_Fsync;
				break;
			case SW_Fsync:
				dm_EndSWFsync(priv);
				priv->ieee80211->fsync_state = Default_Fsync;
				break;
			case Default_Fsync:
			default:
				break;
		}

		if(priv->framesyncMonitor)
		{
			if(priv->ieee80211->state == IEEE80211_LINKED)
			{
				if(priv->undecorated_smoothed_pwdb <= RegC38_TH)
				{
					if(reg_c38_State != RegC38_NonFsync_Other_AP)
					{
						write_nic_byte(priv, rOFDM0_RxDetector3, 0x90);

						reg_c38_State = RegC38_NonFsync_Other_AP;
					}
				}
				else if(priv->undecorated_smoothed_pwdb >= (RegC38_TH+5))
				{
					if(reg_c38_State)
					{
						write_nic_byte(priv, rOFDM0_RxDetector3, priv->framesync);
						reg_c38_State = RegC38_Default;
					}
				}
			}
			else
			{
				if(reg_c38_State)
				{
					write_nic_byte(priv, rOFDM0_RxDetector3, priv->framesync);
					reg_c38_State = RegC38_Default;
				}
			}
		}
	}
	if(priv->framesyncMonitor)
	{
		if(priv->reset_count != reset_cnt)
		{	//After silent reset, the reg_c38_State will be returned to default value
			write_nic_byte(priv, rOFDM0_RxDetector3, priv->framesync);
			reg_c38_State = RegC38_Default;
			reset_cnt = priv->reset_count;
		}
	}
	else
	{
		if(reg_c38_State)
		{
			write_nic_byte(priv, rOFDM0_RxDetector3, priv->framesync);
			reg_c38_State = RegC38_Default;
		}
	}
}

/*
 * Detect Signal strength to control TX Registry
 * Tx Power Control For Near/Far Range
 */
static void dm_init_dynamic_txpower(struct r8192_priv *priv)
{
	//Initial TX Power Control for near/far range , add by amy 2008/05/15, porting from windows code.
	priv->ieee80211->bdynamic_txpower_enable = true;    //Default to enable Tx Power Control
	priv->bLastDTPFlag_High = false;
	priv->bLastDTPFlag_Low = false;
	priv->bDynamicTxHighPower = false;
	priv->bDynamicTxLowPower = false;
}

static void dm_dynamic_txpower(struct r8192_priv *priv)
{
	unsigned int txhipower_threshhold=0;
        unsigned int txlowpower_threshold=0;
	if(priv->ieee80211->bdynamic_txpower_enable != true)
	{
		priv->bDynamicTxHighPower = false;
		priv->bDynamicTxLowPower = false;
		return;
	}
        if((priv->ieee80211->current_network.atheros_cap_exist ) && (priv->ieee80211->mode == IEEE_G)){
		txhipower_threshhold = TX_POWER_ATHEROAP_THRESH_HIGH;
		txlowpower_threshold = TX_POWER_ATHEROAP_THRESH_LOW;
	}
	else
	{
		txhipower_threshhold = TX_POWER_NEAR_FIELD_THRESH_HIGH;
		txlowpower_threshold = TX_POWER_NEAR_FIELD_THRESH_LOW;
	}

	RT_TRACE(COMP_TXAGC, "priv->undecorated_smoothed_pwdb = %ld\n" , priv->undecorated_smoothed_pwdb);

	if(priv->ieee80211->state == IEEE80211_LINKED)
	{
		if(priv->undecorated_smoothed_pwdb >= txhipower_threshhold)
		{
			priv->bDynamicTxHighPower = true;
			priv->bDynamicTxLowPower = false;
		}
		else
		{
			// high power state check
			if(priv->undecorated_smoothed_pwdb < txlowpower_threshold && priv->bDynamicTxHighPower == true)
			{
				priv->bDynamicTxHighPower = false;
			}
			// low power state check
			if(priv->undecorated_smoothed_pwdb < 35)
			{
				priv->bDynamicTxLowPower = true;
			}
			else if(priv->undecorated_smoothed_pwdb >= 40)
			{
				priv->bDynamicTxLowPower = false;
			}
		}
	}
	else
	{
		//pHalData->bTXPowerCtrlforNearFarRange = !pHalData->bTXPowerCtrlforNearFarRange;
		priv->bDynamicTxHighPower = false;
		priv->bDynamicTxLowPower = false;
	}

	if( (priv->bDynamicTxHighPower != priv->bLastDTPFlag_High ) ||
		(priv->bDynamicTxLowPower != priv->bLastDTPFlag_Low ) )
	{
		RT_TRACE(COMP_TXAGC, "SetTxPowerLevel8190() channel = %d\n", priv->ieee80211->current_network.channel);


		rtl8192_phy_setTxPower(priv, priv->ieee80211->current_network.channel);

	}
	priv->bLastDTPFlag_High = priv->bDynamicTxHighPower;
	priv->bLastDTPFlag_Low = priv->bDynamicTxLowPower;

}

//added by vivi, for read tx rate and retrycount
static void dm_check_txrateandretrycount(struct r8192_priv *priv)
{
	struct ieee80211_device* ieee = priv->ieee80211;

	//for initial tx rate
	ieee->softmac_stats.last_packet_rate = read_nic_byte(priv ,Initial_Tx_Rate_Reg);
	//for tx tx retry count
	ieee->softmac_stats.txretrycount = read_nic_dword(priv, Tx_Retry_Count_Reg);
}

static void dm_send_rssi_tofw(struct r8192_priv *priv)
{
	// If we test chariot, we should stop the TX command ?
	// Because 92E will always silent reset when we send tx command. We use register
	// 0x1e0(byte) to botify driver.
	write_nic_byte(priv, DRIVER_RSSI, (u8)priv->undecorated_smoothed_pwdb);
	return;
}

