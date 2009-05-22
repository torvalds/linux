/******************************************************************************
 *
 *     (c) Copyright  2008, RealTEK Technologies Inc. All Rights Reserved.
 *
 * Module:	HalRf.h	( Header File)
 *
 * Note:	Collect every HAL RF type exter API or constant.
 *
 * Function:
 *
 * Export:
 *
 * Abbrev:
 *
 * History:
 * Data			Who		Remark
 *
 * 09/25/2008	MHC		Create initial version.
 *
 *
******************************************************************************/
/* Check to see if the file has been included already.  */


/*--------------------------Define Parameters-------------------------------*/

//
// For RF 6052 Series
//
#define	RF6052_MAX_TX_PWR	0x3F
#define	RF6052_MAX_REG		0x3F
#define	RF6052_MAX_PATH		4
/*--------------------------Define Parameters-------------------------------*/


/*------------------------------Define structure----------------------------*/

/*------------------------------Define structure----------------------------*/


/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/

/*------------------------Export Marco Definition---------------------------*/

/*------------------------Export Marco Definition---------------------------*/


/*--------------------------Exported Function prototype---------------------*/
//======================================================
#if 1
// Function prototypes for HalPhy8225.c
//1======================================================
extern void PHY_SetRF0222DBandwidth(struct net_device* dev , HT_CHANNEL_WIDTH Bandwidth);	//20M or 40M;
extern void PHY_SetRF8225Bandwidth(	struct net_device* dev ,	HT_CHANNEL_WIDTH Bandwidth);
extern bool PHY_RF8225_Config(struct net_device* dev );
extern void phy_RF8225_Config_HardCode(struct net_device*	dev);
extern bool phy_RF8225_Config_ParaFile(struct net_device* dev);
extern void PHY_SetRF8225CckTxPower(struct net_device* dev ,u8 powerlevel);
extern void PHY_SetRF8225OfdmTxPower(struct net_device* dev ,u8        powerlevel);
extern void PHY_SetRF0222DOfdmTxPower(struct net_device* dev ,u8 powerlevel);
extern void PHY_SetRF0222DCckTxPower(struct net_device* dev ,u8        powerlevel);

//1======================================================
// Function prototypes for HalPhy8256.c
//1======================================================
extern void PHY_SetRF8256Bandwidth(struct net_device* dev , HT_CHANNEL_WIDTH Bandwidth);
extern void PHY_RF8256_Config(struct net_device* dev);
extern void phy_RF8256_Config_ParaFile(struct net_device* dev);
extern void PHY_SetRF8256CCKTxPower(struct net_device*	dev, u8	powerlevel);
extern void PHY_SetRF8256OFDMTxPower(struct net_device* dev, u8 powerlevel);
#endif

//
// RF RL6052 Series API
//
extern	void		RF_ChangeTxPath(struct net_device  * dev, u16 DataRate);
extern	void		PHY_RF6052SetBandwidth(struct net_device  * dev,HT_CHANNEL_WIDTH	Bandwidth);
extern	void		PHY_RF6052SetCckTxPower(struct net_device  * dev, u8	powerlevel);
extern	void		PHY_RF6052SetOFDMTxPower(struct net_device  * dev, u8 powerlevel);
extern	RT_STATUS	PHY_RF6052_Config(struct net_device  * dev);
extern void PHY_RFShadowRefresh( struct net_device  		* dev);
extern void PHY_RFShadowWrite( struct net_device* dev, u32 eRFPath, u32 Offset, u32 Data);
#if 0
//
// RF Shadow operation relative API
//
extern	u32
PHY_RFShadowRead(
	struct net_device  		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset);
extern void
PHY_RFShadowCompare(
	struct net_device  		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset);
extern void
PHY_RFShadowRecorver(
	struct net_device  		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset);
extern void
PHY_RFShadowCompareAll(
	struct net_device  		* dev);
extern void
PHY_RFShadowRecorverAll(
	struct net_device  		* dev);
extern void
PHY_RFShadowCompareFlagSet(
	struct net_device  		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset,
	u8					Type);
extern void
PHY_RFShadowRecorverFlagSet(
	struct net_device  		* dev,
	RF90_RADIO_PATH_E	eRFPath,
	u32					Offset,
	u8					Type);
extern void
PHY_RFShadowCompareFlagSetAll(
	struct net_device  		* dev);
extern void
PHY_RFShadowRecorverFlagSetAll(
	struct net_device  		* dev);
extern void
PHY_RFShadowRefresh(
	struct net_device  		* dev);
#endif
/*--------------------------Exported Function prototype---------------------*/


/* End of HalRf.h */
