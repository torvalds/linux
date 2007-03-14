#ifndef BCM43xx_H_
#define BCM43xx_H_

#include <linux/hw_random.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/stringify.h>
#include <linux/pci.h>
#include <net/ieee80211.h>
#include <net/ieee80211softmac.h>
#include <asm/atomic.h>
#include <asm/io.h>


#include "bcm43xx_debugfs.h"
#include "bcm43xx_leds.h"


#define PFX				KBUILD_MODNAME ": "

#define BCM43xx_SWITCH_CORE_MAX_RETRIES	50
#define BCM43xx_IRQWAIT_MAX_RETRIES	100

#define BCM43xx_IO_SIZE			8192

/* Active Core PCI Configuration Register. */
#define BCM43xx_PCICFG_ACTIVE_CORE	0x80
/* SPROM control register. */
#define BCM43xx_PCICFG_SPROMCTL		0x88
/* Interrupt Control PCI Configuration Register. (Only on PCI cores with rev >= 6) */
#define BCM43xx_PCICFG_ICR		0x94

/* MMIO offsets */
#define BCM43xx_MMIO_DMA0_REASON	0x20
#define BCM43xx_MMIO_DMA0_IRQ_MASK	0x24
#define BCM43xx_MMIO_DMA1_REASON	0x28
#define BCM43xx_MMIO_DMA1_IRQ_MASK	0x2C
#define BCM43xx_MMIO_DMA2_REASON	0x30
#define BCM43xx_MMIO_DMA2_IRQ_MASK	0x34
#define BCM43xx_MMIO_DMA3_REASON	0x38
#define BCM43xx_MMIO_DMA3_IRQ_MASK	0x3C
#define BCM43xx_MMIO_DMA4_REASON	0x40
#define BCM43xx_MMIO_DMA4_IRQ_MASK	0x44
#define BCM43xx_MMIO_DMA5_REASON	0x48
#define BCM43xx_MMIO_DMA5_IRQ_MASK	0x4C
#define BCM43xx_MMIO_STATUS_BITFIELD	0x120
#define BCM43xx_MMIO_STATUS2_BITFIELD	0x124
#define BCM43xx_MMIO_GEN_IRQ_REASON	0x128
#define BCM43xx_MMIO_GEN_IRQ_MASK	0x12C
#define BCM43xx_MMIO_RAM_CONTROL	0x130
#define BCM43xx_MMIO_RAM_DATA		0x134
#define BCM43xx_MMIO_PS_STATUS		0x140
#define BCM43xx_MMIO_RADIO_HWENABLED_HI	0x158
#define BCM43xx_MMIO_SHM_CONTROL	0x160
#define BCM43xx_MMIO_SHM_DATA		0x164
#define BCM43xx_MMIO_SHM_DATA_UNALIGNED	0x166
#define BCM43xx_MMIO_XMITSTAT_0		0x170
#define BCM43xx_MMIO_XMITSTAT_1		0x174
#define BCM43xx_MMIO_REV3PLUS_TSF_LOW	0x180 /* core rev >= 3 only */
#define BCM43xx_MMIO_REV3PLUS_TSF_HIGH	0x184 /* core rev >= 3 only */

/* 32-bit DMA */
#define BCM43xx_MMIO_DMA32_BASE0	0x200
#define BCM43xx_MMIO_DMA32_BASE1	0x220
#define BCM43xx_MMIO_DMA32_BASE2	0x240
#define BCM43xx_MMIO_DMA32_BASE3	0x260
#define BCM43xx_MMIO_DMA32_BASE4	0x280
#define BCM43xx_MMIO_DMA32_BASE5	0x2A0
/* 64-bit DMA */
#define BCM43xx_MMIO_DMA64_BASE0	0x200
#define BCM43xx_MMIO_DMA64_BASE1	0x240
#define BCM43xx_MMIO_DMA64_BASE2	0x280
#define BCM43xx_MMIO_DMA64_BASE3	0x2C0
#define BCM43xx_MMIO_DMA64_BASE4	0x300
#define BCM43xx_MMIO_DMA64_BASE5	0x340
/* PIO */
#define BCM43xx_MMIO_PIO1_BASE		0x300
#define BCM43xx_MMIO_PIO2_BASE		0x310
#define BCM43xx_MMIO_PIO3_BASE		0x320
#define BCM43xx_MMIO_PIO4_BASE		0x330

#define BCM43xx_MMIO_PHY_VER		0x3E0
#define BCM43xx_MMIO_PHY_RADIO		0x3E2
#define BCM43xx_MMIO_ANTENNA		0x3E8
#define BCM43xx_MMIO_CHANNEL		0x3F0
#define BCM43xx_MMIO_CHANNEL_EXT	0x3F4
#define BCM43xx_MMIO_RADIO_CONTROL	0x3F6
#define BCM43xx_MMIO_RADIO_DATA_HIGH	0x3F8
#define BCM43xx_MMIO_RADIO_DATA_LOW	0x3FA
#define BCM43xx_MMIO_PHY_CONTROL	0x3FC
#define BCM43xx_MMIO_PHY_DATA		0x3FE
#define BCM43xx_MMIO_MACFILTER_CONTROL	0x420
#define BCM43xx_MMIO_MACFILTER_DATA	0x422
#define BCM43xx_MMIO_RADIO_HWENABLED_LO	0x49A
#define BCM43xx_MMIO_GPIO_CONTROL	0x49C
#define BCM43xx_MMIO_GPIO_MASK		0x49E
#define BCM43xx_MMIO_TSF_0		0x632 /* core rev < 3 only */
#define BCM43xx_MMIO_TSF_1		0x634 /* core rev < 3 only */
#define BCM43xx_MMIO_TSF_2		0x636 /* core rev < 3 only */
#define BCM43xx_MMIO_TSF_3		0x638 /* core rev < 3 only */
#define BCM43xx_MMIO_RNG		0x65A
#define BCM43xx_MMIO_POWERUP_DELAY	0x6A8

