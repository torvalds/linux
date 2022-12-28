// SPDX-License-Identifier: MIT
// This is a stripped-down version of the smu11_driver_if.h file for the relevant DAL interfaces.

#define SMU11_DRIVER_IF_VERSION 0x40

//Only Clks that have DPM descriptors are listed here
typedef enum {
	PPCLK_GFXCLK = 0,
	PPCLK_SOCCLK,
	PPCLK_UCLK,
	PPCLK_FCLK,
	PPCLK_DCLK_0,
	PPCLK_VCLK_0,
	PPCLK_DCLK_1,
	PPCLK_VCLK_1,
	PPCLK_DCEFCLK,
	PPCLK_DISPCLK,
	PPCLK_PIXCLK,
	PPCLK_PHYCLK,
	PPCLK_DTBCLK,
	PPCLK_COUNT,
} PPCLK_e;

typedef struct {
	uint16_t MinClock; // This is either DCEFCLK or SOCCLK (in MHz)
	uint16_t MaxClock; // This is either DCEFCLK or SOCCLK (in MHz)
	uint16_t MinUclk;
	uint16_t MaxUclk;

	uint8_t  WmSetting;
	uint8_t  Flags;
	uint8_t  Padding[2];

} WatermarkRowGeneric_t;

#define NUM_WM_RANGES 4

typedef enum {
	WM_SOCCLK = 0,
	WM_DCEFCLK,
	WM_COUNT,
} WM_CLOCK_e;

typedef enum {
	WATERMARKS_CLOCK_RANGE = 0,
	WATERMARKS_DUMMY_PSTATE,
	WATERMARKS_MALL,
	WATERMARKS_COUNT,
} WATERMARKS_FLAGS_e;

typedef struct {
	// Watermarks
	WatermarkRowGeneric_t WatermarkRow[WM_COUNT][NUM_WM_RANGES];
} Watermarks_t;

typedef struct {
	Watermarks_t Watermarks;

	uint32_t     MmHubPadding[8]; // SMU internal use
} WatermarksExternal_t;

// Table types
#define TABLE_PPTABLE                 0
#define TABLE_WATERMARKS              1
#define TABLE_AVFS_PSM_DEBUG          2
#define TABLE_AVFS_FUSE_OVERRIDE      3
#define TABLE_PMSTATUSLOG             4
#define TABLE_SMU_METRICS             5
#define TABLE_DRIVER_SMU_CONFIG       6
#define TABLE_ACTIVITY_MONITOR_COEFF  7
#define TABLE_OVERDRIVE               8
#define TABLE_I2C_COMMANDS            9
#define TABLE_PACE                   10
#define TABLE_ECCINFO                11
#define TABLE_COUNT                  12
