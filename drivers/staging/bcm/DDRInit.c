#include "headers.h"



#define DDR_DUMP_INTERNAL_DEVICE_MEMORY 0xBFC02B00
#define MIPS_CLOCK_REG 0x0f000820

/* DDR INIT-133Mhz */
#define T3_SKIP_CLOCK_PROGRAM_DUMP_133MHZ 12  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3_DDRSetting133MHz[] = {  /* DPLL Clock Setting */
	{0x0F000800, 0x00007212},
	{0x0f000820, 0x07F13FFF},
	{0x0f000810, 0x00000F95},
	{0x0f000860, 0x00000000},
	{0x0f000880, 0x000003DD},
	/* Changed source for X-bar and MIPS clock to APLL */
	{0x0f000840, 0x0FFF1B00},
	{0x0f000870, 0x00000002},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0F00a084, 0x1Cffffff},
	{0x0F00a080, 0x1C000000},
	{0x0F00a04C, 0x0000000C},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01010100},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020001},
	{0x0F007020, 0x04030107},
	{0x0F007024, 0x02000007},
	{0x0F007028, 0x02020202},
	{0x0F00702c, 0x0206060a},
	{0x0F007030, 0x05000000},
	{0x0F007034, 0x00000003},
	{0x0F007038, 0x110a0200},
	{0x0F00703C, 0x02101010},
	{0x0F007040, 0x45751200},
	{0x0F007044, 0x110a0d00},
	{0x0F007048, 0x081b0306},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0000001c},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x0010246c},
	{0x0F007064, 0x00000010},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00007000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	/* Enable BW improvement within memory controller */
	{0x0F007094, 0x00000104},
	/* Enable 2 ports within X-bar */
	{0x0F00A000, 0x00000016},
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000}
};
/* 80Mhz */
#define T3_SKIP_CLOCK_PROGRAM_DUMP_80MHZ 10  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3_DDRSetting80MHz[] = {  /* DPLL Clock Setting */
	{0x0f000810, 0x00000F95},
	{0x0f000820, 0x07f1ffff},
	{0x0f000860, 0x00000000},
	{0x0f000880, 0x000003DD},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0F00a084, 0x1Cffffff},
	{0x0F00a080, 0x1C000000},
	{0x0F00a000, 0x00000016},
	{0x0F00a04C, 0x0000000C},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01000000},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020000},
	{0x0F007020, 0x04020107},
	{0x0F007024, 0x00000007},
	{0x0F007028, 0x02020201},
	{0x0F00702c, 0x0204040a},
	{0x0F007030, 0x04000000},
	{0x0F007034, 0x00000002},
	{0x0F007038, 0x1F060200},
	{0x0F00703C, 0x1C22221F},
	{0x0F007040, 0x8A006600},
	{0x0F007044, 0x221a0800},
	{0x0F007048, 0x02690204},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0000001c},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x000A15D6},
	{0x0F007064, 0x0000000A},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00004000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	{0x0F007094, 0x00000104},
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000}
};
/* 100Mhz */
#define T3_SKIP_CLOCK_PROGRAM_DUMP_100MHZ 13  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3_DDRSetting100MHz[] = {  /* DPLL Clock Setting */
	{0x0F000800, 0x00007008},
	{0x0f000810, 0x00000F95},
	{0x0f000820, 0x07F13E3F},
	{0x0f000860, 0x00000000},
	{0x0f000880, 0x000003DD},
	/* Changed source for X-bar and MIPS clock to APLL */
	{0x0f000840, 0x0FFF1B00},
	{0x0f000870, 0x00000002},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0F00a084, 0x1Cffffff},
	{0x0F00a080, 0x1C000000},
	{0x0F00a04C, 0x0000000C},
	/* Enable 2 ports within X-bar */
	{0x0F00A000, 0x00000016},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01010100},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020001},
	{0x0F007020, 0x04020107},
	{0x0F007024, 0x00000007},
	{0x0F007028, 0x01020201},
	{0x0F00702c, 0x0204040A},
	{0x0F007030, 0x06000000},
	{0x0F007034, 0x00000004},
	{0x0F007038, 0x20080200},
	{0x0F00703C, 0x02030320},
	{0x0F007040, 0x6E7F1200},
	{0x0F007044, 0x01190A00},
	{0x0F007048, 0x06120305},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0000001C},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x00082ED6},
	{0x0F007064, 0x0000000A},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00005000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	/* Enable BW improvement within memory controller */
	{0x0F007094, 0x00000104},
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000}
};

/* Net T3B DDR Settings
 * DDR INIT-133Mhz
 */
static struct bcm_ddr_setting asDPLL_266MHZ[] = {
	{0x0F000800, 0x00007212},
	{0x0f000820, 0x07F13FFF},
	{0x0f000810, 0x00000F95},
	{0x0f000860, 0x00000000},
	{0x0f000880, 0x000003DD},
	/* Changed source for X-bar and MIPS clock to APLL */
	{0x0f000840, 0x0FFF1B00},
	{0x0f000870, 0x00000002}
};