/* SPROM offsets. */
#define BCM43xx_SPROM_BASE		0x1000
#define BCM43xx_SPROM_BOARDFLAGS2	0x1c
#define BCM43xx_SPROM_IL0MACADDR	0x24
#define BCM43xx_SPROM_ET0MACADDR	0x27
#define BCM43xx_SPROM_ET1MACADDR	0x2a
#define BCM43xx_SPROM_ETHPHY		0x2d
#define BCM43xx_SPROM_BOARDREV		0x2e
#define BCM43xx_SPROM_PA0B0		0x2f
#define BCM43xx_SPROM_PA0B1		0x30
#define BCM43xx_SPROM_PA0B2		0x31
#define BCM43xx_SPROM_WL0GPIO0		0x32
#define BCM43xx_SPROM_WL0GPIO2		0x33
#define BCM43xx_SPROM_MAXPWR		0x34
#define BCM43xx_SPROM_PA1B0		0x35
#define BCM43xx_SPROM_PA1B1		0x36
#define BCM43xx_SPROM_PA1B2		0x37
#define BCM43xx_SPROM_IDL_TSSI_TGT	0x38
#define BCM43xx_SPROM_BOARDFLAGS	0x39
#define BCM43xx_SPROM_ANTENNA_GAIN	0x3a
#define BCM43xx_SPROM_VERSION		0x3f

/* BCM43xx_SPROM_BOARDFLAGS values */
#define BCM43xx_BFL_BTCOEXIST		0x0001 /* implements Bluetooth coexistance */
#define BCM43xx_BFL_PACTRL		0x0002 /* GPIO 9 controlling the PA */
#define BCM43xx_BFL_AIRLINEMODE		0x0004 /* implements GPIO 13 radio disable indication */
#define BCM43xx_BFL_RSSI		0x0008 /* software calculates nrssi slope. */
#define BCM43xx_BFL_ENETSPI		0x0010 /* has ephy roboswitch spi */
#define BCM43xx_BFL_XTAL_NOSLOW		0x0020 /* no slow clock available */
#define BCM43xx_BFL_CCKHIPWR		0x0040 /* can do high power CCK transmission */
#define BCM43xx_BFL_ENETADM		0x0080 /* has ADMtek switch */
#define BCM43xx_BFL_ENETVLAN		0x0100 /* can do vlan */
#define BCM43xx_BFL_AFTERBURNER		0x0200 /* supports Afterburner mode */
#define BCM43xx_BFL_NOPCI		0x0400 /* leaves PCI floating */
#define BCM43xx_BFL_FEM			0x0800 /* supports the Front End Module */
#define BCM43xx_BFL_EXTLNA		0x1000 /* has an external LNA */
#define BCM43xx_BFL_HGPA		0x2000 /* had high gain PA */
#define BCM43xx_BFL_BTCMOD		0x4000 /* BFL_BTCOEXIST is given in alternate GPIOs */
#define BCM43xx_BFL_ALTIQ		0x8000 /* alternate I/Q settings */

/* GPIO register offset, in both ChipCommon and PCI core. */
#define BCM43xx_GPIO_CONTROL		0x6c

/* SHM Routing */
#define BCM43xx_SHM_SHARED		0x0001
#define BCM43xx_SHM_WIRELESS		0x0002
#define BCM43xx_SHM_PCM			0x0003
#define BCM43xx_SHM_HWMAC		0x0004
#define BCM43xx_SHM_UCODE		0x0300

/* MacFilter offsets. */
#define BCM43xx_MACFILTER_SELF		0x0000
#define BCM43xx_MACFILTER_ASSOC		0x0003

/* Chipcommon registers. */
#define BCM43xx_CHIPCOMMON_CAPABILITIES 	0x04
#define BCM43xx_CHIPCOMMON_CTL			0x28
#define BCM43xx_CHIPCOMMON_PLLONDELAY		0xB0
#define BCM43xx_CHIPCOMMON_FREFSELDELAY		0xB4
#define BCM43xx_CHIPCOMMON_SLOWCLKCTL		0xB8
#define BCM43xx_CHIPCOMMON_SYSCLKCTL		0xC0

/* PCI core specific registers. */
#define BCM43xx_PCICORE_BCAST_ADDR	0x50
#define BCM43xx_PCICORE_BCAST_DATA	0x54
#define BCM43xx_PCICORE_SBTOPCI2	0x108

/* SBTOPCI2 values. */
#define BCM43xx_SBTOPCI2_PREFETCH	0x4
#define BCM43xx_SBTOPCI2_BURST		0x8
#define BCM43xx_SBTOPCI2_MEMREAD_MULTI	0x20

/* PCI-E core registers. */
#define BCM43xx_PCIECORE_REG_ADDR      0x0130
#define BCM43xx_PCIECORE_REG_DATA      0x0134
#define BCM43xx_PCIECORE_MDIO_CTL      0x0128
#define BCM43xx_PCIECORE_MDIO_DATA     0x012C

