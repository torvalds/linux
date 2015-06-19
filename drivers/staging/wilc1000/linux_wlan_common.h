#ifndef LINUX_WLAN_COMMON_H
#define LINUX_WLAN_COMMON_H

enum debug_region {
	Generic_debug = 0,
	Hostapd_debug,
	Hostinf_debug,
	CFG80211_debug,
	Coreconfig_debug,
	Interrupt_debug,
	TX_debug,
	RX_debug,
	Lock_debug,
	Tcp_enhance,
	/*Added by amr - BugID_4720*/
	Spin_debug,

	Init_debug,
	Bus_debug,
	Mem_debug,
	Firmware_debug,
	COMP = 0xFFFFFFFF,
};

#define GENERIC_DBG             (1 << Generic_debug)
#define HOSTAPD_DBG             (1 << Hostapd_debug)
#define HOSTINF_DBG             (1 << Hostinf_debug)
#define CORECONFIG_DBG          (1 << Coreconfig_debug)
#define CFG80211_DBG            (1 << CFG80211_debug)
#define INT_DBG                 (1 << Interrupt_debug)
#define TX_DBG                  (1 << TX_debug)
#define RX_DBG                  (1 << RX_debug)
#define LOCK_DBG                (1 << Lock_debug)
#define TCP_ENH                 (1 << Tcp_enhance)
#define SPIN_DEBUG              (1 << Spin_debug)
#define INIT_DBG                (1 << Init_debug)
#define BUS_DBG                 (1 << Bus_debug)
#define MEM_DBG                 (1 << Mem_debug)
#define FIRM_DBG                (1 << Firmware_debug)

#if defined (WILC_DEBUGFS)
extern int wilc_debugfs_init(void);
extern void wilc_debugfs_remove(void);

extern atomic_t REGION;
extern atomic_t DEBUG_LEVEL;

#define DEBUG           (1 << 0)
#define INFO            (1 << 1)
#define WRN             (1 << 2)
#define ERR             (1 << 3)

#define PRINT_D(region, ...)						\
	do {								\
		if ((atomic_read(&DEBUG_LEVEL) & DEBUG) &&		\
		   ((atomic_read(&REGION)) & (region))) {		\
			printk("DBG [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_INFO(region, ...)						\
	do {								\
		if ((atomic_read(&DEBUG_LEVEL) & INFO) &&		\
		   ((atomic_read(&REGION)) & (region))) {		\
			printk("INFO [%s]", __func__);			\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_WRN(region, ...)						\
	do {								\
		if ((atomic_read(&DEBUG_LEVEL) & WRN) &&		\
		   ((atomic_read(&REGION)) & (region))) {		\
			printk("WRN [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_ER(...)							\
	do {								\
		if ((atomic_read(&DEBUG_LEVEL) & ERR)) {		\
			printk("ERR [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#else

#define REGION  (INIT_DBG | GENERIC_DBG | CFG80211_DBG | FIRM_DBG | HOSTAPD_DBG)

#define DEBUG       1
#define INFO        0
#define WRN         0

#define PRINT_D(region, ...)						\
	do {								\
		if (DEBUG == 1 && ((REGION)&(region))) {		\
			printk("DBG [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_INFO(region, ...)						\
	do {								\
		if (INFO == 1 && ((REGION)&(region))) {			\
			printk("INFO [%s]", __func__);			\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_WRN(region, ...)						\
	do {								\
		if (WRN == 1 && ((REGION)&(region))) {			\
			printk("WRN [%s: %d]", __func__, __LINE__);	\
			printk(__VA_ARGS__);				\
		}							\
	} while (0)

#define PRINT_ER(...)							\
	do {								\
		printk("ERR [%s: %d]", __func__, __LINE__);		\
		printk(__VA_ARGS__);					\
	} while (0)
#endif

#define FN_IN   /* PRINT_D(">>> \n") */
#define FN_OUT  /* PRINT_D("<<<\n") */

#ifdef MEMORY_STATIC
#define LINUX_RX_SIZE	(96 * 1024)
#endif
#define LINUX_TX_SIZE	(64 * 1024)


#define WILC_MULTICAST_TABLE_SIZE	8

#if defined (NM73131_0_BOARD)

#define MODALIAS "wilc_spi"
#define GPIO_NUM	IRQ_WILC1000_GPIO

#elif defined (BEAGLE_BOARD)
	#define SPI_CHANNEL	4

	#if SPI_CHANNEL == 4
		#define MODALIAS	"wilc_spi4"
		#define GPIO_NUM	162
	#else
		#define MODALIAS	"wilc_spi3"
		#define GPIO_NUM	133
	#endif
#elif defined(PANDA_BOARD)
	#define MODALIAS	"WILC_SPI"
	#define GPIO_NUM	139
#elif defined(PLAT_WMS8304)             /* rachel */
	#define MODALIAS	"wilc_spi"
	#define GPIO_NUM	139
#elif defined (PLAT_RKXXXX)
 #define MODALIAS	"WILC_IRQ"
 #define GPIO_NUM	RK30_PIN3_PD2 /* RK30_PIN3_PA1 */
/* RK30_PIN3_PD2 */
/* RK2928_PIN1_PA7 */

#elif defined(CUSTOMER_PLATFORM)
/*
 TODO : specify MODALIAS name and GPIO number. This is certainly necessary for SPI interface.
 *
 * ex)
 * #define MODALIAS  "WILC_SPI"
 * #define GPIO_NUM  139
 */

#else
/* base on SAMA5D3_Xplained Board */
	#define MODALIAS	"WILC_SPI"
	#define GPIO_NUM	0x44
#endif


void linux_wlan_enable_irq(void);
#endif
