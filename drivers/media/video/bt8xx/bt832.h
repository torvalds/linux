/* Bt832 CMOS Camera Video Processor (VP)

 The Bt832 CMOS Camera Video Processor chip connects a Quartsight CMOS
  color digital camera directly to video capture devices via an 8-bit,
  4:2:2 YUV or YCrCb video interface.

 i2c addresses: 0x88 or 0x8a
 */

/* The 64 registers: */

// Input Processor
#define BT832_OFFSET 0
#define BT832_RCOMP	1
#define BT832_G1COMP	2
#define BT832_G2COMP	3
#define BT832_BCOMP	4
// Exposures:
#define BT832_FINEH	5
#define BT832_FINEL	6
#define BT832_COARSEH	7
#define BT832_COARSEL   8
#define BT832_CAMGAIN	9
// Main Processor:
#define BT832_M00	10
#define BT832_M01	11
#define BT832_M02	12
#define BT832_M10	13
#define BT832_M11	14
#define BT832_M12	15
#define BT832_M20	16
#define BT832_M21	17
#define BT832_M22	18
#define BT832_APCOR	19
#define BT832_GAMCOR	20
// Level Accumulator Inputs
#define BT832_VPCONTROL2	21
#define BT832_ZONECODE0	22
#define BT832_ZONECODE1	23
#define BT832_ZONECODE2	24
#define BT832_ZONECODE3	25
// Level Accumulator Outputs:
#define BT832_RACC	26
#define BT832_GACC	27
#define BT832_BACC	28
#define BT832_BLACKACC	29
#define BT832_EXP_AGC	30
#define BT832_LACC0	31
#define BT832_LACC1	32
#define BT832_LACC2	33
#define BT832_LACC3	34
#define BT832_LACC4	35
#define BT832_LACC5	36
#define BT832_LACC6	37
#define BT832_LACC7	38
// System:
#define BT832_VP_CONTROL0	39
#define BT832_VP_CONTROL1	40
#define BT832_THRESH	41
#define BT832_VP_TESTCONTROL0	42
#define BT832_VP_DMCODE	43
#define BT832_ACB_CONFIG	44
#define BT832_ACB_GNBASE	45
#define BT832_ACB_MU	46
#define BT832_CAM_TEST0	47
#define BT832_AEC_CONFIG	48
#define BT832_AEC_TL	49
#define BT832_AEC_TC	50
#define BT832_AEC_TH	51
// Status:
#define BT832_VP_STATUS	52
#define BT832_VP_LINECOUNT	53
#define BT832_CAM_DEVICEL	54 // e.g. 0x19
#define BT832_CAM_DEVICEH	55 // e.g. 0x40  == 0x194 Mask0, 0x194 = 404 decimal (VVL-404 camera)
#define BT832_CAM_STATUS		56
 #define BT832_56_CAMERA_PRESENT 0x20
//Camera Setups:
#define BT832_CAM_SETUP0	57
#define BT832_CAM_SETUP1	58
#define BT832_CAM_SETUP2	59
#define BT832_CAM_SETUP3	60
// System:
#define BT832_DEFCOR		61
#define BT832_VP_TESTCONTROL1	62
#define BT832_DEVICE_ID		63
# define BT832_DEVICE_ID__31		0x31 // Bt832 has ID 0x31

/* STMicroelectronivcs VV5404 camera module
   i2c: 0x20: sensor address
   i2c: 0xa0: eeprom for ccd defect map
 */
#define VV5404_device_h		0x00  // 0x19
#define VV5404_device_l		0x01  // 0x40
#define VV5404_status0		0x02
#define VV5404_linecountc	0x03 // current line counter
#define VV5404_linecountl	0x04
#define VV5404_setup0		0x10
#define VV5404_setup1		0x11
#define VV5404_setup2		0x12
#define VV5404_setup4		0x14
#define VV5404_setup5		0x15
#define VV5404_fine_h		0x20  // fine exposure
#define VV5404_fine_l		0x21
#define VV5404_coarse_h		0x22  //coarse exposure
#define VV5404_coarse_l		0x23
#define VV5404_gain		0x24 // ADC pre-amp gain setting
#define VV5404_clk_div		0x25
#define VV5404_cr		0x76 // control register
#define VV5404_as0		0x77 // ADC setup register


// IOCTL
#define BT832_HEXDUMP   _IOR('b',1,int)
#define BT832_REATTACH	_IOR('b',2,int)

/* from BT8x8VXD/capdrv/dialogs.cpp */