/* PCI-E registers. */
#define BCM43xx_PCIE_TLP_WORKAROUND    0x0004
#define BCM43xx_PCIE_DLLP_LINKCTL      0x0100

/* PCI-E MDIO bits. */
#define BCM43xx_PCIE_MDIO_ST   0x40000000
#define BCM43xx_PCIE_MDIO_WT   0x10000000
#define BCM43xx_PCIE_MDIO_DEV  22
#define BCM43xx_PCIE_MDIO_REG  18
#define BCM43xx_PCIE_MDIO_TA   0x00020000
#define BCM43xx_PCIE_MDIO_TC   0x0100

/* MDIO devices. */
#define BCM43xx_MDIO_SERDES_RX	0x1F

/* SERDES RX registers. */
#define BCM43xx_SERDES_RXTIMER	0x2
#define BCM43xx_SERDES_CDR	0x6
#define BCM43xx_SERDES_CDR_BW	0x7

/* Chipcommon capabilities. */
#define BCM43xx_CAPABILITIES_PCTL		0x00040000
#define BCM43xx_CAPABILITIES_PLLMASK		0x00030000
#define BCM43xx_CAPABILITIES_PLLSHIFT		16
#define BCM43xx_CAPABILITIES_FLASHMASK		0x00000700
#define BCM43xx_CAPABILITIES_FLASHSHIFT		8
#define BCM43xx_CAPABILITIES_EXTBUSPRESENT	0x00000040
#define BCM43xx_CAPABILITIES_UARTGPIO		0x00000020
#define BCM43xx_CAPABILITIES_UARTCLOCKMASK	0x00000018
#define BCM43xx_CAPABILITIES_UARTCLOCKSHIFT	3
#define BCM43xx_CAPABILITIES_MIPSBIGENDIAN	0x00000004
#define BCM43xx_CAPABILITIES_NRUARTSMASK	0x00000003

/* PowerControl */
#define BCM43xx_PCTL_IN			0xB0
#define BCM43xx_PCTL_OUT		0xB4
#define BCM43xx_PCTL_OUTENABLE		0xB8
#define BCM43xx_PCTL_XTAL_POWERUP	0x40
#define BCM43xx_PCTL_PLL_POWERDOWN	0x80

/* PowerControl Clock Modes */
#define BCM43xx_PCTL_CLK_FAST		0x00
#define BCM43xx_PCTL_CLK_SLOW		0x01
#define BCM43xx_PCTL_CLK_DYNAMIC	0x02

#define BCM43xx_PCTL_FORCE_SLOW		0x0800
#define BCM43xx_PCTL_FORCE_PLL		0x1000
#define BCM43xx_PCTL_DYN_XTAL		0x2000

/* COREIDs */
#define BCM43xx_COREID_CHIPCOMMON	0x800
#define BCM43xx_COREID_ILINE20          0x801
#define BCM43xx_COREID_SDRAM            0x803
#define BCM43xx_COREID_PCI		0x804
#define BCM43xx_COREID_MIPS             0x805
#define BCM43xx_COREID_ETHERNET         0x806
#define BCM43xx_COREID_V90		0x807
#define BCM43xx_COREID_USB11_HOSTDEV    0x80a
#define BCM43xx_COREID_IPSEC            0x80b
#define BCM43xx_COREID_PCMCIA		0x80d
#define BCM43xx_COREID_EXT_IF           0x80f
#define BCM43xx_COREID_80211		0x812
#define BCM43xx_COREID_MIPS_3302        0x816
#define BCM43xx_COREID_USB11_HOST       0x817
#define BCM43xx_COREID_USB11_DEV        0x818
#define BCM43xx_COREID_USB20_HOST       0x819
#define BCM43xx_COREID_USB20_DEV        0x81a
#define BCM43xx_COREID_SDIO_HOST        0x81b
#define BCM43xx_COREID_PCIE		0x820

/* Core Information Registers */
#define BCM43xx_CIR_BASE		0xf00
#define BCM43xx_CIR_SBTPSFLAG		(BCM43xx_CIR_BASE + 0x18)
#define BCM43xx_CIR_SBIMSTATE		(BCM43xx_CIR_BASE + 0x90)
#define BCM43xx_CIR_SBINTVEC		(BCM43xx_CIR_BASE + 0x94)
#define BCM43xx_CIR_SBTMSTATELOW	(BCM43xx_CIR_BASE + 0x98)
#define BCM43xx_CIR_SBTMSTATEHIGH	(BCM43xx_CIR_BASE + 0x9c)
#define BCM43xx_CIR_SBIMCONFIGLOW	(BCM43xx_CIR_BASE + 0xa8)
#define BCM43xx_CIR_SB_ID_HI		(BCM43xx_CIR_BASE + 0xfc)

/* Mask to get the Backplane Flag Number from SBTPSFLAG. */
#define BCM43xx_BACKPLANE_FLAG_NR_MASK	0x3f

/* SBIMCONFIGLOW values/masks. */
#define BCM43xx_SBIMCONFIGLOW_SERVICE_TOUT_MASK		0x00000007
#define BCM43xx_SBIMCONFIGLOW_SERVICE_TOUT_SHIFT	0
#define BCM43xx_SBIMCONFIGLOW_REQUEST_TOUT_MASK		0x00000070
#define BCM43xx_SBIMCONFIGLOW_REQUEST_TOUT_SHIFT	4
#define BCM43xx_SBIMCONFIGLOW_CONNID_MASK		0x00ff0000
#define BCM43xx_SBIMCONFIGLOW_CONNID_SHIFT		16