#define T3B_SKIP_CLOCK_PROGRAM_DUMP_133MHZ 11  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3B_DDRSetting133MHz[] = {  /* DPLL Clock Setting */
	{0x0f000810, 0x00000F95},
	{0x0f000810, 0x00000F95},
	{0x0f000810, 0x00000F95},
	{0x0f000820, 0x07F13652},
	{0x0f000840, 0x0FFF0800},
	/* Changed source for X-bar and MIPS clock to APLL */
	{0x0f000880, 0x000003DD},
	{0x0f000860, 0x00000000},
	/* Changed source for X-bar and MIPS clock to APLL */
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0F00a084, 0x1Cffffff},
	{0x0F00a080, 0x1C000000},
	/* Enable 2 ports within X-bar */
	{0x0F00A000, 0x00000016},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01010100},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020001},
	{0x0F007020, 0x04030107},
	{0x0F007024, 0x02000007},
	{0x0F007028, 0x02020202},
	{0x0F00702c, 0x0206060a},
	{0x0F007030, 0x05000000},
	{0x0F007034, 0x00000003},
	{0x0F007038, 0x130a0200},
	{0x0F00703C, 0x02101012},
	{0x0F007040, 0x457D1200},
	{0x0F007044, 0x11130d00},
	{0x0F007048, 0x040D0306},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0000001c},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x0010246c},
	{0x0F007064, 0x00000012},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00007000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	/* Enable BW improvement within memory controller */
	{0x0F007094, 0x00000104},
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000},
	};

#define T3B_SKIP_CLOCK_PROGRAM_DUMP_80MHZ 9  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3B_DDRSetting80MHz[] = {  /* DPLL Clock Setting */
	{0x0f000810, 0x00000F95},
	{0x0f000820, 0x07F13FFF},
	{0x0f000840, 0x0FFF1F00},
	{0x0f000880, 0x000003DD},
	{0x0f000860, 0x00000000},

	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0F00a084, 0x1Cffffff},
	{0x0F00a080, 0x1C000000},
	{0x0F00a000, 0x00000016},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01000000},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020000},
	{0x0F007020, 0x04020107},
	{0x0F007024, 0x00000007},
	{0x0F007028, 0x02020201},
	{0x0F00702c, 0x0204040a},
	{0x0F007030, 0x04000000},
	{0x0F007034, 0x02000002},
	{0x0F007038, 0x1F060202},
	{0x0F00703C, 0x1C22221F},
	{0x0F007040, 0x8A006600},
	{0x0F007044, 0x221a0800},
	{0x0F007048, 0x02690204},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0100001c},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x000A15D6},
	{0x0F007064, 0x0000000A},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00004000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	{0x0F007094, 0x00000104},
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000}
};

/* 100Mhz */
#define T3B_SKIP_CLOCK_PROGRAM_DUMP_100MHZ 9  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3B_DDRSetting100MHz[] = {  /* DPLL Clock Setting */
	{0x0f000810, 0x00000F95},
	{0x0f000820, 0x07F1369B},
	{0x0f000840, 0x0FFF0800},
	{0x0f000880, 0x000003DD},
	{0x0f000860, 0x00000000},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0F00a084, 0x1Cffffff},
	{0x0F00a080, 0x1C000000},
	/* Enable 2 ports within X-bar */
	{0x0F00A000, 0x00000016},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01010100},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020000},
	{0x0F007020, 0x04020107},
	{0x0F007024, 0x00000007},
	{0x0F007028, 0x01020201},
	{0x0F00702c, 0x0204040A},
	{0x0F007030, 0x06000000},
	{0x0F007034, 0x02000004},
	{0x0F007038, 0x20080200},
	{0x0F00703C, 0x02030320},
	{0x0F007040, 0x6E7F1200},
	{0x0F007044, 0x01190A00},
	{0x0F007048, 0x06120305},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0100001C},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x00082ED6},
	{0x0F007064, 0x0000000A},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00005000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	/* Enable BW improvement within memory controller */
	{0x0F007094, 0x00000104},
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000}
};


#define T3LP_SKIP_CLOCK_PROGRAM_DUMP_133MHZ 9  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3LP_DDRSetting133MHz[] = {  /* DPLL Clock Setting */
	{0x0f000820, 0x03F1365B},
	{0x0f000810, 0x00002F95},
	{0x0f000880, 0x000003DD},
	/* Changed source for X-bar and MIPS clock to APLL */
	{0x0f000840, 0x0FFF0000},
	{0x0f000860, 0x00000000},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0F00a084, 0x1Cffffff},
	{0x0F00a080, 0x1C000000},
	{0x0F00A000, 0x00000016},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01010100},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020001},
	{0x0F007020, 0x04030107},
	{0x0F007024, 0x02000007},
	{0x0F007028, 0x02020200},
	{0x0F00702c, 0x0206060a},
	{0x0F007030, 0x05000000},
	{0x0F007034, 0x00000003},
	{0x0F007038, 0x200a0200},
	{0x0F00703C, 0x02101020},
	{0x0F007040, 0x45711200},
	{0x0F007044, 0x110D0D00},
	{0x0F007048, 0x04080306},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0100001c},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x0010245F},
	{0x0F007064, 0x00000010},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00007000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	{0x0F007088, 0x01000001},
	{0x0F00708c, 0x00000101},
	{0x0F007090, 0x00000000},
	/* Enable BW improvement within memory controller */
	{0x0F007094, 0x00040000},
	{0x0F007098, 0x00000000},
	{0x0F0070c8, 0x00000104},
	/* Enable 2 ports within X-bar */
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000}
};