/*
typedef enum { SVI, Logitech, Rockwell } CAMERA;

static COMBOBOX_ENTRY gwCameraOptions[] =
{
   { SVI,      "Silicon Vision 512N" },
   { Logitech, "Logitech VideoMan 1.3"  },
   { Rockwell, "Rockwell QuartzSight PCI 1.0"   }
};

// SRAM table values
//===========================================================================
typedef enum { TGB_NTSC624, TGB_NTSC780, TGB_NTSC858, TGB_NTSC392 } TimeGenByte;

BYTE SRAMTable[][ 60 ] =
{
   // TGB_NTSC624
   {
      0x33, // size of table = 51
      0x0E, 0xC0, 0x00, 0x00, 0x90, 0x02, 0x03, 0x10, 0x03, 0x06,
      0x10, 0x04, 0x12, 0x12, 0x05, 0x02, 0x13, 0x04, 0x19, 0x00,
      0x04, 0x39, 0x00, 0x06, 0x59, 0x08, 0x03, 0x85, 0x08, 0x07,
      0x03, 0x50, 0x00, 0x91, 0x40, 0x00, 0x11, 0x01, 0x01, 0x4D,
      0x0D, 0x02, 0x03, 0x11, 0x01, 0x05, 0x37, 0x00, 0x37, 0x21, 0x00
   },
   // TGB_NTSC780
   {
      0x33, // size of table = 51
      0x0e, 0xc0, 0x00, 0x00, 0x90, 0xe2, 0x03, 0x10, 0x03, 0x06,
      0x10, 0x34, 0x12, 0x12, 0x65, 0x02, 0x13, 0x24, 0x19, 0x00,
      0x24, 0x39, 0x00, 0x96, 0x59, 0x08, 0x93, 0x85, 0x08, 0x97,
      0x03, 0x50, 0x50, 0xaf, 0x40, 0x30, 0x5f, 0x01, 0xf1, 0x7f,
      0x0d, 0xf2, 0x03, 0x11, 0xf1, 0x05, 0x37, 0x30, 0x85, 0x21, 0x50
   },
   // TGB_NTSC858
   {
      0x33, // size of table = 51
      0x0c, 0xc0, 0x00, 0x00, 0x90, 0xc2, 0x03, 0x10, 0x03, 0x06,
      0x10, 0x34, 0x12, 0x12, 0x65, 0x02, 0x13, 0x24, 0x19, 0x00,
      0x24, 0x39, 0x00, 0x96, 0x59, 0x08, 0x93, 0x83, 0x08, 0x97,
      0x03, 0x50, 0x30, 0xc0, 0x40, 0x30, 0x86, 0x01, 0x01, 0xa6,
      0x0d, 0x62, 0x03, 0x11, 0x61, 0x05, 0x37, 0x30, 0xac, 0x21, 0x50
   },
   // TGB_NTSC392
   // This table has been modified to be used for Fusion Rev D
   {
      0x2A, // size of table = 42
      0x06, 0x08, 0x04, 0x0a, 0xc0, 0x00, 0x18, 0x08, 0x03, 0x24,
      0x08, 0x07, 0x02, 0x90, 0x02, 0x08, 0x10, 0x04, 0x0c, 0x10,
      0x05, 0x2c, 0x11, 0x04, 0x55, 0x48, 0x00, 0x05, 0x50, 0x00,
      0xbf, 0x0c, 0x02, 0x2f, 0x3d, 0x00, 0x2f, 0x3f, 0x00, 0xc3,
      0x20, 0x00
   }
};

//===========================================================================
// This is the structure of the camera specifications
//===========================================================================
typedef struct tag_cameraSpec
{
   SignalFormat signal;       // which digital signal format the camera has
   VideoFormat  vidFormat;    // video standard
   SyncVideoRef syncRef;      // which sync video reference is used
   State        syncOutput;   // enable sync output for sync video input?
   DecInputClk  iClk;         // which input clock is used
   TimeGenByte  tgb;          // which timing generator byte does the camera use
   int          HReset;       // select 64, 48, 32, or 16 CLKx1 for HReset
   PLLFreq      pllFreq;      // what synthesized frequency to set PLL to
   VSIZEPARMS   vSize;        // video size the camera produces
   int          lineCount;    // expected total number of half-line per frame - 1
   BOOL         interlace;    // interlace signal?
} CameraSpec;

//===========================================================================
// <UPDATE REQUIRED>
// Camera specifications database. Update this table whenever camera spec
// has been changed or added/deleted supported camera models
//===========================================================================
static CameraSpec dbCameraSpec[ N_CAMERAOPTIONS ] =
{  // Silicon Vision 512N
   { Signal_CCIR656, VFormat_NTSC, VRef_alignedCb, Off, DecClk_GPCLK, TGB_NTSC624, 64, KHz19636,
      // Clkx1_HACTIVE, Clkx1_HDELAY, VActive, VDelay, linesPerField; lineCount, Interlace
   {         512,           0x64,       480,    0x13,      240 },         0,       TRUE
   },
   // Logitech VideoMan 1.3
   { Signal_CCIR656, VFormat_NTSC, VRef_alignedCb, Off, DecClk_GPCLK, TGB_NTSC780, 64, KHz24545,
      // Clkx1_HACTIVE, Clkx1_HDELAY, VActive, VDelay, linesPerField; lineCount, Interlace
      {      640,           0x80,       480,    0x1A,      240 },         0,       TRUE
   },
   // Rockwell QuartzSight
   // Note: Fusion Rev D (rev ID 0x02) and later supports 16 pixels for HReset which is preferable.
   //       Use 32 for earlier version of hardware. Clkx1_HDELAY also changed from 0x27 to 0x20.
   { Signal_CCIR656, VFormat_NTSC, VRef_alignedCb, Off, DecClk_GPCLK, TGB_NTSC392, 16, KHz28636,
      // Clkx1_HACTIVE, Clkx1_HDELAY, VActive, VDelay, linesPerField; lineCount, Interlace
      {      352,           0x20,       576,    0x08,      288 },       607,       FALSE
   }
};
*/