/* sbtmstatelow state flags */
#define BCM43xx_SBTMSTATELOW_RESET		0x01
#define BCM43xx_SBTMSTATELOW_REJECT		0x02
#define BCM43xx_SBTMSTATELOW_CLOCK		0x10000
#define BCM43xx_SBTMSTATELOW_FORCE_GATE_CLOCK	0x20000
#define BCM43xx_SBTMSTATELOW_G_MODE_ENABLE	0x20000000

/* sbtmstatehigh state flags */
#define BCM43xx_SBTMSTATEHIGH_SERROR		0x00000001
#define BCM43xx_SBTMSTATEHIGH_BUSY		0x00000004
#define BCM43xx_SBTMSTATEHIGH_TIMEOUT		0x00000020
#define BCM43xx_SBTMSTATEHIGH_G_PHY_AVAIL	0x00010000
#define BCM43xx_SBTMSTATEHIGH_A_PHY_AVAIL	0x00020000
#define BCM43xx_SBTMSTATEHIGH_COREFLAGS		0x1FFF0000
#define BCM43xx_SBTMSTATEHIGH_DMA64BIT		0x10000000
#define BCM43xx_SBTMSTATEHIGH_GATEDCLK		0x20000000
#define BCM43xx_SBTMSTATEHIGH_BISTFAILED	0x40000000
#define BCM43xx_SBTMSTATEHIGH_BISTCOMPLETE	0x80000000

/* sbimstate flags */
#define BCM43xx_SBIMSTATE_IB_ERROR		0x20000
#define BCM43xx_SBIMSTATE_TIMEOUT		0x40000

/* PHYVersioning */
#define BCM43xx_PHYTYPE_A		0x00
#define BCM43xx_PHYTYPE_B		0x01
#define BCM43xx_PHYTYPE_G		0x02

/* PHYRegisters */
#define BCM43xx_PHY_ILT_A_CTRL		0x0072
#define BCM43xx_PHY_ILT_A_DATA1		0x0073
#define BCM43xx_PHY_ILT_A_DATA2		0x0074
#define BCM43xx_PHY_G_LO_CONTROL	0x0810
#define BCM43xx_PHY_ILT_G_CTRL		0x0472
#define BCM43xx_PHY_ILT_G_DATA1		0x0473
#define BCM43xx_PHY_ILT_G_DATA2		0x0474
#define BCM43xx_PHY_A_PCTL		0x007B
#define BCM43xx_PHY_G_PCTL		0x0029
#define BCM43xx_PHY_A_CRS		0x0029
#define BCM43xx_PHY_RADIO_BITFIELD	0x0401
#define BCM43xx_PHY_G_CRS		0x0429
#define BCM43xx_PHY_NRSSILT_CTRL	0x0803
#define BCM43xx_PHY_NRSSILT_DATA	0x0804

/* RadioRegisters */
#define BCM43xx_RADIOCTL_ID		0x01

/* StatusBitField */
#define BCM43xx_SBF_MAC_ENABLED		0x00000001
#define BCM43xx_SBF_2			0x00000002 /*FIXME: fix name*/
#define BCM43xx_SBF_CORE_READY		0x00000004
#define BCM43xx_SBF_400			0x00000400 /*FIXME: fix name*/
#define BCM43xx_SBF_4000		0x00004000 /*FIXME: fix name*/
#define BCM43xx_SBF_8000		0x00008000 /*FIXME: fix name*/
#define BCM43xx_SBF_XFER_REG_BYTESWAP	0x00010000
#define BCM43xx_SBF_MODE_NOTADHOC	0x00020000
#define BCM43xx_SBF_MODE_AP		0x00040000
#define BCM43xx_SBF_RADIOREG_LOCK	0x00080000
#define BCM43xx_SBF_MODE_MONITOR	0x00400000
#define BCM43xx_SBF_MODE_PROMISC	0x01000000
#define BCM43xx_SBF_PS1			0x02000000
#define BCM43xx_SBF_PS2			0x04000000
#define BCM43xx_SBF_NO_SSID_BCAST	0x08000000
#define BCM43xx_SBF_TIME_UPDATE		0x10000000
#define BCM43xx_SBF_MODE_G		0x80000000

/* Microcode */
#define BCM43xx_UCODE_REVISION		0x0000
#define BCM43xx_UCODE_PATCHLEVEL	0x0002
#define BCM43xx_UCODE_DATE		0x0004
#define BCM43xx_UCODE_TIME		0x0006
#define BCM43xx_UCODE_STATUS		0x0040

/* MicrocodeFlagsBitfield (addr + lo-word values?)*/
#define BCM43xx_UCODEFLAGS_OFFSET	0x005E

#define BCM43xx_UCODEFLAG_AUTODIV	0x0001
#define BCM43xx_UCODEFLAG_UNKBGPHY	0x0002
#define BCM43xx_UCODEFLAG_UNKBPHY	0x0004
#define BCM43xx_UCODEFLAG_UNKGPHY	0x0020
#define BCM43xx_UCODEFLAG_UNKPACTRL	0x0040
#define BCM43xx_UCODEFLAG_JAPAN		0x0080

/* Hardware Radio Enable masks */
#define BCM43xx_MMIO_RADIO_HWENABLED_HI_MASK (1 << 16)
#define BCM43xx_MMIO_RADIO_HWENABLED_LO_MASK (1 << 4)

