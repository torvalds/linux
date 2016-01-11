#ifndef DDK750_DVI_H__
#define DDK750_DVI_H__

/* dvi chip stuffs structros */

typedef long (*PFN_DVICTRL_INIT)(
	unsigned char edgeSelect,
	unsigned char busSelect,
	unsigned char dualEdgeClkSelect,
	unsigned char hsyncEnable,
	unsigned char vsyncEnable,
	unsigned char deskewEnable,
	unsigned char deskewSetting,
	unsigned char continuousSyncEnable,
	unsigned char pllFilterEnable,
	unsigned char pllFilterValue);

typedef void (*PFN_DVICTRL_RESETCHIP)(void);
typedef char* (*PFN_DVICTRL_GETCHIPSTRING)(void);
typedef unsigned short (*PFN_DVICTRL_GETVENDORID)(void);
typedef unsigned short (*PFN_DVICTRL_GETDEVICEID)(void);
typedef void (*PFN_DVICTRL_SETPOWER)(unsigned char powerUp);
typedef void (*PFN_DVICTRL_HOTPLUGDETECTION)(unsigned char enableHotPlug);
typedef unsigned char (*PFN_DVICTRL_ISCONNECTED)(void);
typedef unsigned char (*PFN_DVICTRL_CHECKINTERRUPT)(void);
typedef void (*PFN_DVICTRL_CLEARINTERRUPT)(void);

/* Structure to hold all the function pointer to the DVI Controller. */
typedef struct _dvi_ctrl_device_t {
	PFN_DVICTRL_INIT		pfnInit;
	PFN_DVICTRL_RESETCHIP		pfnResetChip;
	PFN_DVICTRL_GETCHIPSTRING	pfnGetChipString;
	PFN_DVICTRL_GETVENDORID		pfnGetVendorId;
	PFN_DVICTRL_GETDEVICEID		pfnGetDeviceId;
	PFN_DVICTRL_SETPOWER		pfnSetPower;
	PFN_DVICTRL_HOTPLUGDETECTION	pfnEnableHotPlugDetection;
	PFN_DVICTRL_ISCONNECTED		pfnIsConnected;
	PFN_DVICTRL_CHECKINTERRUPT	pfnCheckInterrupt;
	PFN_DVICTRL_CLEARINTERRUPT	pfnClearInterrupt;
} dvi_ctrl_device_t;

#define DVI_CTRL_SII164

/* dvi functions prototype */
int dviInit(
	unsigned char edgeSelect,
	unsigned char busSelect,
	unsigned char dualEdgeClkSelect,
	unsigned char hsyncEnable,
	unsigned char vsyncEnable,
	unsigned char deskewEnable,
	unsigned char deskewSetting,
	unsigned char continuousSyncEnable,
	unsigned char pllFilterEnable,
	unsigned char pllFilterValue
);

unsigned short dviGetVendorID(void);
unsigned short dviGetDeviceID(void);

#endif

