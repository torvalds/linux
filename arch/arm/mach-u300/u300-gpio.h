/*
 * Individual pin assignments for the B26/S26. Notice that the
 * actual usage of these pins depends on the PAD MUX settings, that
 * is why the same number can potentially appear several times.
 * In the reference design each pin is only used for one purpose.
 * These were determined by inspecting the B26/S26 schematic:
 * 2/1911-ROA 128 1603
 */
#ifdef CONFIG_MACH_U300_BS2X
#define U300_GPIO_PIN_UART_RX		0
#define U300_GPIO_PIN_UART_TX		1
#define U300_GPIO_PIN_GPIO02		2  /* Unrouted */
#define U300_GPIO_PIN_GPIO03		3  /* Unrouted */
#define U300_GPIO_PIN_CAM_SLEEP		4
#define U300_GPIO_PIN_CAM_REG_EN	5
#define U300_GPIO_PIN_GPIO06		6  /* Unrouted */
#define U300_GPIO_PIN_GPIO07		7  /* Unrouted */

#define U300_GPIO_PIN_GPIO08		8  /* Service point SP2321 */
#define U300_GPIO_PIN_GPIO09		9  /* Service point SP2322 */
#define U300_GPIO_PIN_PHFSENSE		10 /* Headphone jack sensing */
#define U300_GPIO_PIN_MMC_CLKRET	11 /* Clock return from MMC/SD card */
#define U300_GPIO_PIN_MMC_CD		12 /* MMC Card insertion detection */
#define U300_GPIO_PIN_FLIPSENSE		13 /* Mechanical flip sensing */
#define U300_GPIO_PIN_GPIO14		14 /* DSP JTAG Port RTCK */
#define U300_GPIO_PIN_GPIO15		15 /* Unrouted */

#define U300_GPIO_PIN_GPIO16		16 /* Unrouted */
#define U300_GPIO_PIN_GPIO17		17 /* Unrouted */
#define U300_GPIO_PIN_GPIO18		18 /* Unrouted */
#define U300_GPIO_PIN_GPIO19		19 /* Unrouted */
#define U300_GPIO_PIN_GPIO20		20 /* Unrouted */
#define U300_GPIO_PIN_GPIO21		21 /* Unrouted */
#define U300_GPIO_PIN_GPIO22		22 /* Unrouted */
#define U300_GPIO_PIN_GPIO23		23 /* Unrouted */
#endif

/*
 * Individual pin assignments for the B330/S330 and B365/S365.
 * Notice that the actual usage of these pins depends on the
 * PAD MUX settings, that is why the same number can potentially
 * appear several times. In the reference design each pin is only
 * used for one purpose. These were determined by inspecting the
 * S365 schematic.
 */
#if defined(CONFIG_MACH_U300_BS330) || defined(CONFIG_MACH_U300_BS365) || \
    defined(CONFIG_MACH_U300_BS335)
#define U300_GPIO_PIN_UART_RX		0
#define U300_GPIO_PIN_UART_TX		1
#define U300_GPIO_PIN_UART_CTS		2
#define U300_GPIO_PIN_UART_RTS		3
#define U300_GPIO_PIN_CAM_MAIN_STANDBY	4 /* Camera MAIN standby */
#define U300_GPIO_PIN_GPIO05		5 /* Unrouted */
#define U300_GPIO_PIN_MS_CD		6 /* Memory Stick Card insertion */
#define U300_GPIO_PIN_GPIO07		7 /* Test point TP2430 */

#define U300_GPIO_PIN_GPIO08		8 /* Test point TP2437 */
#define U300_GPIO_PIN_GPIO09		9 /* Test point TP2431 */
#define U300_GPIO_PIN_GPIO10		10 /* Test point TP2432 */
#define U300_GPIO_PIN_MMC_CLKRET	11 /* Clock return from MMC/SD card */
#define U300_GPIO_PIN_MMC_CD		12 /* MMC Card insertion detection */
#define U300_GPIO_PIN_CAM_SUB_STANDBY	13 /* Camera SUB standby */
#define U300_GPIO_PIN_GPIO14		14 /* Test point TP2436 */
#define U300_GPIO_PIN_GPIO15		15 /* Unrouted */

#define U300_GPIO_PIN_GPIO16		16 /* Test point TP2438 */
#define U300_GPIO_PIN_PHFSENSE		17 /* Headphone jack sensing */
#define U300_GPIO_PIN_GPIO18		18 /* Test point TP2439 */
#define U300_GPIO_PIN_GPIO19		19 /* Routed somewhere */
#define U300_GPIO_PIN_GPIO20		20 /* Unrouted */
#define U300_GPIO_PIN_GPIO21		21 /* Unrouted */
#define U300_GPIO_PIN_GPIO22		22 /* Unrouted */
#define U300_GPIO_PIN_GPIO23		23 /* Unrouted */

#define U300_GPIO_PIN_GPIO24		24 /* Unrouted */
#define U300_GPIO_PIN_GPIO25		25 /* Unrouted */
#define U300_GPIO_PIN_GPIO26		26 /* Unrouted */
#define U300_GPIO_PIN_GPIO27		27 /* Unrouted */
#define U300_GPIO_PIN_GPIO28		28 /* Unrouted */
#define U300_GPIO_PIN_GPIO29		29 /* Unrouted */
#define U300_GPIO_PIN_GPIO30		30 /* Unrouted */
#define U300_GPIO_PIN_GPIO31		31 /* Unrouted */

#define U300_GPIO_PIN_GPIO32		32 /* Unrouted */
#define U300_GPIO_PIN_GPIO33		33 /* Unrouted */
#define U300_GPIO_PIN_GPIO34		34 /* Unrouted */
#define U300_GPIO_PIN_GPIO35		35 /* Unrouted */
#define U300_GPIO_PIN_GPIO36		36 /* Unrouted */
#define U300_GPIO_PIN_GPIO37		37 /* Unrouted */
#define U300_GPIO_PIN_GPIO38		38 /* Unrouted */
#define U300_GPIO_PIN_GPIO39		39 /* Unrouted */

#ifdef CONFIG_MACH_U300_BS335

#define U300_GPIO_PIN_GPIO40		40 /* Unrouted */
#define U300_GPIO_PIN_GPIO41		41 /* Unrouted */
#define U300_GPIO_PIN_GPIO42		42 /* Unrouted */
#define U300_GPIO_PIN_GPIO43		43 /* Unrouted */
#define U300_GPIO_PIN_GPIO44		44 /* Unrouted */
#define U300_GPIO_PIN_GPIO45		45 /* Unrouted */
#define U300_GPIO_PIN_GPIO46		46 /* Unrouted */
#define U300_GPIO_PIN_GPIO47		47 /* Unrouted */

#define U300_GPIO_PIN_GPIO48		48 /* Unrouted */
#define U300_GPIO_PIN_GPIO49		49 /* Unrouted */
#define U300_GPIO_PIN_GPIO50		50 /* Unrouted */
#define U300_GPIO_PIN_GPIO51		51 /* Unrouted */
#define U300_GPIO_PIN_GPIO52		52 /* Unrouted */
#define U300_GPIO_PIN_GPIO53		53 /* Unrouted */
#define U300_GPIO_PIN_GPIO54		54 /* Unrouted */
#define U300_GPIO_PIN_GPIO55		55 /* Unrouted */
#endif

#endif