/* Generic-Interrupt reasons. */
#define BCM43xx_IRQ_READY		(1 << 0)
#define BCM43xx_IRQ_BEACON		(1 << 1)
#define BCM43xx_IRQ_PS			(1 << 2)
#define BCM43xx_IRQ_REG124		(1 << 5)
#define BCM43xx_IRQ_PMQ			(1 << 6)
#define BCM43xx_IRQ_PIO_WORKAROUND	(1 << 8)
#define BCM43xx_IRQ_XMIT_ERROR		(1 << 11)
#define BCM43xx_IRQ_RX			(1 << 15)
#define BCM43xx_IRQ_SCAN		(1 << 16)
#define BCM43xx_IRQ_NOISE		(1 << 18)
#define BCM43xx_IRQ_XMIT_STATUS		(1 << 29)

#define BCM43xx_IRQ_ALL			0xffffffff
#define BCM43xx_IRQ_INITIAL		(BCM43xx_IRQ_PS |		\
					 BCM43xx_IRQ_REG124 |		\
					 BCM43xx_IRQ_PMQ |		\
					 BCM43xx_IRQ_XMIT_ERROR |	\
					 BCM43xx_IRQ_RX |		\
					 BCM43xx_IRQ_SCAN |		\
					 BCM43xx_IRQ_NOISE |		\
					 BCM43xx_IRQ_XMIT_STATUS)
					 

/* Initial default iw_mode */
#define BCM43xx_INITIAL_IWMODE			IW_MODE_INFRA

/* Bus type PCI. */
#define BCM43xx_BUSTYPE_PCI	0
/* Bus type Silicone Backplane Bus. */
#define BCM43xx_BUSTYPE_SB	1
/* Bus type PCMCIA. */
#define BCM43xx_BUSTYPE_PCMCIA	2

/* Threshold values. */
#define BCM43xx_MIN_RTS_THRESHOLD		1U
#define BCM43xx_MAX_RTS_THRESHOLD		2304U
#define BCM43xx_DEFAULT_RTS_THRESHOLD		BCM43xx_MAX_RTS_THRESHOLD

#define BCM43xx_DEFAULT_SHORT_RETRY_LIMIT	7
#define BCM43xx_DEFAULT_LONG_RETRY_LIMIT	4

/* FIXME: the next line is a guess as to what the maximum RSSI value might be */
#define RX_RSSI_MAX				60

/* Max size of a security key */
#define BCM43xx_SEC_KEYSIZE			16
/* Security algorithms. */
enum {
	BCM43xx_SEC_ALGO_NONE = 0, /* unencrypted, as of TX header. */
	BCM43xx_SEC_ALGO_WEP,
	BCM43xx_SEC_ALGO_UNKNOWN,
	BCM43xx_SEC_ALGO_AES,
	BCM43xx_SEC_ALGO_WEP104,
	BCM43xx_SEC_ALGO_TKIP,
};