#define T3LP_SKIP_CLOCK_PROGRAM_DUMP_100MHZ 11  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3LP_DDRSetting100MHz[] = {  /* DPLL Clock Setting */
	{0x0f000810, 0x00002F95},
	{0x0f000820, 0x03F1369B},
	{0x0f000840, 0x0fff0000},
	{0x0f000860, 0x00000000},
	{0x0f000880, 0x000003DD},
	/* Changed source for X-bar and MIPS clock to APLL */
	{0x0f000840, 0x0FFF0000},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0F00a084, 0x1Cffffff},
	{0x0F00a080, 0x1C000000},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01010100},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020000},
	{0x0F007020, 0x04020107},
	{0x0F007024, 0x00000007},
	{0x0F007028, 0x01020200},
	{0x0F00702c, 0x0204040a},
	{0x0F007030, 0x06000000},
	{0x0F007034, 0x00000004},
	{0x0F007038, 0x1F080200},
	{0x0F00703C, 0x0203031F},
	{0x0F007040, 0x6e001200},
	{0x0F007044, 0x011a0a00},
	{0x0F007048, 0x03000305},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0100001c},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x00082ED6},
	{0x0F007064, 0x0000000A},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00005000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	{0x0F007088, 0x01000001},
	{0x0F00708c, 0x00000101},
	{0x0F007090, 0x00000000},
	{0x0F007094, 0x00010000},
	{0x0F007098, 0x00000000},
	{0x0F0070C8, 0x00000104},
	/* Enable 2 ports within X-bar */
	{0x0F00A000, 0x00000016},
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000}
};

#define T3LP_SKIP_CLOCK_PROGRAM_DUMP_80MHZ 9  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3LP_DDRSetting80MHz[] = {  /* DPLL Clock Setting */
	{0x0f000820, 0x07F13FFF},
	{0x0f000810, 0x00002F95},
	{0x0f000860, 0x00000000},
	{0x0f000880, 0x000003DD},
	{0x0f000840, 0x0FFF1F00},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0F00a084, 0x1Cffffff},
	{0x0F00a080, 0x1C000000},
	{0x0F00A000, 0x00000016},
	{0x0f007000, 0x00010001},
	{0x0f007004, 0x01000000},
	{0x0f007008, 0x01000001},
	{0x0f00700c, 0x00000000},
	{0x0f007010, 0x01000000},
	{0x0f007014, 0x01000100},
	{0x0f007018, 0x01000000},
	{0x0f00701c, 0x01020000},
	{0x0f007020, 0x04020107},
	{0x0f007024, 0x00000007},
	{0x0f007028, 0x02020200},
	{0x0f00702c, 0x0204040a},
	{0x0f007030, 0x04000000},
	{0x0f007034, 0x00000002},
	{0x0f007038, 0x1d060200},
	{0x0f00703c, 0x1c22221d},
	{0x0f007040, 0x8A116600},
	{0x0f007044, 0x222d0800},
	{0x0f007048, 0x02690204},
	{0x0f00704c, 0x00000000},
	{0x0f007050, 0x0100001c},
	{0x0f007054, 0x00000000},
	{0x0f007058, 0x00000000},
	{0x0f00705c, 0x00000000},
	{0x0f007060, 0x000A15D6},
	{0x0f007064, 0x0000000A},
	{0x0f007068, 0x00000000},
	{0x0f00706c, 0x00000001},
	{0x0f007070, 0x00004000},
	{0x0f007074, 0x00000000},
	{0x0f007078, 0x00000000},
	{0x0f00707c, 0x00000000},
	{0x0f007080, 0x00000000},
	{0x0f007084, 0x00000000},
	{0x0f007088, 0x01000001},
	{0x0f00708c, 0x00000101},
	{0x0f007090, 0x00000000},
	{0x0f007094, 0x00010000},
	{0x0f007098, 0x00000000},
	{0x0F0070C8, 0x00000104},
	{0x0F007018, 0x01010000}
};




/* T3 LP-B (UMA-B) */

