//
// ADAPTER.H -
// Windows NDIS global variable 'adapter' typedef
//
#define MAX_ANSI_STRING		40

struct wb35_adapter {
	u32 adapterIndex; // 20060703.4 Add for using padapterContext global adapter point

	WB_LOCALDESCRIPT	sLocalPara;		// Myself connected parameters
	PWB_BSSDESCRIPTION	asBSSDescriptElement;

	MLME_FRAME		sMlmeFrame;		// connect to peerSTA parameters

	MTO_PARAMETERS		sMtoPara; // MTO_struct ...
	hw_data_t			sHwData; //For HAL
	MDS					Mds;

	WBLINUX		WbLinux;
        struct iw_statistics iw_stats;

	u8	LinkName[MAX_ANSI_STRING];
};
