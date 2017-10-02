#ifndef DDK750_SII164_H__
#define DDK750_SII164_H__

#define USE_DVICHIP

/* Hot Plug detection mode structure */
enum sii164_hot_plug_mode {
	SII164_HOTPLUG_DISABLE = 0,         /* Disable Hot Plug output bit (always high). */
	SII164_HOTPLUG_USE_MDI,             /* Use Monitor Detect Interrupt bit. */
	SII164_HOTPLUG_USE_RSEN,            /* Use Receiver Sense detect bit. */
	SII164_HOTPLUG_USE_HTPLG            /* Use Hot Plug detect bit. */
};


/* Silicon Image SiI164 chip prototype */
long sii164InitChip(unsigned char edgeSelect,
		    unsigned char busSelect,
		    unsigned char dualEdgeClkSelect,
		    unsigned char hsyncEnable,
		    unsigned char vsyncEnable,
		    unsigned char deskewEnable,
		    unsigned char deskewSetting,
		    unsigned char continuousSyncEnable,
		    unsigned char pllFilterEnable,
		    unsigned char pllFilterValue);

unsigned short sii164GetVendorID(void);
unsigned short sii164GetDeviceID(void);


#ifdef SII164_FULL_FUNCTIONS
void sii164ResetChip(void);
char *sii164GetChipString(void);
void sii164SetPower(unsigned char powerUp);
void sii164EnableHotPlugDetection(unsigned char enableHotPlug);
unsigned char sii164IsConnected(void);
unsigned char sii164CheckInterrupt(void);
void sii164ClearInterrupt(void);
#endif
/*
 * below register definition is used for
 * Silicon Image SiI164 DVI controller chip
 */
/*
 * Vendor ID registers
 */
#define SII164_VENDOR_ID_LOW                        0x00
#define SII164_VENDOR_ID_HIGH                       0x01

/*
 * Device ID registers
 */
#define SII164_DEVICE_ID_LOW                        0x02
#define SII164_DEVICE_ID_HIGH                       0x03

/*
 * Device Revision
 */
#define SII164_DEVICE_REVISION                      0x04

/*
 * Frequency Limitation registers
 */
#define SII164_FREQUENCY_LIMIT_LOW                  0x06
#define SII164_FREQUENCY_LIMIT_HIGH                 0x07

/*
 * Power Down and Input Signal Configuration registers
 */
#define SII164_CONFIGURATION                        0x08

/* Power down (PD) */
#define SII164_CONFIGURATION_POWER_DOWN             0x00
#define SII164_CONFIGURATION_POWER_NORMAL           0x01
#define SII164_CONFIGURATION_POWER_MASK             0x01

/* Input Edge Latch Select (EDGE) */
#define SII164_CONFIGURATION_LATCH_FALLING          0x00
#define SII164_CONFIGURATION_LATCH_RISING           0x02

/* Bus Select (BSEL) */
#define SII164_CONFIGURATION_BUS_12BITS             0x00
#define SII164_CONFIGURATION_BUS_24BITS             0x04

/* Dual Edge Clock Select (DSEL) */
#define SII164_CONFIGURATION_CLOCK_SINGLE           0x00
#define SII164_CONFIGURATION_CLOCK_DUAL             0x08

/* Horizontal Sync Enable (HEN) */
#define SII164_CONFIGURATION_HSYNC_FORCE_LOW        0x00
#define SII164_CONFIGURATION_HSYNC_AS_IS            0x10

/* Vertical Sync Enable (VEN) */
#define SII164_CONFIGURATION_VSYNC_FORCE_LOW        0x00
#define SII164_CONFIGURATION_VSYNC_AS_IS            0x20

/*
 * Detection registers
 */
#define SII164_DETECT                               0x09

/* Monitor Detect Interrupt (MDI) */
#define SII164_DETECT_MONITOR_STATE_CHANGE          0x00
#define SII164_DETECT_MONITOR_STATE_NO_CHANGE       0x01
#define SII164_DETECT_MONITOR_STATE_CLEAR           0x01
#define SII164_DETECT_MONITOR_STATE_MASK            0x01

/* Hot Plug detect Input (HTPLG) */
#define SII164_DETECT_HOT_PLUG_STATUS_OFF           0x00
#define SII164_DETECT_HOT_PLUG_STATUS_ON            0x02
#define SII164_DETECT_HOT_PLUG_STATUS_MASK          0x02

/* Receiver Sense (RSEN) */
#define SII164_DETECT_RECEIVER_SENSE_NOT_DETECTED   0x00
#define SII164_DETECT_RECEIVER_SENSE_DETECTED       0x04

/* Interrupt Generation Method (TSEL) */
#define SII164_DETECT_INTERRUPT_BY_RSEN_PIN         0x00
#define SII164_DETECT_INTERRUPT_BY_HTPLG_PIN        0x08
#define SII164_DETECT_INTERRUPT_MASK                0x08

/* Monitor Sense Output (MSEN) */
#define SII164_DETECT_MONITOR_SENSE_OUTPUT_HIGH     0x00
#define SII164_DETECT_MONITOR_SENSE_OUTPUT_MDI      0x10
#define SII164_DETECT_MONITOR_SENSE_OUTPUT_RSEN     0x20
#define SII164_DETECT_MONITOR_SENSE_OUTPUT_HTPLG    0x30
#define SII164_DETECT_MONITOR_SENSE_OUTPUT_FLAG     0x30

/*
 * Skewing registers
 */
#define SII164_DESKEW                               0x0A

/* General Purpose Input (CTL[3:1]) */
#define SII164_DESKEW_GENERAL_PURPOSE_INPUT_MASK    0x0E

/* De-skewing Enable bit (DKEN) */
#define SII164_DESKEW_DISABLE                       0x00
#define SII164_DESKEW_ENABLE                        0x10

/* De-skewing Setting (DK[3:1])*/
#define SII164_DESKEW_1_STEP                        0x00
#define SII164_DESKEW_2_STEP                        0x20
#define SII164_DESKEW_3_STEP                        0x40
#define SII164_DESKEW_4_STEP                        0x60
#define SII164_DESKEW_5_STEP                        0x80
#define SII164_DESKEW_6_STEP                        0xA0
#define SII164_DESKEW_7_STEP                        0xC0
#define SII164_DESKEW_8_STEP                        0xE0

/*
 * User Configuration Data registers (CFG 7:0)
 */
#define SII164_USER_CONFIGURATION                   0x0B

/*
 * PLL registers
 */
#define SII164_PLL                                  0x0C

/* PLL Filter Value (PLLF) */
#define SII164_PLL_FILTER_VALUE_MASK                0x0E

/* PLL Filter Enable (PFEN) */
#define SII164_PLL_FILTER_DISABLE                   0x00
#define SII164_PLL_FILTER_ENABLE                    0x01

/* Sync Continuous (SCNT) */
#define SII164_PLL_FILTER_SYNC_CONTINUOUS_DISABLE   0x00
#define SII164_PLL_FILTER_SYNC_CONTINUOUS_ENABLE    0x80

#endif