#ifdef assert
# undef assert
#endif
#ifdef CONFIG_BCM43XX_DEBUG
#define assert(expr) \
	do {									\
		if (unlikely(!(expr))) {					\
		printk(KERN_ERR PFX "ASSERTION FAILED (%s) at: %s:%d:%s()\n",	\
			#expr, __FILE__, __LINE__, __FUNCTION__);		\
		}								\
	} while (0)
#else
#define assert(expr)	do { /* nothing */ } while (0)
#endif

/* rate limited printk(). */
#ifdef printkl
# undef printkl
#endif
#define printkl(f, x...)  do { if (printk_ratelimit()) printk(f ,##x); } while (0)
/* rate limited printk() for debugging */
#ifdef dprintkl
# undef dprintkl
#endif
#ifdef CONFIG_BCM43XX_DEBUG
# define dprintkl		printkl
#else
# define dprintkl(f, x...)	do { /* nothing */ } while (0)
#endif

/* Helper macro for if branches.
 * An if branch marked with this macro is only taken in DEBUG mode.
 * Example:
 *	if (DEBUG_ONLY(foo == bar)) {
 *		do something
 *	}
 *	In DEBUG mode, the branch will be taken if (foo == bar).
 *	In non-DEBUG mode, the branch will never be taken.
 */
#ifdef DEBUG_ONLY
# undef DEBUG_ONLY
#endif
#ifdef CONFIG_BCM43XX_DEBUG
# define DEBUG_ONLY(x)	(x)
#else
# define DEBUG_ONLY(x)	0
#endif

/* debugging printk() */
#ifdef dprintk
# undef dprintk
#endif
#ifdef CONFIG_BCM43XX_DEBUG
# define dprintk(f, x...)  do { printk(f ,##x); } while (0)
#else
# define dprintk(f, x...)  do { /* nothing */ } while (0)
#endif


struct net_device;
struct pci_dev;
struct bcm43xx_dmaring;
struct bcm43xx_pioqueue;

struct bcm43xx_initval {
	u16 offset;
	u16 size;
	u32 value;
} __attribute__((__packed__));

/* Values for bcm430x_sprominfo.locale */
enum {
	BCM43xx_LOCALE_WORLD = 0,
	BCM43xx_LOCALE_THAILAND,
	BCM43xx_LOCALE_ISRAEL,
	BCM43xx_LOCALE_JORDAN,
	BCM43xx_LOCALE_CHINA,
	BCM43xx_LOCALE_JAPAN,
	BCM43xx_LOCALE_USA_CANADA_ANZ,
	BCM43xx_LOCALE_EUROPE,
	BCM43xx_LOCALE_USA_LOW,
	BCM43xx_LOCALE_JAPAN_HIGH,
	BCM43xx_LOCALE_ALL,
	BCM43xx_LOCALE_NONE,
};

#define BCM43xx_SPROM_SIZE	64 /* in 16-bit words. */
struct bcm43xx_sprominfo {
	u16 boardflags2;
	u8 il0macaddr[6];
	u8 et0macaddr[6];
	u8 et1macaddr[6];
	u8 et0phyaddr:5;
	u8 et1phyaddr:5;
	u8 boardrev;
	u8 locale:4;
	u8 antennas_aphy:2;
	u8 antennas_bgphy:2;
	u16 pa0b0;
	u16 pa0b1;
	u16 pa0b2;
	u8 wl0gpio0;
	u8 wl0gpio1;
	u8 wl0gpio2;
	u8 wl0gpio3;
	u8 maxpower_aphy;
	u8 maxpower_bgphy;
	u16 pa1b0;
	u16 pa1b1;
	u16 pa1b2;
	u8 idle_tssi_tgt_aphy;
	u8 idle_tssi_tgt_bgphy;
	u16 boardflags;
	u16 antennagain_aphy;
	u16 antennagain_bgphy;
};

/* Value pair to measure the LocalOscillator. */
struct bcm43xx_lopair {
	s8 low;
	s8 high;
	u8 used:1;
};
#define BCM43xx_LO_COUNT	(14*4)

struct bcm43xx_phyinfo {
	/* Hardware Data */
	u8 analog;
	u8 type;
	u8 rev;
	u16 antenna_diversity;
	u16 savedpctlreg;
	u16 minlowsig[2];
	u16 minlowsigpos[2];
	u8 connected:1,
	   calibrated:1,
	   is_locked:1, /* used in bcm43xx_phy_{un}lock() */
	   dyn_tssi_tbl:1; /* used in bcm43xx_phy_init_tssi2dbm_table() */
	/* LO Measurement Data.
	 * Use bcm43xx_get_lopair() to get a value.
	 */
	struct bcm43xx_lopair *_lo_pairs;

	/* TSSI to dBm table in use */
	const s8 *tssi2dbm;
	/* idle TSSI value */
	s8 idle_tssi;

	/* Values from bcm43xx_calc_loopback_gain() */
	u16 loopback_gain[2];

	/* PHY lock for core.rev < 3
	 * This lock is only used by bcm43xx_phy_{un}lock()
	 */
	spinlock_t lock;

	/* Firmware. */
	const struct firmware *ucode;
	const struct firmware *pcm;
	const struct firmware *initvals0;
	const struct firmware *initvals1;
};


struct bcm43xx_radioinfo {
	u16 manufact;
	u16 version;
	u8 revision;

	/* Desired TX power in dBm Q5.2 */
	u16 txpower_desired;
	/* TX Power control values. */
	union {
		/* B/G PHY */
		struct {
			u16 baseband_atten;
			u16 radio_atten;
			u16 txctl1;
			u16 txctl2;
		};
		/* A PHY */
		struct {
			u16 txpwr_offset;
		};
	};

	/* Current Interference Mitigation mode */
	int interfmode;
	/* Stack of saved values from the Interference Mitigation code.
	 * Each value in the stack is layed out as follows:
	 * bit 0-11:  offset
	 * bit 12-15: register ID
	 * bit 16-32: value
	 * register ID is: 0x1 PHY, 0x2 Radio, 0x3 ILT
	 */
#define BCM43xx_INTERFSTACK_SIZE	26
	u32 interfstack[BCM43xx_INTERFSTACK_SIZE];

	/* Saved values from the NRSSI Slope calculation */
	s16 nrssi[2];
	s32 nrssislope;
	/* In memory nrssi lookup table. */
	s8 nrssi_lt[64];

	/* current channel */
	u8 channel;
	u8 initial_channel;

	u16 lofcal;

	u16 initval;

	u8 enabled:1;
	/* ACI (adjacent channel interference) flags. */
	u8 aci_enable:1,
	   aci_wlan_automatic:1,
	   aci_hw_rssi:1;
};

/* Data structures for DMA transmission, per 80211 core. */
struct bcm43xx_dma {
	struct bcm43xx_dmaring *tx_ring0;
	struct bcm43xx_dmaring *tx_ring1;
	struct bcm43xx_dmaring *tx_ring2;
	struct bcm43xx_dmaring *tx_ring3;
	struct bcm43xx_dmaring *tx_ring4;
	struct bcm43xx_dmaring *tx_ring5;

	struct bcm43xx_dmaring *rx_ring0;
	struct bcm43xx_dmaring *rx_ring3; /* only available on core.rev < 5 */
};

/* Data structures for PIO transmission, per 80211 core. */
struct bcm43xx_pio {
	struct bcm43xx_pioqueue *queue0;
	struct bcm43xx_pioqueue *queue1;
	struct bcm43xx_pioqueue *queue2;
	struct bcm43xx_pioqueue *queue3;
};

#define BCM43xx_MAX_80211_CORES		2

#ifdef CONFIG_BCM947XX
#define core_offset(bcm) (bcm)->current_core_offset
#else
#define core_offset(bcm) 0
#endif

/* Generic information about a core. */
struct bcm43xx_coreinfo {
	u8 available:1,
	   enabled:1,
	   initialized:1;
	/** core_rev revision number */
	u8 rev;
	/** Index number for _switch_core() */
	u8 index;
	/** core_id ID number */
	u16 id;
	/** Core-specific data. */
	void *priv;
};

/* Additional information for each 80211 core. */
struct bcm43xx_coreinfo_80211 {
	/* PHY device. */
	struct bcm43xx_phyinfo phy;
	/* Radio device. */
	struct bcm43xx_radioinfo radio;
	union {
		/* DMA context. */
		struct bcm43xx_dma dma;
		/* PIO context. */
		struct bcm43xx_pio pio;
	};
};

/* Context information for a noise calculation (Link Quality). */
struct bcm43xx_noise_calculation {
	struct bcm43xx_coreinfo *core_at_start;
	u8 channel_at_start;
	u8 calculation_running:1;
	u8 nr_samples;
	s8 samples[8][4];
};

struct bcm43xx_stats {
	u8 noise;
	struct iw_statistics wstats;
	/* Store the last TX/RX times here for updating the leds. */
	unsigned long last_tx;
	unsigned long last_rx;
};

struct bcm43xx_key {
	u8 enabled:1;
	u8 algorithm;
};

/* Driver initialization status. */
enum {
	BCM43xx_STAT_UNINIT,		/* Uninitialized. */
	BCM43xx_STAT_INITIALIZING,	/* init_board() in progress. */
	BCM43xx_STAT_INITIALIZED,	/* Fully operational. */
	BCM43xx_STAT_SHUTTINGDOWN,	/* free_board() in progress. */
	BCM43xx_STAT_RESTARTING,	/* controller_restart() called. */
};
#define bcm43xx_status(bcm)		atomic_read(&(bcm)->init_status)
#define bcm43xx_set_status(bcm, stat)	do {			\
		atomic_set(&(bcm)->init_status, (stat));	\
		smp_wmb();					\
					} while (0)

