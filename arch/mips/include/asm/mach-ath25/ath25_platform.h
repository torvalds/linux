/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_ATH25_PLATFORM_H
#define __ASM_MACH_ATH25_PLATFORM_H

#include <linux/etherdevice.h>

/*
 * This is board-specific data that is stored in a "fixed" location in flash.
 * It is shared across operating systems, so it should not be changed lightly.
 * The main reason we need it is in order to extract the ethernet MAC
 * address(es).
 */
struct ath25_boarddata {
	u32 magic;                   /* board data is valid */
#define ATH25_BD_MAGIC 0x35333131    /* "5311", for all 531x/231x platforms */
	u16 cksum;                   /* checksum (starting with BD_REV 2) */
	u16 rev;                     /* revision of this struct */
#define BD_REV 4
	char board_name[64];         /* Name of board */
	u16 major;                   /* Board major number */
	u16 minor;                   /* Board minor number */
	u32 flags;                   /* Board configuration */
#define BD_ENET0        0x00000001   /* ENET0 is stuffed */
#define BD_ENET1        0x00000002   /* ENET1 is stuffed */
#define BD_UART1        0x00000004   /* UART1 is stuffed */
#define BD_UART0        0x00000008   /* UART0 is stuffed (dma) */
#define BD_RSTFACTORY   0x00000010   /* Reset factory defaults stuffed */
#define BD_SYSLED       0x00000020   /* System LED stuffed */
#define BD_EXTUARTCLK   0x00000040   /* External UART clock */
#define BD_CPUFREQ      0x00000080   /* cpu freq is valid in nvram */
#define BD_SYSFREQ      0x00000100   /* sys freq is set in nvram */
#define BD_WLAN0        0x00000200   /* Enable WLAN0 */
#define BD_MEMCAP       0x00000400   /* CAP SDRAM @ mem_cap for testing */
#define BD_DISWATCHDOG  0x00000800   /* disable system watchdog */
#define BD_WLAN1        0x00001000   /* Enable WLAN1 (ar5212) */
#define BD_ISCASPER     0x00002000   /* FLAG for AR2312 */
#define BD_WLAN0_2G_EN  0x00004000   /* FLAG for radio0_2G */
#define BD_WLAN0_5G_EN  0x00008000   /* FLAG for radio0_2G */
#define BD_WLAN1_2G_EN  0x00020000   /* FLAG for radio0_2G */
#define BD_WLAN1_5G_EN  0x00040000   /* FLAG for radio0_2G */
	u16 reset_config_gpio;       /* Reset factory GPIO pin */
	u16 sys_led_gpio;            /* System LED GPIO pin */

	u32 cpu_freq;                /* CPU core frequency in Hz */
	u32 sys_freq;                /* System frequency in Hz */
	u32 cnt_freq;                /* Calculated C0_COUNT frequency */

	u8  wlan0_mac[ETH_ALEN];
	u8  enet0_mac[ETH_ALEN];
	u8  enet1_mac[ETH_ALEN];

	u16 pci_id;                  /* Pseudo PCIID for common code */
	u16 mem_cap;                 /* cap bank1 in MB */

	/* version 3 */
	u8  wlan1_mac[ETH_ALEN];     /* (ar5212) */
};

#define BOARD_CONFIG_BUFSZ		0x1000

/*
 * Platform device information for the Wireless MAC
 */
struct ar231x_board_config {
	u16 devid;

	/* board config data */
	struct ath25_boarddata *config;

	/* radio calibration data */
	const char *radio;
};

#endif /* __ASM_MACH_ATH25_PLATFORM_H */