#define T3LPB_SKIP_CLOCK_PROGRAM_DUMP_160MHZ 7  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3LPB_DDRSetting160MHz[] = {  /* DPLL Clock Setting */
	{0x0f000820, 0x03F137DB},
	{0x0f000810, 0x01842795},
	{0x0f000860, 0x00000000},
	{0x0f000880, 0x000003DD},
	{0x0f000840, 0x0FFF0400},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0f003050, 0x00000021},  /* this is flash/eeprom clock divisor which set the flash clock to 20 MHz */
	{0x0F00a084, 0x1Cffffff},  /* Now dump from her in internal memory */
	{0x0F00a080, 0x1C000000},
	{0x0F00A000, 0x00000016},
	{0x0f007000, 0x00010001},
	{0x0f007004, 0x01000001},
	{0x0f007008, 0x01000101},
	{0x0f00700c, 0x00000000},
	{0x0f007010, 0x01000100},
	{0x0f007014, 0x01000100},
	{0x0f007018, 0x01000000},
	{0x0f00701c, 0x01020000},
	{0x0f007020, 0x04030107},
	{0x0f007024, 0x02000007},
	{0x0f007028, 0x02020200},
	{0x0f00702c, 0x0206060a},
	{0x0f007030, 0x050d0d00},
	{0x0f007034, 0x00000003},
	{0x0f007038, 0x170a0200},
	{0x0f00703c, 0x02101012},
	{0x0f007040, 0x45161200},
	{0x0f007044, 0x11250c00},
	{0x0f007048, 0x04da0307},
	{0x0f00704c, 0x00000000},
	{0x0f007050, 0x0000001c},
	{0x0f007054, 0x00000000},
	{0x0f007058, 0x00000000},
	{0x0f00705c, 0x00000000},
	{0x0f007060, 0x00142bb6},
	{0x0f007064, 0x20430014},
	{0x0f007068, 0x00000000},
	{0x0f00706c, 0x00000001},
	{0x0f007070, 0x00009000},
	{0x0f007074, 0x00000000},
	{0x0f007078, 0x00000000},
	{0x0f00707c, 0x00000000},
	{0x0f007080, 0x00000000},
	{0x0f007084, 0x00000000},
	{0x0f007088, 0x01000001},
	{0x0f00708c, 0x00000101},
	{0x0f007090, 0x00000000},
	{0x0f007094, 0x00040000},
	{0x0f007098, 0x00000000},
	{0x0F0070C8, 0x00000104},
	{0x0F007018, 0x01010000}
};


#define T3LPB_SKIP_CLOCK_PROGRAM_DUMP_133MHZ 7  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3LPB_DDRSetting133MHz[] = {  /* DPLL Clock Setting */
	{0x0f000820, 0x03F1365B},
	{0x0f000810, 0x00002F95},
	{0x0f000880, 0x000003DD},
	/* Changed source for X-bar and MIPS clock to APLL */
	{0x0f000840, 0x0FFF0000},
	{0x0f000860, 0x00000000},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0f003050, 0x00000021},  /* flash/eeprom clock divisor which set the flash clock to 20 MHz */
	{0x0F00a084, 0x1Cffffff},  /* dump from here in internal memory */
	{0x0F00a080, 0x1C000000},
	{0x0F00A000, 0x00000016},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01010100},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020001},
	{0x0F007020, 0x04030107},
	{0x0F007024, 0x02000007},
	{0x0F007028, 0x02020200},
	{0x0F00702c, 0x0206060a},
	{0x0F007030, 0x05000000},
	{0x0F007034, 0x00000003},
	{0x0F007038, 0x190a0200},
	{0x0F00703C, 0x02101017},
	{0x0F007040, 0x45171200},
	{0x0F007044, 0x11290D00},
	{0x0F007048, 0x04080306},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0100001c},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x0010245F},
	{0x0F007064, 0x00000010},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00007000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	{0x0F007088, 0x01000001},
	{0x0F00708c, 0x00000101},
	{0x0F007090, 0x00000000},
	/* Enable BW improvement within memory controller */
	{0x0F007094, 0x00040000},
	{0x0F007098, 0x00000000},
	{0x0F0070c8, 0x00000104},
	/* Enable 2 ports within X-bar */
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000}
};

#define T3LPB_SKIP_CLOCK_PROGRAM_DUMP_100MHZ 8  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3LPB_DDRSetting100MHz[] = {  /* DPLL Clock Setting */
	{0x0f000810, 0x00002F95},
	{0x0f000820, 0x03F1369B},
	{0x0f000840, 0x0fff0000},
	{0x0f000860, 0x00000000},
	{0x0f000880, 0x000003DD},
	/* Changed source for X-bar and MIPS clock to APLL */
	{0x0f000840, 0x0FFF0000},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0f003050, 0x00000021},  /* flash/eeprom clock divisor which set the flash clock to 20 MHz */
	{0x0F00a084, 0x1Cffffff},  /* dump from here in internal memory */
	{0x0F00a080, 0x1C000000},
	/* Memcontroller Default values */
	{0x0F007000, 0x00010001},
	{0x0F007004, 0x01010100},
	{0x0F007008, 0x01000001},
	{0x0F00700c, 0x00000000},
	{0x0F007010, 0x01000000},
	{0x0F007014, 0x01000100},
	{0x0F007018, 0x01000000},
	{0x0F00701c, 0x01020000},
	{0x0F007020, 0x04020107},
	{0x0F007024, 0x00000007},
	{0x0F007028, 0x01020200},
	{0x0F00702c, 0x0204040a},
	{0x0F007030, 0x06000000},
	{0x0F007034, 0x00000004},
	{0x0F007038, 0x1F080200},
	{0x0F00703C, 0x0203031F},
	{0x0F007040, 0x6e001200},
	{0x0F007044, 0x011a0a00},
	{0x0F007048, 0x03000305},
	{0x0F00704c, 0x00000000},
	{0x0F007050, 0x0100001c},
	{0x0F007054, 0x00000000},
	{0x0F007058, 0x00000000},
	{0x0F00705c, 0x00000000},
	{0x0F007060, 0x00082ED6},
	{0x0F007064, 0x0000000A},
	{0x0F007068, 0x00000000},
	{0x0F00706c, 0x00000001},
	{0x0F007070, 0x00005000},
	{0x0F007074, 0x00000000},
	{0x0F007078, 0x00000000},
	{0x0F00707C, 0x00000000},
	{0x0F007080, 0x00000000},
	{0x0F007084, 0x00000000},
	{0x0F007088, 0x01000001},
	{0x0F00708c, 0x00000101},
	{0x0F007090, 0x00000000},
	{0x0F007094, 0x00010000},
	{0x0F007098, 0x00000000},
	{0x0F0070C8, 0x00000104},
	/* Enable 2 ports within X-bar */
	{0x0F00A000, 0x00000016},
	/* Enable start bit within memory controller */
	{0x0F007018, 0x01010000}
};