/*
The corresponding APIs required to be invoked are:
SetConnector( ConCamera, TRUE/FALSE );
SetSignalFormat( spec.signal );
SetVideoFormat( spec.vidFormat );
SetSyncVideoRef( spec.syncRef );
SetEnableSyncOutput( spec.syncOutput );
SetTimGenByte( SRAMTable[ spec.tgb ], SRAMTableSize[ spec.tgb ] );
SetHReset( spec.HReset );
SetPLL( spec.pllFreq );
SetDecInputClock( spec.iClk );
SetVideoInfo( spec.vSize );
SetTotalLineCount( spec.lineCount );
SetInterlaceMode( spec.interlace );
*/

/* from web:
 Video Sampling
Digital video is a sampled form of analog video. The most common sampling schemes in use today are:
		  Pixel Clock   Horiz    Horiz    Vert
		   Rate         Total    Active
NTSC square pixel  12.27 MHz    780      640      525
NTSC CCIR-601      13.5  MHz    858      720      525
NTSC 4FSc          14.32 MHz    910      768      525
PAL  square pixel  14.75 MHz    944      768      625
PAL  CCIR-601      13.5  MHz    864      720      625
PAL  4FSc          17.72 MHz   1135      948      625

For the CCIR-601 standards, the sampling is based on a static orthogonal sampling grid. The luminance component (Y) is sampled at 13.5 MHz, while the two color difference signals, Cr and Cb are sampled at half that, or 6.75 MHz. The Cr and Cb samples are colocated with alternate Y samples, and they are taken at the same position on each line, such that one sample is coincident with the 50% point of the falling edge of analog sync. The samples are coded to either 8 or 10 bits per component.
*/

/* from DScaler:*/
/*
//===========================================================================
// CCIR656 Digital Input Support: The tables were taken from DScaler proyect
//
// 13 Dec 2000 - Michael Eskin, Conexant Systems - Initial version
//

//===========================================================================
// Timing generator SRAM table values for CCIR601 720x480 NTSC
//===========================================================================
// For NTSC CCIR656
BYTE BtCard::SRAMTable_NTSC[] =
{
    // SRAM Timing Table for NTSC
    0x0c, 0xc0, 0x00,
    0x00, 0x90, 0xc2,
    0x03, 0x10, 0x03,
    0x06, 0x10, 0x34,
    0x12, 0x12, 0x65,
    0x02, 0x13, 0x24,
    0x19, 0x00, 0x24,
    0x39, 0x00, 0x96,
    0x59, 0x08, 0x93,
    0x83, 0x08, 0x97,
    0x03, 0x50, 0x30,
    0xc0, 0x40, 0x30,
    0x86, 0x01, 0x01,
    0xa6, 0x0d, 0x62,
    0x03, 0x11, 0x61,
    0x05, 0x37, 0x30,
    0xac, 0x21, 0x50
};

//===========================================================================
// Timing generator SRAM table values for CCIR601 720x576 NTSC
//===========================================================================
// For PAL CCIR656
BYTE BtCard::SRAMTable_PAL[] =
{
    // SRAM Timing Table for PAL
    0x36, 0x11, 0x01,
    0x00, 0x90, 0x02,
    0x05, 0x10, 0x04,
    0x16, 0x14, 0x05,
    0x11, 0x00, 0x04,
    0x12, 0xc0, 0x00,
    0x31, 0x00, 0x06,
    0x51, 0x08, 0x03,
    0x89, 0x08, 0x07,
    0xc0, 0x44, 0x00,
    0x81, 0x01, 0x01,
    0xa9, 0x0d, 0x02,
    0x02, 0x50, 0x03,
    0x37, 0x3d, 0x00,
    0xaf, 0x21, 0x00,
};
*/
