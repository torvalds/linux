// This is a stripped-down version of the smu13_driver_if.h file for the relevant DAL interfaces.

#define SMU13_DRIVER_IF_VERSION  0x18

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
	PPCLK_DISPCLK,
	PPCLK_DPPCLK,
	PPCLK_DPREFCLK,
	PPCLK_DCFCLK,
	PPCLK_DTBCLK,
	PPCLK_COUNT,
} PPCLK_e;

typedef struct {
	uint8_t  WmSetting;
	uint8_t  Flags;
	uint8_t  Padding[2];

} WatermarkRowGeneric_t;

#define NUM_WM_RANGES 4

typedef enum {
	WATERMARKS_CLOCK_RANGE = 0,
	WATERMARKS_DUMMY_PSTATE,
	WATERMARKS_MALL,
	WATERMARKS_COUNT,
} WATERMARKS_FLAGS_e;

typedef struct {
	// Watermarks
	WatermarkRowGeneric_t WatermarkRow[NUM_WM_RANGES];
} Watermarks_t;

typedef struct {
	Watermarks_t Watermarks;
	uint32_t  Spare[16];

	uint32_t     MmHubPadding[8]; // SMU internal use
} WatermarksExternal_t;

// Table types
#define TABLE_PMFW_PPTABLE            0
#define TABLE_COMBO_PPTABLE           1
#define TABLE_WATERMARKS              2
#define TABLE_AVFS_PSM_DEBUG          3
#define TABLE_PMSTATUSLOG             4
#define TABLE_SMU_METRICS             5
#define TABLE_DRIVER_SMU_CONFIG       6
#define TABLE_ACTIVITY_MONITOR_COEFF  7
#define TABLE_OVERDRIVE               8
#define TABLE_I2C_COMMANDS            9
#define TABLE_DRIVER_INFO             10
#define TABLE_COUNT                   11