/*    *** THEORY OF LOCKING ***
 *
 * We have two different locks in the bcm43xx driver.
 * => bcm->mutex:    General sleeping mutex. Protects struct bcm43xx_private
 *                   and the device registers. This mutex does _not_ protect
 *                   against concurrency from the IRQ handler.
 * => bcm->irq_lock: IRQ spinlock. Protects against IRQ handler concurrency.
 *
 * Please note that, if you only take the irq_lock, you are not protected
 * against concurrency from the periodic work handlers.
 * Most times you want to take _both_ locks.
 */

struct bcm43xx_private {
	struct ieee80211_device *ieee;
	struct ieee80211softmac_device *softmac;

	struct net_device *net_dev;
	struct pci_dev *pci_dev;
	unsigned int irq;

	void __iomem *mmio_addr;

	spinlock_t irq_lock;
	struct mutex mutex;

	/* Driver initialization status BCM43xx_STAT_*** */
	atomic_t init_status;

	u16 was_initialized:1,		/* for PCI suspend/resume. */
	    __using_pio:1,		/* Internal, use bcm43xx_using_pio(). */
	    bad_frames_preempt:1,	/* Use "Bad Frames Preemption" (default off) */
	    reg124_set_0x4:1,		/* Some variable to keep track of IRQ stuff. */
	    short_preamble:1,		/* TRUE, if short preamble is enabled. */
	    firmware_norelease:1,	/* Do not release the firmware. Used on suspend. */
	    radio_hw_enable:1;		/* TRUE if radio is hardware enabled */

	struct bcm43xx_stats stats;

	/* Bus type we are connected to.
	 * This is currently always BCM43xx_BUSTYPE_PCI
	 */
	u8 bustype;
	u64 dma_mask;

	u16 board_vendor;
	u16 board_type;
	u16 board_revision;

	u16 chip_id;
	u8 chip_rev;
	u8 chip_package;

	struct bcm43xx_sprominfo sprom;
#define BCM43xx_NR_LEDS		4
	struct bcm43xx_led leds[BCM43xx_NR_LEDS];
	spinlock_t leds_lock;

	/* The currently active core. */
	struct bcm43xx_coreinfo *current_core;
#ifdef CONFIG_BCM947XX
	/** current core memory offset */
	u32 current_core_offset;
#endif
	struct bcm43xx_coreinfo *active_80211_core;
	/* coreinfo structs for all possible cores follow.
	 * Note that a core might not exist.
	 * So check the coreinfo flags before using it.
	 */
	struct bcm43xx_coreinfo core_chipcommon;
	struct bcm43xx_coreinfo core_pci;
	struct bcm43xx_coreinfo core_80211[ BCM43xx_MAX_80211_CORES ];
	/* Additional information, specific to the 80211 cores. */
	struct bcm43xx_coreinfo_80211 core_80211_ext[ BCM43xx_MAX_80211_CORES ];
	/* Number of available 80211 cores. */
	int nr_80211_available;

	u32 chipcommon_capabilities;

	/* Reason code of the last interrupt. */
	u32 irq_reason;
	u32 dma_reason[6];
	/* saved irq enable/disable state bitfield. */
	u32 irq_savedstate;
	/* Link Quality calculation context. */
	struct bcm43xx_noise_calculation noisecalc;
	/* if > 0 MAC is suspended. if == 0 MAC is enabled. */
	int mac_suspended;

	/* Threshold values. */
	//TODO: The RTS thr has to be _used_. Currently, it is only set via WX.
	u32 rts_threshold;

	/* Interrupt Service Routine tasklet (bottom-half) */
	struct tasklet_struct isr_tasklet;

	/* Periodic tasks */
	struct delayed_work periodic_work;
	unsigned int periodic_state;

	struct work_struct restart_work;

	/* Informational stuff. */
	char nick[IW_ESSID_MAX_SIZE + 1];

	/* encryption/decryption */
	u16 security_offset;
	struct bcm43xx_key key[54];
	u8 default_key_idx;

