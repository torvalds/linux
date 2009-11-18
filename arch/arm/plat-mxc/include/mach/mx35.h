/*
 * IRAM
 */
#define MX35_IRAM_BASE_ADDR		0x10000000	/* internal ram */
#define MX35_IRAM_SIZE		SZ_128K

#define MXC_FEC_BASE_ADDR	0x50038000
#define MX35_OTG_BASE_ADDR	0x53ff4000
#define MX35_NFC_BASE_ADDR	0xBB000000

/*
 * Interrupt numbers
 */
#define MXC_INT_OWIRE		2
#define MX35_INT_MMC_SDHC1	7
#define MXC_INT_MMC_SDHC2	8
#define MXC_INT_MMC_SDHC3	9
#define MX35_INT_SSI1		11
#define MX35_INT_SSI2		12
#define MXC_INT_GPU2D		16
#define MXC_INT_ASRC		17
#define MXC_INT_USBHS		35
#define MXC_INT_USBOTG		37
#define MXC_INT_ESAI		40
#define MXC_INT_CAN1		43
#define MXC_INT_CAN2		44
#define MXC_INT_MLB		46
#define MXC_INT_SPDIF		47
#define MXC_INT_FEC		57