#define T3LPB_SKIP_CLOCK_PROGRAM_DUMP_80MHZ 7  /* index for 0x0F007000 */
static struct bcm_ddr_setting asT3LPB_DDRSetting80MHz[] = {  /* DPLL Clock Setting */
	{0x0f000820, 0x07F13FFF},
	{0x0f000810, 0x00002F95},
	{0x0f000860, 0x00000000},
	{0x0f000880, 0x000003DD},
	{0x0f000840, 0x0FFF1F00},
	{0x0F00a044, 0x1fffffff},
	{0x0F00a040, 0x1f000000},
	{0x0f003050, 0x00000021},  /* flash/eeprom clock divisor which set the flash clock to 20 MHz */
	{0x0F00a084, 0x1Cffffff},  /* dump from here in internal memory */
	{0x0F00a080, 0x1C000000},
	{0x0F00A000, 0x00000016},
	{0x0f007000, 0x00010001},
	{0x0f007004, 0x01000000},
	{0x0f007008, 0x01000001},
	{0x0f00700c, 0x00000000},
	{0x0f007010, 0x01000000},
	{0x0f007014, 0x01000100},
	{0x0f007018, 0x01000000},
	{0x0f00701c, 0x01020000},
	{0x0f007020, 0x04020107},
	{0x0f007024, 0x00000007},
	{0x0f007028, 0x02020200},
	{0x0f00702c, 0x0204040a},
	{0x0f007030, 0x04000000},
	{0x0f007034, 0x00000002},
	{0x0f007038, 0x1d060200},
	{0x0f00703c, 0x1c22221d},
	{0x0f007040, 0x8A116600},
	{0x0f007044, 0x222d0800},
	{0x0f007048, 0x02690204},
	{0x0f00704c, 0x00000000},
	{0x0f007050, 0x0100001c},
	{0x0f007054, 0x00000000},
	{0x0f007058, 0x00000000},
	{0x0f00705c, 0x00000000},
	{0x0f007060, 0x000A15D6},
	{0x0f007064, 0x0000000A},
	{0x0f007068, 0x00000000},
	{0x0f00706c, 0x00000001},
	{0x0f007070, 0x00004000},
	{0x0f007074, 0x00000000},
	{0x0f007078, 0x00000000},
	{0x0f00707c, 0x00000000},
	{0x0f007080, 0x00000000},
	{0x0f007084, 0x00000000},
	{0x0f007088, 0x01000001},
	{0x0f00708c, 0x00000101},
	{0x0f007090, 0x00000000},
	{0x0f007094, 0x00010000},
	{0x0f007098, 0x00000000},
	{0x0F0070C8, 0x00000104},
	{0x0F007018, 0x01010000}
};