	/* Random Number Generator. */
	struct hwrng rng;
	char rng_name[20 + 1];

	/* Debugging stuff follows. */
#ifdef CONFIG_BCM43XX_DEBUG
	struct bcm43xx_dfsentry *dfsentry;
#endif
};


static inline
struct bcm43xx_private * bcm43xx_priv(struct net_device *dev)
{
	return ieee80211softmac_priv(dev);
}

struct device;

static inline
struct bcm43xx_private * dev_to_bcm(struct device *dev)
{
	struct net_device *net_dev;
	struct bcm43xx_private *bcm;

	net_dev = dev_get_drvdata(dev);
	bcm = bcm43xx_priv(net_dev);

	return bcm;
}


/* Helper function, which returns a boolean.
 * TRUE, if PIO is used; FALSE, if DMA is used.
 */
#if defined(CONFIG_BCM43XX_DMA) && defined(CONFIG_BCM43XX_PIO)
static inline
int bcm43xx_using_pio(struct bcm43xx_private *bcm)
{
	return bcm->__using_pio;
}
#elif defined(CONFIG_BCM43XX_DMA)
static inline
int bcm43xx_using_pio(struct bcm43xx_private *bcm)
{
	return 0;
}
#elif defined(CONFIG_BCM43XX_PIO)
static inline
int bcm43xx_using_pio(struct bcm43xx_private *bcm)
{
	return 1;
}
#else
# error "Using neither DMA nor PIO? Confused..."
#endif

/* Helper functions to access data structures private to the 80211 cores.
 * Note that we _must_ have an 80211 core mapped when calling
 * any of these functions.
 */
static inline
struct bcm43xx_coreinfo_80211 *
bcm43xx_current_80211_priv(struct bcm43xx_private *bcm)
{
	assert(bcm->current_core->id == BCM43xx_COREID_80211);
	return bcm->current_core->priv;
}
static inline
struct bcm43xx_pio * bcm43xx_current_pio(struct bcm43xx_private *bcm)
{
	assert(bcm43xx_using_pio(bcm));
	return &(bcm43xx_current_80211_priv(bcm)->pio);
}
static inline
struct bcm43xx_dma * bcm43xx_current_dma(struct bcm43xx_private *bcm)
{
	assert(!bcm43xx_using_pio(bcm));
	return &(bcm43xx_current_80211_priv(bcm)->dma);
}
static inline
struct bcm43xx_phyinfo * bcm43xx_current_phy(struct bcm43xx_private *bcm)
{
	return &(bcm43xx_current_80211_priv(bcm)->phy);
}
static inline
struct bcm43xx_radioinfo * bcm43xx_current_radio(struct bcm43xx_private *bcm)
{
	return &(bcm43xx_current_80211_priv(bcm)->radio);
}


static inline
struct bcm43xx_lopair * bcm43xx_get_lopair(struct bcm43xx_phyinfo *phy,
					   u16 radio_attenuation,
					   u16 baseband_attenuation)
{
	return phy->_lo_pairs + (radio_attenuation + 14 * (baseband_attenuation / 2));
}


static inline
u16 bcm43xx_read16(struct bcm43xx_private *bcm, u16 offset)
{
	return ioread16(bcm->mmio_addr + core_offset(bcm) + offset);
}

static inline
void bcm43xx_write16(struct bcm43xx_private *bcm, u16 offset, u16 value)
{
	iowrite16(value, bcm->mmio_addr + core_offset(bcm) + offset);
}

static inline
u32 bcm43xx_read32(struct bcm43xx_private *bcm, u16 offset)
{
	return ioread32(bcm->mmio_addr + core_offset(bcm) + offset);
}

static inline
void bcm43xx_write32(struct bcm43xx_private *bcm, u16 offset, u32 value)
{
	iowrite32(value, bcm->mmio_addr + core_offset(bcm) + offset);
}

static inline
int bcm43xx_pci_read_config16(struct bcm43xx_private *bcm, int offset, u16 *value)
{
	return pci_read_config_word(bcm->pci_dev, offset, value);
}

static inline
int bcm43xx_pci_read_config32(struct bcm43xx_private *bcm, int offset, u32 *value)
{
	return pci_read_config_dword(bcm->pci_dev, offset, value);
}

static inline
int bcm43xx_pci_write_config16(struct bcm43xx_private *bcm, int offset, u16 value)
{
	return pci_write_config_word(bcm->pci_dev, offset, value);
}

static inline
int bcm43xx_pci_write_config32(struct bcm43xx_private *bcm, int offset, u32 value)
{
	return pci_write_config_dword(bcm->pci_dev, offset, value);
}

/** Limit a value between two limits */
#ifdef limit_value
# undef limit_value
#endif
#define limit_value(value, min, max)  \
	({						\
		typeof(value) __value = (value);	\
	 	typeof(value) __min = (min);		\
	 	typeof(value) __max = (max);		\
	 	if (__value < __min)			\
	 		__value = __min;		\
	 	else if (__value > __max)		\
	 		__value = __max;		\
	 	__value;				\
	})

/** Helpers to print MAC addresses. */
#define BCM43xx_MACFMT		"%02x:%02x:%02x:%02x:%02x:%02x"
#define BCM43xx_MACARG(x)	((u8*)(x))[0], ((u8*)(x))[1], \
				((u8*)(x))[2], ((u8*)(x))[3], \
				((u8*)(x))[4], ((u8*)(x))[5]

#endif /* BCM43xx_H_ */