int ddr_init(struct bcm_mini_adapter *Adapter)
{
	struct bcm_ddr_setting *psDDRSetting = NULL;
	ULONG RegCount = 0;
	UINT value = 0;
	UINT uiResetValue = 0;
	UINT uiClockSetting = 0;
	int retval = STATUS_SUCCESS;

	switch (Adapter->chip_id) {
	case 0xbece3200:
		switch (Adapter->DDRSetting) {
		case DDR_80_MHZ:
			psDDRSetting = asT3LP_DDRSetting80MHz;
			RegCount = (sizeof(asT3LP_DDRSetting80MHz) /
				    sizeof(struct bcm_ddr_setting));
			break;
		case DDR_100_MHZ:
			psDDRSetting = asT3LP_DDRSetting100MHz;
			RegCount = (sizeof(asT3LP_DDRSetting100MHz) /
				    sizeof(struct bcm_ddr_setting));
			break;
		case DDR_133_MHZ:
			psDDRSetting = asT3LP_DDRSetting133MHz;
			RegCount = (sizeof(asT3LP_DDRSetting133MHz) /
				    sizeof(struct bcm_ddr_setting));
			if (Adapter->bMipsConfig == MIPS_200_MHZ)
				uiClockSetting = 0x03F13652;
			else
				uiClockSetting = 0x03F1365B;
			break;
		default:
			return -EINVAL;
		}

		break;
	case T3LPB:
	case BCS220_2:
	case BCS220_2BC:
	case BCS250_BC:
	case BCS220_3:
		/* Set bit 2 and bit 6 to 1 for BBIC 2mA drive
		 * (please check current value and additionally set these bits)
		 */
	if ((Adapter->chip_id !=  BCS220_2) &&
		(Adapter->chip_id !=  BCS220_2BC) &&
		(Adapter->chip_id != BCS220_3)) {
		retval = rdmalt(Adapter, (UINT)0x0f000830, &uiResetValue, sizeof(uiResetValue));
		if (retval < 0) {
			BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
			return retval;
		}
		uiResetValue |= 0x44;
		retval = wrmalt(Adapter, (UINT)0x0f000830, &uiResetValue, sizeof(uiResetValue));
		if (retval < 0) {
			BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
			return retval;
		}
	}
		switch (Adapter->DDRSetting) {



		case DDR_80_MHZ:
			psDDRSetting = asT3LPB_DDRSetting80MHz;
			RegCount = (sizeof(asT3B_DDRSetting80MHz) /
				    sizeof(struct bcm_ddr_setting));
			break;
		case DDR_100_MHZ:
			psDDRSetting = asT3LPB_DDRSetting100MHz;
			RegCount = (sizeof(asT3B_DDRSetting100MHz) /
				    sizeof(struct bcm_ddr_setting));
			break;
		case DDR_133_MHZ:
			psDDRSetting = asT3LPB_DDRSetting133MHz;
			RegCount = (sizeof(asT3B_DDRSetting133MHz) /
				    sizeof(struct bcm_ddr_setting));

			if (Adapter->bMipsConfig == MIPS_200_MHZ)
				uiClockSetting = 0x03F13652;
			else
				uiClockSetting = 0x03F1365B;
			break;

		case DDR_160_MHZ:
			psDDRSetting = asT3LPB_DDRSetting160MHz;
			RegCount = sizeof(asT3LPB_DDRSetting160MHz)/sizeof(struct bcm_ddr_setting);

			if (Adapter->bMipsConfig == MIPS_200_MHZ)
				uiClockSetting = 0x03F137D2;
			else
				uiClockSetting = 0x03F137DB;
		}
			break;

	case 0xbece0110:
	case 0xbece0120:
	case 0xbece0121:
	case 0xbece0130:
	case 0xbece0300:
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "DDR Setting: %x\n", Adapter->DDRSetting);
		switch (Adapter->DDRSetting) {
		case DDR_80_MHZ:
			psDDRSetting = asT3_DDRSetting80MHz;
			RegCount = (sizeof(asT3_DDRSetting80MHz) /
				    sizeof(struct bcm_ddr_setting));
			break;
		case DDR_100_MHZ:
			psDDRSetting = asT3_DDRSetting100MHz;
			RegCount = (sizeof(asT3_DDRSetting100MHz) /
				    sizeof(struct bcm_ddr_setting));
			break;
		case DDR_133_MHZ:
			psDDRSetting = asT3_DDRSetting133MHz;
			RegCount = (sizeof(asT3_DDRSetting133MHz) /
				    sizeof(struct bcm_ddr_setting));
			break;
		default:
			return -EINVAL;
		}
	case 0xbece0310:
	{
		switch (Adapter->DDRSetting) {
		case DDR_80_MHZ:
			psDDRSetting = asT3B_DDRSetting80MHz;
			RegCount = (sizeof(asT3B_DDRSetting80MHz) /
				    sizeof(struct bcm_ddr_setting));
			break;
		case DDR_100_MHZ:
			psDDRSetting = asT3B_DDRSetting100MHz;
			RegCount = (sizeof(asT3B_DDRSetting100MHz) /
				    sizeof(struct bcm_ddr_setting));
			break;
		case DDR_133_MHZ:

			if (Adapter->bDPLLConfig == PLL_266_MHZ) {  /* 266Mhz PLL selected. */
				memcpy(asT3B_DDRSetting133MHz, asDPLL_266MHZ,
				       sizeof(asDPLL_266MHZ));
				psDDRSetting = asT3B_DDRSetting133MHz;
				RegCount = (sizeof(asT3B_DDRSetting133MHz) /
					    sizeof(struct bcm_ddr_setting));
			} else {
				psDDRSetting = asT3B_DDRSetting133MHz;
				RegCount = (sizeof(asT3B_DDRSetting133MHz) /
					    sizeof(struct bcm_ddr_setting));
				if (Adapter->bMipsConfig == MIPS_200_MHZ)
					uiClockSetting = 0x07F13652;
				else
					uiClockSetting = 0x07F1365B;
			}
			break;
		default:
			return -EINVAL;
		}
		break;

	}
	default:
		return -EINVAL;
	}

	value = 0;
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Register Count is =%lu\n", RegCount);
	while (RegCount && !retval) {
		if (uiClockSetting && psDDRSetting->ulRegAddress == MIPS_CLOCK_REG)
			value = uiClockSetting;
		else
			value = psDDRSetting->ulRegValue;
		retval = wrmalt(Adapter, psDDRSetting->ulRegAddress, &value, sizeof(value));
		if (STATUS_SUCCESS != retval) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "%s:%d\n", __func__, __LINE__);
			break;
		}

		RegCount--;
		psDDRSetting++;
	}

	if (Adapter->chip_id >= 0xbece3300) {

		mdelay(3);
		if ((Adapter->chip_id != BCS220_2) &&
			(Adapter->chip_id != BCS220_2BC) &&
			(Adapter->chip_id != BCS220_3)) {
			/* drive MDDR to half in case of UMA-B:	*/
			uiResetValue = 0x01010001;
			retval = wrmalt(Adapter, (UINT)0x0F007018, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			uiResetValue = 0x00040020;
			retval = wrmalt(Adapter, (UINT)0x0F007094, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			uiResetValue = 0x01020101;
			retval = wrmalt(Adapter, (UINT)0x0F00701c, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			uiResetValue = 0x01010000;
			retval = wrmalt(Adapter, (UINT)0x0F007018, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
		}
		mdelay(3);

		/* DC/DC standby change...
		 * This is to be done only for Hybrid PMU mode.
		 * with the current h/w there is no way to detect this.
		 * and since we dont have internal PMU lets do it under UMA-B chip id.
		 * we will change this when we will have internal PMU.
		 */
		if (Adapter->PmuMode == HYBRID_MODE_7C) {
			retval = rdmalt(Adapter, (UINT)0x0f000c00, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			retval = rdmalt(Adapter, (UINT)0x0f000c00, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			uiResetValue = 0x1322a8;
			retval = wrmalt(Adapter, (UINT)0x0f000d1c, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			retval = rdmalt(Adapter, (UINT)0x0f000c00, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			retval = rdmalt(Adapter, (UINT)0x0f000c00, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			uiResetValue = 0x132296;
			retval = wrmalt(Adapter, (UINT)0x0f000d14, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
		} else if (Adapter->PmuMode == HYBRID_MODE_6) {

			retval = rdmalt(Adapter, (UINT)0x0f000c00, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			retval = rdmalt(Adapter, (UINT)0x0f000c00, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			uiResetValue = 0x6003229a;
			retval = wrmalt(Adapter, (UINT)0x0f000d14, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			retval = rdmalt(Adapter, (UINT)0x0f000c00, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			retval = rdmalt(Adapter, (UINT)0x0f000c00, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
			uiResetValue = 0x1322a8;
			retval = wrmalt(Adapter, (UINT)0x0f000d1c, &uiResetValue, sizeof(uiResetValue));
			if (retval < 0) {
				BCM_DEBUG_PRINT(Adapter, CMHOST, RDM, DBG_LVL_ALL, "%s:%d RDM failed\n", __func__, __LINE__);
				return retval;
			}
		}

	}
	Adapter->bDDRInitDone = TRUE;
	return retval;
}

int download_ddr_settings(struct bcm_mini_adapter *Adapter)
{
	struct bcm_ddr_setting *psDDRSetting = NULL;
	ULONG RegCount = 0;
	unsigned long ul_ddr_setting_load_addr = DDR_DUMP_INTERNAL_DEVICE_MEMORY;
	UINT value = 0;
	int retval = STATUS_SUCCESS;
	bool bOverrideSelfRefresh = false;

	switch (Adapter->chip_id) {
	case 0xbece3200:
		switch (Adapter->DDRSetting) {
		case DDR_80_MHZ:
			psDDRSetting = asT3LP_DDRSetting80MHz;
			RegCount = ARRAY_SIZE(asT3LP_DDRSetting80MHz);
			RegCount -= T3LP_SKIP_CLOCK_PROGRAM_DUMP_80MHZ;
			psDDRSetting += T3LP_SKIP_CLOCK_PROGRAM_DUMP_80MHZ;
			break;
		case DDR_100_MHZ:
			psDDRSetting = asT3LP_DDRSetting100MHz;
			RegCount = ARRAY_SIZE(asT3LP_DDRSetting100MHz);
			RegCount -= T3LP_SKIP_CLOCK_PROGRAM_DUMP_100MHZ;
			psDDRSetting += T3LP_SKIP_CLOCK_PROGRAM_DUMP_100MHZ;
			break;
		case DDR_133_MHZ:
			bOverrideSelfRefresh = TRUE;
			psDDRSetting = asT3LP_DDRSetting133MHz;
			RegCount = ARRAY_SIZE(asT3LP_DDRSetting133MHz);
			RegCount -= T3LP_SKIP_CLOCK_PROGRAM_DUMP_133MHZ;
			psDDRSetting += T3LP_SKIP_CLOCK_PROGRAM_DUMP_133MHZ;
			break;
		default:
			return -EINVAL;
		}
		break;

	case T3LPB:
	case BCS220_2:
	case BCS220_2BC:
	case BCS250_BC:
	case BCS220_3:
		switch (Adapter->DDRSetting) {
		case DDR_80_MHZ:
			psDDRSetting = asT3LPB_DDRSetting80MHz;
			RegCount = ARRAY_SIZE(asT3LPB_DDRSetting80MHz);
			RegCount -= T3LPB_SKIP_CLOCK_PROGRAM_DUMP_80MHZ;
			psDDRSetting += T3LPB_SKIP_CLOCK_PROGRAM_DUMP_80MHZ;
			break;
		case DDR_100_MHZ:
			psDDRSetting = asT3LPB_DDRSetting100MHz;
			RegCount = ARRAY_SIZE(asT3LPB_DDRSetting100MHz);
			RegCount -= T3LPB_SKIP_CLOCK_PROGRAM_DUMP_100MHZ;
			psDDRSetting += T3LPB_SKIP_CLOCK_PROGRAM_DUMP_100MHZ;
			break;
		case DDR_133_MHZ:
			bOverrideSelfRefresh = TRUE;
			psDDRSetting = asT3LPB_DDRSetting133MHz;
			RegCount = ARRAY_SIZE(asT3LPB_DDRSetting133MHz);
			RegCount -= T3LPB_SKIP_CLOCK_PROGRAM_DUMP_133MHZ;
			psDDRSetting += T3LPB_SKIP_CLOCK_PROGRAM_DUMP_133MHZ;
			break;

		case DDR_160_MHZ:
			bOverrideSelfRefresh = TRUE;
			psDDRSetting = asT3LPB_DDRSetting160MHz;
			RegCount = ARRAY_SIZE(asT3LPB_DDRSetting160MHz);
			RegCount -= T3LPB_SKIP_CLOCK_PROGRAM_DUMP_160MHZ;
			psDDRSetting += T3LPB_SKIP_CLOCK_PROGRAM_DUMP_160MHZ;

			break;
		default:
			return -EINVAL;
		}
		break;
	case 0xbece0300:
		switch (Adapter->DDRSetting) {
		case DDR_80_MHZ:
			psDDRSetting = asT3_DDRSetting80MHz;
			RegCount = ARRAY_SIZE(asT3_DDRSetting80MHz);
			RegCount -= T3_SKIP_CLOCK_PROGRAM_DUMP_80MHZ;
			psDDRSetting += T3_SKIP_CLOCK_PROGRAM_DUMP_80MHZ;
			break;
		case DDR_100_MHZ:
			psDDRSetting = asT3_DDRSetting100MHz;
			RegCount = ARRAY_SIZE(asT3_DDRSetting100MHz);
			RegCount -= T3_SKIP_CLOCK_PROGRAM_DUMP_100MHZ;
			psDDRSetting += T3_SKIP_CLOCK_PROGRAM_DUMP_100MHZ;
			break;
		case DDR_133_MHZ:
			psDDRSetting = asT3_DDRSetting133MHz;
			RegCount = ARRAY_SIZE(asT3_DDRSetting133MHz);
			RegCount -= T3_SKIP_CLOCK_PROGRAM_DUMP_133MHZ;
			psDDRSetting += T3_SKIP_CLOCK_PROGRAM_DUMP_133MHZ;
			break;
		default:
			return -EINVAL;
		}
	break;
	case 0xbece0310:
	    {
		switch (Adapter->DDRSetting) {
		case DDR_80_MHZ:
			psDDRSetting = asT3B_DDRSetting80MHz;
			RegCount = ARRAY_SIZE(asT3B_DDRSetting80MHz);
			RegCount -= T3B_SKIP_CLOCK_PROGRAM_DUMP_80MHZ;
			psDDRSetting += T3B_SKIP_CLOCK_PROGRAM_DUMP_80MHZ;
			break;
		case DDR_100_MHZ:
			psDDRSetting = asT3B_DDRSetting100MHz;
			RegCount = ARRAY_SIZE(asT3B_DDRSetting100MHz);
			RegCount -= T3B_SKIP_CLOCK_PROGRAM_DUMP_100MHZ;
			psDDRSetting += T3B_SKIP_CLOCK_PROGRAM_DUMP_100MHZ;
			break;
		case DDR_133_MHZ:
			bOverrideSelfRefresh = TRUE;
			psDDRSetting = asT3B_DDRSetting133MHz;
			RegCount = ARRAY_SIZE(asT3B_DDRSetting133MHz);
			RegCount -= T3B_SKIP_CLOCK_PROGRAM_DUMP_133MHZ;
			psDDRSetting += T3B_SKIP_CLOCK_PROGRAM_DUMP_133MHZ;
		break;
		}
		break;
	     }
	default:
		return -EINVAL;
	}
	/* total number of Register that has to be dumped */
	value = RegCount;
	retval = wrmalt(Adapter, ul_ddr_setting_load_addr, &value, sizeof(value));
	if (retval) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "%s:%d\n", __func__, __LINE__);

		return retval;
	}
	ul_ddr_setting_load_addr += sizeof(ULONG);
	/* signature */
	value = (0x1d1e0dd0);
	retval = wrmalt(Adapter, ul_ddr_setting_load_addr, &value, sizeof(value));
	if (retval) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "%s:%d\n", __func__, __LINE__);
		return retval;
	}

	ul_ddr_setting_load_addr += sizeof(ULONG);
	RegCount *= (sizeof(struct bcm_ddr_setting)/sizeof(ULONG));

	while (RegCount && !retval) {
		value = psDDRSetting->ulRegAddress;
		retval = wrmalt(Adapter, ul_ddr_setting_load_addr, &value, sizeof(value));
		ul_ddr_setting_load_addr += sizeof(ULONG);
		if (!retval) {
			if (bOverrideSelfRefresh && (psDDRSetting->ulRegAddress == 0x0F007018))
				value = (psDDRSetting->ulRegValue | (1<<8));
			else
				value = psDDRSetting->ulRegValue;

			if (STATUS_SUCCESS != wrmalt(Adapter, ul_ddr_setting_load_addr,
					&value, sizeof(value))) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "%s:%d\n", __func__, __LINE__);
				break;
			}
		}
		ul_ddr_setting_load_addr += sizeof(ULONG);
		RegCount--;
		psDDRSetting++;
	}
	return retval;
}
