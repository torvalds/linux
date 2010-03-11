#ifndef B43_H_
#define B43_H_

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/hw_random.h>
#include <linux/ssb/ssb.h>
#include <net/mac80211.h>

#include "debugfs.h"
#include "leds.h"
#include "rfkill.h"
#include "lo.h"
#include "phy_common.h"


/* The unique identifier of the firmware that's officially supported by
 * this driver version. */
#define B43_SUPPORTED_FIRMWARE_ID	"FW13"


#ifdef CONFIG_B43_DEBUG
# define B43_DEBUG	1
#else
# define B43_DEBUG	0
#endif

#define B43_RX_MAX_SSI			60

/* MMIO offsets */
#define B43_MMIO_DMA0_REASON		0x20
#define B43_MMIO_DMA0_IRQ_MASK		0x24
#define B43_MMIO_DMA1_REASON		0x28
#define B43_MMIO_DMA1_IRQ_MASK		0x2C
#define B43_MMIO_DMA2_REASON		0x30
#define B43_MMIO_DMA2_IRQ_MASK		0x34
#define B43_MMIO_DMA3_REASON		0x38
#define B43_MMIO_DMA3_IRQ_MASK		0x3C
#define B43_MMIO_DMA4_REASON		0x40
#define B43_MMIO_DMA4_IRQ_MASK		0x44
#define B43_MMIO_DMA5_REASON		0x48
#define B43_MMIO_DMA5_IRQ_MASK		0x4C
#define B43_MMIO_MACCTL			0x120	/* MAC control */
#define B43_MMIO_MACCMD			0x124	/* MAC command */
#define B43_MMIO_GEN_IRQ_REASON		0x128
#define B43_MMIO_GEN_IRQ_MASK		0x12C
#define B43_MMIO_RAM_CONTROL		0x130
#define B43_MMIO_RAM_DATA		0x134
#define B43_MMIO_PS_STATUS		0x140
#define B43_MMIO_RADIO_HWENABLED_HI	0x158
#define B43_MMIO_SHM_CONTROL		0x160
#define B43_MMIO_SHM_DATA		0x164
#define B43_MMIO_SHM_DATA_UNALIGNED	0x166
#define B43_MMIO_XMITSTAT_0		0x170
#define B43_MMIO_XMITSTAT_1		0x174
#define B43_MMIO_REV3PLUS_TSF_LOW	0x180	/* core rev >= 3 only */
#define B43_MMIO_REV3PLUS_TSF_HIGH	0x184	/* core rev >= 3 only */
#define B43_MMIO_TSF_CFP_REP		0x188
#define B43_MMIO_TSF_CFP_START		0x18C
#define B43_MMIO_TSF_CFP_MAXDUR		0x190

/* 32-bit DMA */
#define B43_MMIO_DMA32_BASE0		0x200
#define B43_MMIO_DMA32_BASE1		0x220
#define B43_MMIO_DMA32_BASE2		0x240
#define B43_MMIO_DMA32_BASE3		0x260
#define B43_MMIO_DMA32_BASE4		0x280
#define B43_MMIO_DMA32_BASE5		0x2A0
/* 64-bit DMA */
#define B43_MMIO_DMA64_BASE0		0x200
#define B43_MMIO_DMA64_BASE1		0x240
#define B43_MMIO_DMA64_BASE2		0x280
#define B43_MMIO_DMA64_BASE3		0x2C0
#define B43_MMIO_DMA64_BASE4		0x300
#define B43_MMIO_DMA64_BASE5		0x340

/* PIO on core rev < 11 */
#define B43_MMIO_PIO_BASE0		0x300
#define B43_MMIO_PIO_BASE1		0x310
#define B43_MMIO_PIO_BASE2		0x320
#define B43_MMIO_PIO_BASE3		0x330
#define B43_MMIO_PIO_BASE4		0x340
#define B43_MMIO_PIO_BASE5		0x350
#define B43_MMIO_PIO_BASE6		0x360
#define B43_MMIO_PIO_BASE7		0x370
/* PIO on core rev >= 11 */
#define B43_MMIO_PIO11_BASE0		0x200
#define B43_MMIO_PIO11_BASE1		0x240
#define B43_MMIO_PIO11_BASE2		0x280
#define B43_MMIO_PIO11_BASE3		0x2C0
#define B43_MMIO_PIO11_BASE4		0x300
#define B43_MMIO_PIO11_BASE5		0x340

#define B43_MMIO_PHY_VER		0x3E0
#define B43_MMIO_PHY_RADIO		0x3E2
#define B43_MMIO_PHY0			0x3E6
#define B43_MMIO_ANTENNA		0x3E8
#define B43_MMIO_CHANNEL		0x3F0
#define B43_MMIO_CHANNEL_EXT		0x3F4
#define B43_MMIO_RADIO_CONTROL		0x3F6
#define B43_MMIO_RADIO_DATA_HIGH	0x3F8
#define B43_MMIO_RADIO_DATA_LOW		0x3FA
#define B43_MMIO_PHY_CONTROL		0x3FC
#define B43_MMIO_PHY_DATA		0x3FE
#define B43_MMIO_MACFILTER_CONTROL	0x420
#define B43_MMIO_MACFILTER_DATA		0x422
#define B43_MMIO_RCMTA_COUNT		0x43C
#define B43_MMIO_RADIO_HWENABLED_LO	0x49A
#define B43_MMIO_GPIO_CONTROL		0x49C
#define B43_MMIO_GPIO_MASK		0x49E
#define B43_MMIO_TSF_CFP_START_LOW	0x604
#define B43_MMIO_TSF_CFP_START_HIGH	0x606
#define B43_MMIO_TSF_CFP_PRETBTT	0x612
#define B43_MMIO_TSF_0			0x632	/* core rev < 3 only */
#define B43_MMIO_TSF_1			0x634	/* core rev < 3 only */
#define B43_MMIO_TSF_2			0x636	/* core rev < 3 only */
#define B43_MMIO_TSF_3			0x638	/* core rev < 3 only */
#define B43_MMIO_RNG			0x65A
#define B43_MMIO_IFSSLOT		0x684	/* Interframe slot time */
#define B43_MMIO_IFSCTL			0x688 /* Interframe space control */
#define  B43_MMIO_IFSCTL_USE_EDCF	0x0004
#define B43_MMIO_POWERUP_DELAY		0x6A8
#define B43_MMIO_BTCOEX_CTL		0x6B4 /* Bluetooth Coexistence Control */
#define B43_MMIO_BTCOEX_STAT		0x6B6 /* Bluetooth Coexistence Status */
#define B43_MMIO_BTCOEX_TXCTL		0x6B8 /* Bluetooth Coexistence Transmit Control */

/* SPROM boardflags_lo values */
#define B43_BFL_BTCOEXIST		0x0001	/* implements Bluetooth coexistance */
#define B43_BFL_PACTRL			0x0002	/* GPIO 9 controlling the PA */
#define B43_BFL_AIRLINEMODE		0x0004	/* implements GPIO 13 radio disable indication */
#define B43_BFL_RSSI			0x0008	/* software calculates nrssi slope. */
#define B43_BFL_ENETSPI			0x0010	/* has ephy roboswitch spi */
#define B43_BFL_XTAL_NOSLOW		0x0020	/* no slow clock available */
#define B43_BFL_CCKHIPWR		0x0040	/* can do high power CCK transmission */
#define B43_BFL_ENETADM			0x0080	/* has ADMtek switch */
#define B43_BFL_ENETVLAN		0x0100	/* can do vlan */
#define B43_BFL_AFTERBURNER		0x0200	/* supports Afterburner mode */
#define B43_BFL_NOPCI			0x0400	/* leaves PCI floating */
#define B43_BFL_FEM			0x0800	/* supports the Front End Module */
#define B43_BFL_EXTLNA			0x1000	/* has an external LNA */
#define B43_BFL_HGPA			0x2000	/* had high gain PA */
#define B43_BFL_BTCMOD			0x4000	/* BFL_BTCOEXIST is given in alternate GPIOs */
#define B43_BFL_ALTIQ			0x8000	/* alternate I/Q settings */

/* SPROM boardflags_hi values */
#define B43_BFH_NOPA			0x0001	/* has no PA */
#define B43_BFH_RSSIINV			0x0002	/* RSSI uses positive slope (not TSSI) */
#define B43_BFH_PAREF			0x0004	/* uses the PARef LDO */
#define B43_BFH_3TSWITCH		0x0008	/* uses a triple throw switch shared
						 * with bluetooth */
#define B43_BFH_PHASESHIFT		0x0010	/* can support phase shifter */
#define B43_BFH_BUCKBOOST		0x0020	/* has buck/booster */
#define B43_BFH_FEM_BT			0x0040	/* has FEM and switch to share antenna
						 * with bluetooth */

/* GPIO register offset, in both ChipCommon and PCI core. */
#define B43_GPIO_CONTROL		0x6c

/* SHM Routing */
enum {
	B43_SHM_UCODE,		/* Microcode memory */
	B43_SHM_SHARED,		/* Shared memory */
	B43_SHM_SCRATCH,	/* Scratch memory */
	B43_SHM_HW,		/* Internal hardware register */
	B43_SHM_RCMTA,		/* Receive match transmitter address (rev >= 5 only) */
};
/* SHM Routing modifiers */
#define B43_SHM_AUTOINC_R		0x0200	/* Auto-increment address on read */
#define B43_SHM_AUTOINC_W		0x0100	/* Auto-increment address on write */
#define B43_SHM_AUTOINC_RW		(B43_SHM_AUTOINC_R | \
					 B43_SHM_AUTOINC_W)

/* Misc SHM_SHARED offsets */
#define B43_SHM_SH_WLCOREREV		0x0016	/* 802.11 core revision */
#define B43_SHM_SH_PCTLWDPOS		0x0008
#define B43_SHM_SH_RXPADOFF		0x0034	/* RX Padding data offset (PIO only) */
#define B43_SHM_SH_FWCAPA		0x0042	/* Firmware capabilities (Opensource firmware only) */
#define B43_SHM_SH_PHYVER		0x0050	/* PHY version */
#define B43_SHM_SH_PHYTYPE		0x0052	/* PHY type */
#define B43_SHM_SH_ANTSWAP		0x005C	/* Antenna swap threshold */
#define B43_SHM_SH_HOSTFLO		0x005E	/* Hostflags for ucode options (low) */
#define B43_SHM_SH_HOSTFMI		0x0060	/* Hostflags for ucode options (middle) */
#define B43_SHM_SH_HOSTFHI		0x0062	/* Hostflags for ucode options (high) */
#define B43_SHM_SH_RFATT		0x0064	/* Current radio attenuation value */
#define B43_SHM_SH_RADAR		0x0066	/* Radar register */
#define B43_SHM_SH_PHYTXNOI		0x006E	/* PHY noise directly after TX (lower 8bit only) */
#define B43_SHM_SH_RFRXSP1		0x0072	/* RF RX SP Register 1 */
#define B43_SHM_SH_CHAN			0x00A0	/* Current channel (low 8bit only) */
#define  B43_SHM_SH_CHAN_5GHZ		0x0100	/* Bit set, if 5Ghz channel */
#define B43_SHM_SH_BCMCFIFOID		0x0108	/* Last posted cookie to the bcast/mcast FIFO */
/* TSSI information */
#define B43_SHM_SH_TSSI_CCK		0x0058	/* TSSI for last 4 CCK frames (32bit) */
#define B43_SHM_SH_TSSI_OFDM_A		0x0068	/* TSSI for last 4 OFDM frames (32bit) */
#define B43_SHM_SH_TSSI_OFDM_G		0x0070	/* TSSI for last 4 OFDM frames (32bit) */
#define  B43_TSSI_MAX			0x7F	/* Max value for one TSSI value */
/* SHM_SHARED TX FIFO variables */
#define B43_SHM_SH_SIZE01		0x0098	/* TX FIFO size for FIFO 0 (low) and 1 (high) */
#define B43_SHM_SH_SIZE23		0x009A	/* TX FIFO size for FIFO 2 and 3 */
#define B43_SHM_SH_SIZE45		0x009C	/* TX FIFO size for FIFO 4 and 5 */
#define B43_SHM_SH_SIZE67		0x009E	/* TX FIFO size for FIFO 6 and 7 */
/* SHM_SHARED background noise */
#define B43_SHM_SH_JSSI0		0x0088	/* Measure JSSI 0 */
#define B43_SHM_SH_JSSI1		0x008A	/* Measure JSSI 1 */
#define B43_SHM_SH_JSSIAUX		0x008C	/* Measure JSSI AUX */
/* SHM_SHARED crypto engine */
#define B43_SHM_SH_DEFAULTIV		0x003C	/* Default IV location */
#define B43_SHM_SH_NRRXTRANS		0x003E	/* # of soft RX transmitter addresses (max 8) */
#define B43_SHM_SH_KTP			0x0056	/* Key table pointer */
#define B43_SHM_SH_TKIPTSCTTAK		0x0318
#define B43_SHM_SH_KEYIDXBLOCK		0x05D4	/* Key index/algorithm block (v4 firmware) */
#define B43_SHM_SH_PSM			0x05F4	/* PSM transmitter address match block (rev < 5) */
/* SHM_SHARED WME variables */
#define B43_SHM_SH_EDCFSTAT		0x000E	/* EDCF status */
#define B43_SHM_SH_TXFCUR		0x0030	/* TXF current index */
#define B43_SHM_SH_EDCFQ		0x0240	/* EDCF Q info */
/* SHM_SHARED powersave mode related */
#define B43_SHM_SH_SLOTT		0x0010	/* Slot time */
#define B43_SHM_SH_DTIMPER		0x0012	/* DTIM period */
#define B43_SHM_SH_NOSLPZNATDTIM	0x004C	/* NOSLPZNAT DTIM */
/* SHM_SHARED beacon/AP variables */
#define B43_SHM_SH_BTL0			0x0018	/* Beacon template length 0 */
#define B43_SHM_SH_BTL1			0x001A	/* Beacon template length 1 */
#define B43_SHM_SH_BTSFOFF		0x001C	/* Beacon TSF offset */
#define B43_SHM_SH_TIMBPOS		0x001E	/* TIM B position in beacon */
#define B43_SHM_SH_DTIMP		0x0012	/* DTIP period */
#define B43_SHM_SH_MCASTCOOKIE		0x00A8	/* Last bcast/mcast frame ID */
#define B43_SHM_SH_SFFBLIM		0x0044	/* Short frame fallback retry limit */
#define B43_SHM_SH_LFFBLIM		0x0046	/* Long frame fallback retry limit */
#define B43_SHM_SH_BEACPHYCTL		0x0054	/* Beacon PHY TX control word (see PHY TX control) */
#define B43_SHM_SH_EXTNPHYCTL		0x00B0	/* Extended bytes for beacon PHY control (N) */
/* SHM_SHARED ACK/CTS control */
#define B43_SHM_SH_ACKCTSPHYCTL		0x0022	/* ACK/CTS PHY control word (see PHY TX control) */
/* SHM_SHARED probe response variables */
#define B43_SHM_SH_PRSSID		0x0160	/* Probe Response SSID */
#define B43_SHM_SH_PRSSIDLEN		0x0048	/* Probe Response SSID length */
#define B43_SHM_SH_PRTLEN		0x004A	/* Probe Response template length */
#define B43_SHM_SH_PRMAXTIME		0x0074	/* Probe Response max time */
#define B43_SHM_SH_PRPHYCTL		0x0188	/* Probe Response PHY TX control word */
/* SHM_SHARED rate tables */
#define B43_SHM_SH_OFDMDIRECT		0x01C0	/* Pointer to OFDM direct map */
#define B43_SHM_SH_OFDMBASIC		0x01E0	/* Pointer to OFDM basic rate map */
#define B43_SHM_SH_CCKDIRECT		0x0200	/* Pointer to CCK direct map */
#define B43_SHM_SH_CCKBASIC		0x0220	/* Pointer to CCK basic rate map */
/* SHM_SHARED microcode soft registers */
#define B43_SHM_SH_UCODEREV		0x0000	/* Microcode revision */
#define B43_SHM_SH_UCODEPATCH		0x0002	/* Microcode patchlevel */
#define B43_SHM_SH_UCODEDATE		0x0004	/* Microcode date */
#define B43_SHM_SH_UCODETIME		0x0006	/* Microcode time */
#define B43_SHM_SH_UCODESTAT		0x0040	/* Microcode debug status code */
#define  B43_SHM_SH_UCODESTAT_INVALID	0
#define  B43_SHM_SH_UCODESTAT_INIT	1
#define  B43_SHM_SH_UCODESTAT_ACTIVE	2
#define  B43_SHM_SH_UCODESTAT_SUSP	3	/* suspended */
#define  B43_SHM_SH_UCODESTAT_SLEEP	4	/* asleep (PS) */
#define B43_SHM_SH_MAXBFRAMES		0x0080	/* Maximum number of frames in a burst */
#define B43_SHM_SH_SPUWKUP		0x0094	/* pre-wakeup for synth PU in us */
#define B43_SHM_SH_PRETBTT		0x0096	/* pre-TBTT in us */

/* SHM_SCRATCH offsets */
#define B43_SHM_SC_MINCONT		0x0003	/* Minimum contention window */
#define B43_SHM_SC_MAXCONT		0x0004	/* Maximum contention window */
#define B43_SHM_SC_CURCONT		0x0005	/* Current contention window */
#define B43_SHM_SC_SRLIMIT		0x0006	/* Short retry count limit */
#define B43_SHM_SC_LRLIMIT		0x0007	/* Long retry count limit */
#define B43_SHM_SC_DTIMC		0x0008	/* Current DTIM count */
#define B43_SHM_SC_BTL0LEN		0x0015	/* Beacon 0 template length */
#define B43_SHM_SC_BTL1LEN		0x0016	/* Beacon 1 template length */
#define B43_SHM_SC_SCFB			0x0017	/* Short frame transmit count threshold for rate fallback */
#define B43_SHM_SC_LCFB			0x0018	/* Long frame transmit count threshold for rate fallback */

/* Hardware Radio Enable masks */
#define B43_MMIO_RADIO_HWENABLED_HI_MASK (1 << 16)
#define B43_MMIO_RADIO_HWENABLED_LO_MASK (1 << 4)

/* HostFlags. See b43_hf_read/write() */
#define B43_HF_ANTDIVHELP	0x000000000001ULL /* ucode antenna div helper */
#define B43_HF_SYMW		0x000000000002ULL /* G-PHY SYM workaround */
#define B43_HF_RXPULLW		0x000000000004ULL /* RX pullup workaround */
#define B43_HF_CCKBOOST		0x000000000008ULL /* 4dB CCK power boost (exclusive with OFDM boost) */
#define B43_HF_BTCOEX		0x000000000010ULL /* Bluetooth coexistance */
#define B43_HF_GDCW		0x000000000020ULL /* G-PHY DC canceller filter bw workaround */
#define B43_HF_OFDMPABOOST	0x000000000040ULL /* Enable PA gain boost for OFDM */
#define B43_HF_ACPR		0x000000000080ULL /* Disable for Japan, channel 14 */
#define B43_HF_EDCF		0x000000000100ULL /* on if WME and MAC suspended */
#define B43_HF_TSSIRPSMW	0x000000000200ULL /* TSSI reset PSM ucode workaround */
#define B43_HF_20IN40IQW	0x000000000200ULL /* 20 in 40 MHz I/Q workaround (rev >= 13 only) */
#define B43_HF_DSCRQ		0x000000000400ULL /* Disable slow clock request in ucode */
#define B43_HF_ACIW		0x000000000800ULL /* ACI workaround: shift bits by 2 on PHY CRS */
#define B43_HF_2060W		0x000000001000ULL /* 2060 radio workaround */
#define B43_HF_RADARW		0x000000002000ULL /* Radar workaround */
#define B43_HF_USEDEFKEYS	0x000000004000ULL /* Enable use of default keys */
#define B43_HF_AFTERBURNER	0x000000008000ULL /* Afterburner enabled */
#define B43_HF_BT4PRIOCOEX	0x000000010000ULL /* Bluetooth 4-priority coexistance */
#define B43_HF_FWKUP		0x000000020000ULL /* Fast wake-up ucode */
#define B43_HF_VCORECALC	0x000000040000ULL /* Force VCO recalculation when powering up synthpu */
#define B43_HF_PCISCW		0x000000080000ULL /* PCI slow clock workaround */
#define B43_HF_4318TSSI		0x000000200000ULL /* 4318 TSSI */
#define B43_HF_FBCMCFIFO	0x000000400000ULL /* Flush bcast/mcast FIFO immediately */
#define B43_HF_HWPCTL		0x000000800000ULL /* Enable hardwarre power control */
#define B43_HF_BTCOEXALT	0x000001000000ULL /* Bluetooth coexistance in alternate pins */
#define B43_HF_TXBTCHECK	0x000002000000ULL /* Bluetooth check during transmission */
#define B43_HF_SKCFPUP		0x000004000000ULL /* Skip CFP update */
#define B43_HF_N40W		0x000008000000ULL /* N PHY 40 MHz workaround (rev >= 13 only) */
#define B43_HF_ANTSEL		0x000020000000ULL /* Antenna selection (for testing antenna div.) */
#define B43_HF_BT3COEXT		0x000020000000ULL /* Bluetooth 3-wire coexistence (rev >= 13 only) */
#define B43_HF_BTCANT		0x000040000000ULL /* Bluetooth coexistence (antenna mode) (rev >= 13 only) */
#define B43_HF_ANTSELEN		0x000100000000ULL /* Antenna selection enabled (rev >= 13 only) */
#define B43_HF_ANTSELMODE	0x000200000000ULL /* Antenna selection mode (rev >= 13 only) */
#define B43_HF_MLADVW		0x001000000000ULL /* N PHY ML ADV workaround (rev >= 13 only) */
#define B43_HF_PR45960W		0x080000000000ULL /* PR 45960 workaround (rev >= 13 only) */

/* Firmware capabilities field in SHM (Opensource firmware only) */
#define B43_FWCAPA_HWCRYPTO	0x0001
#define B43_FWCAPA_QOS		0x0002

/* MacFilter offsets. */
#define B43_MACFILTER_SELF		0x0000
#define B43_MACFILTER_BSSID		0x0003

/* PowerControl */
#define B43_PCTL_IN			0xB0
#define B43_PCTL_OUT			0xB4
#define B43_PCTL_OUTENABLE		0xB8
#define B43_PCTL_XTAL_POWERUP		0x40
#define B43_PCTL_PLL_POWERDOWN		0x80

/* PowerControl Clock Modes */
#define B43_PCTL_CLK_FAST		0x00
#define B43_PCTL_CLK_SLOW		0x01
#define B43_PCTL_CLK_DYNAMIC		0x02

#define B43_PCTL_FORCE_SLOW		0x0800
#define B43_PCTL_FORCE_PLL		0x1000
#define B43_PCTL_DYN_XTAL		0x2000

/* PHYVersioning */
#define B43_PHYTYPE_A			0x00
#define B43_PHYTYPE_B			0x01
#define B43_PHYTYPE_G			0x02
#define B43_PHYTYPE_N			0x04
#define B43_PHYTYPE_LP			0x05

/* PHYRegisters */
#define B43_PHY_ILT_A_CTRL		0x0072
#define B43_PHY_ILT_A_DATA1		0x0073
#define B43_PHY_ILT_A_DATA2		0x0074
#define B43_PHY_G_LO_CONTROL		0x0810
#define B43_PHY_ILT_G_CTRL		0x0472
#define B43_PHY_ILT_G_DATA1		0x0473
#define B43_PHY_ILT_G_DATA2		0x0474
#define B43_PHY_A_PCTL			0x007B
#define B43_PHY_G_PCTL			0x0029
#define B43_PHY_A_CRS			0x0029
#define B43_PHY_RADIO_BITFIELD		0x0401
#define B43_PHY_G_CRS			0x0429
#define B43_PHY_NRSSILT_CTRL		0x0803
#define B43_PHY_NRSSILT_DATA		0x0804

/* RadioRegisters */
#define B43_RADIOCTL_ID			0x01

/* MAC Control bitfield */
#define B43_MACCTL_ENABLED		0x00000001	/* MAC Enabled */
#define B43_MACCTL_PSM_RUN		0x00000002	/* Run Microcode */
#define B43_MACCTL_PSM_JMP0		0x00000004	/* Microcode jump to 0 */
#define B43_MACCTL_SHM_ENABLED		0x00000100	/* SHM Enabled */
#define B43_MACCTL_SHM_UPPER		0x00000200	/* SHM Upper */
#define B43_MACCTL_IHR_ENABLED		0x00000400	/* IHR Region Enabled */
#define B43_MACCTL_PSM_DBG		0x00002000	/* Microcode debugging enabled */
#define B43_MACCTL_GPOUTSMSK		0x0000C000	/* GPOUT Select Mask */
#define B43_MACCTL_BE			0x00010000	/* Big Endian mode */
#define B43_MACCTL_INFRA		0x00020000	/* Infrastructure mode */
#define B43_MACCTL_AP			0x00040000	/* AccessPoint mode */
#define B43_MACCTL_RADIOLOCK		0x00080000	/* Radio lock */
#define B43_MACCTL_BEACPROMISC		0x00100000	/* Beacon Promiscuous */
#define B43_MACCTL_KEEP_BADPLCP		0x00200000	/* Keep frames with bad PLCP */
#define B43_MACCTL_KEEP_CTL		0x00400000	/* Keep control frames */
#define B43_MACCTL_KEEP_BAD		0x00800000	/* Keep bad frames (FCS) */
#define B43_MACCTL_PROMISC		0x01000000	/* Promiscuous mode */
#define B43_MACCTL_HWPS			0x02000000	/* Hardware Power Saving */
#define B43_MACCTL_AWAKE		0x04000000	/* Device is awake */
#define B43_MACCTL_CLOSEDNET		0x08000000	/* Closed net (no SSID bcast) */
#define B43_MACCTL_TBTTHOLD		0x10000000	/* TBTT Hold */
#define B43_MACCTL_DISCTXSTAT		0x20000000	/* Discard TX status */
#define B43_MACCTL_DISCPMQ		0x40000000	/* Discard Power Management Queue */
#define B43_MACCTL_GMODE		0x80000000	/* G Mode */

/* MAC Command bitfield */
#define B43_MACCMD_BEACON0_VALID	0x00000001	/* Beacon 0 in template RAM is busy/valid */
#define B43_MACCMD_BEACON1_VALID	0x00000002	/* Beacon 1 in template RAM is busy/valid */
#define B43_MACCMD_DFQ_VALID		0x00000004	/* Directed frame queue valid (IBSS PS mode, ATIM) */
#define B43_MACCMD_CCA			0x00000008	/* Clear channel assessment */
#define B43_MACCMD_BGNOISE		0x00000010	/* Background noise */

/* 802.11 core specific TM State Low (SSB_TMSLOW) flags */
#define B43_TMSLOW_GMODE		0x20000000	/* G Mode Enable */
#define B43_TMSLOW_PHYCLKSPEED		0x00C00000	/* PHY clock speed mask (N-PHY only) */
#define  B43_TMSLOW_PHYCLKSPEED_40MHZ	0x00000000	/* 40 MHz PHY */
#define  B43_TMSLOW_PHYCLKSPEED_80MHZ	0x00400000	/* 80 MHz PHY */
#define  B43_TMSLOW_PHYCLKSPEED_160MHZ	0x00800000	/* 160 MHz PHY */
#define B43_TMSLOW_PLLREFSEL		0x00200000	/* PLL Frequency Reference Select (rev >= 5) */
#define B43_TMSLOW_MACPHYCLKEN		0x00100000	/* MAC PHY Clock Control Enable (rev >= 5) */
#define B43_TMSLOW_PHYRESET		0x00080000	/* PHY Reset */
#define B43_TMSLOW_PHYCLKEN		0x00040000	/* PHY Clock Enable */

/* 802.11 core specific TM State High (SSB_TMSHIGH) flags */
#define B43_TMSHIGH_DUALBAND_PHY	0x00080000	/* Dualband PHY available */
#define B43_TMSHIGH_FCLOCK		0x00040000	/* Fast Clock Available (rev >= 5) */
#define B43_TMSHIGH_HAVE_5GHZ_PHY	0x00020000	/* 5 GHz PHY available (rev >= 5) */
#define B43_TMSHIGH_HAVE_2GHZ_PHY	0x00010000	/* 2.4 GHz PHY available (rev >= 5) */

/* Generic-Interrupt reasons. */
#define B43_IRQ_MAC_SUSPENDED		0x00000001
#define B43_IRQ_BEACON			0x00000002
#define B43_IRQ_TBTT_INDI		0x00000004
#define B43_IRQ_BEACON_TX_OK		0x00000008
#define B43_IRQ_BEACON_CANCEL		0x00000010
#define B43_IRQ_ATIM_END		0x00000020
#define B43_IRQ_PMQ			0x00000040
#define B43_IRQ_PIO_WORKAROUND		0x00000100
#define B43_IRQ_MAC_TXERR		0x00000200
#define B43_IRQ_PHY_TXERR		0x00000800
#define B43_IRQ_PMEVENT			0x00001000
#define B43_IRQ_TIMER0			0x00002000
#define B43_IRQ_TIMER1			0x00004000
#define B43_IRQ_DMA			0x00008000
#define B43_IRQ_TXFIFO_FLUSH_OK		0x00010000
#define B43_IRQ_CCA_MEASURE_OK		0x00020000
#define B43_IRQ_NOISESAMPLE_OK		0x00040000
#define B43_IRQ_UCODE_DEBUG		0x08000000
#define B43_IRQ_RFKILL			0x10000000
#define B43_IRQ_TX_OK			0x20000000
#define B43_IRQ_PHY_G_CHANGED		0x40000000
#define B43_IRQ_TIMEOUT			0x80000000

#define B43_IRQ_ALL			0xFFFFFFFF
#define B43_IRQ_MASKTEMPLATE		(B43_IRQ_TBTT_INDI | \
					 B43_IRQ_ATIM_END | \
					 B43_IRQ_PMQ | \
					 B43_IRQ_MAC_TXERR | \
					 B43_IRQ_PHY_TXERR | \
					 B43_IRQ_DMA | \
					 B43_IRQ_TXFIFO_FLUSH_OK | \
					 B43_IRQ_NOISESAMPLE_OK | \
					 B43_IRQ_UCODE_DEBUG | \
					 B43_IRQ_RFKILL | \
					 B43_IRQ_TX_OK)

/* The firmware register to fetch the debug-IRQ reason from. */
#define B43_DEBUGIRQ_REASON_REG		63
/* Debug-IRQ reasons. */
#define B43_DEBUGIRQ_PANIC		0	/* The firmware panic'ed */
#define B43_DEBUGIRQ_DUMP_SHM		1	/* Dump shared SHM */
#define B43_DEBUGIRQ_DUMP_REGS		2	/* Dump the microcode registers */
#define B43_DEBUGIRQ_MARKER		3	/* A "marker" was thrown by the firmware. */
#define B43_DEBUGIRQ_ACK		0xFFFF	/* The host writes that to ACK the IRQ */

/* The firmware register that contains the "marker" line. */
#define B43_MARKER_ID_REG		2
#define B43_MARKER_LINE_REG		3

/* The firmware register to fetch the panic reason from. */
#define B43_FWPANIC_REASON_REG		3
/* Firmware panic reason codes */
#define B43_FWPANIC_DIE			0 /* Firmware died. Don't auto-restart it. */
#define B43_FWPANIC_RESTART		1 /* Firmware died. Schedule a controller reset. */

/* The firmware register that contains the watchdog counter. */
#define B43_WATCHDOG_REG		1

/* Device specific rate values.
 * The actual values defined here are (rate_in_mbps * 2).
 * Some code depends on this. Don't change it. */
#define B43_CCK_RATE_1MB		0x02
#define B43_CCK_RATE_2MB		0x04
#define B43_CCK_RATE_5MB		0x0B
#define B43_CCK_RATE_11MB		0x16
#define B43_OFDM_RATE_6MB		0x0C
#define B43_OFDM_RATE_9MB		0x12
#define B43_OFDM_RATE_12MB		0x18
#define B43_OFDM_RATE_18MB		0x24
#define B43_OFDM_RATE_24MB		0x30
#define B43_OFDM_RATE_36MB		0x48
#define B43_OFDM_RATE_48MB		0x60
#define B43_OFDM_RATE_54MB		0x6C
/* Convert a b43 rate value to a rate in 100kbps */
#define B43_RATE_TO_BASE100KBPS(rate)	(((rate) * 10) / 2)

#define B43_DEFAULT_SHORT_RETRY_LIMIT	7
#define B43_DEFAULT_LONG_RETRY_LIMIT	4

#define B43_PHY_TX_BADNESS_LIMIT	1000

/* Max size of a security key */
#define B43_SEC_KEYSIZE			16
/* Max number of group keys */
#define B43_NR_GROUP_KEYS		4
/* Max number of pairwise keys */
#define B43_NR_PAIRWISE_KEYS		50
/* Security algorithms. */
enum {
	B43_SEC_ALGO_NONE = 0,	/* unencrypted, as of TX header. */
	B43_SEC_ALGO_WEP40,
	B43_SEC_ALGO_TKIP,
	B43_SEC_ALGO_AES,
	B43_SEC_ALGO_WEP104,
	B43_SEC_ALGO_AES_LEGACY,
};

struct b43_dmaring;

/* The firmware file header */
#define B43_FW_TYPE_UCODE	'u'
#define B43_FW_TYPE_PCM		'p'
#define B43_FW_TYPE_IV		'i'
struct b43_fw_header {
	/* File type */
	u8 type;
	/* File format version */
	u8 ver;
	u8 __padding[2];
	/* Size of the data. For ucode and PCM this is in bytes.
	 * For IV this is number-of-ivs. */
	__be32 size;
} __attribute__((__packed__));

/* Initial Value file format */
#define B43_IV_OFFSET_MASK	0x7FFF
#define B43_IV_32BIT		0x8000
struct b43_iv {
	__be16 offset_size;
	union {
		__be16 d16;
		__be32 d32;
	} data __attribute__((__packed__));
} __attribute__((__packed__));


/* Data structures for DMA transmission, per 80211 core. */
struct b43_dma {
	struct b43_dmaring *tx_ring_AC_BK; /* Background */
	struct b43_dmaring *tx_ring_AC_BE; /* Best Effort */
	struct b43_dmaring *tx_ring_AC_VI; /* Video */
	struct b43_dmaring *tx_ring_AC_VO; /* Voice */
	struct b43_dmaring *tx_ring_mcast; /* Multicast */

	struct b43_dmaring *rx_ring;
};

struct b43_pio_txqueue;
struct b43_pio_rxqueue;

/* Data structures for PIO transmission, per 80211 core. */
struct b43_pio {
	struct b43_pio_txqueue *tx_queue_AC_BK; /* Background */
	struct b43_pio_txqueue *tx_queue_AC_BE; /* Best Effort */
	struct b43_pio_txqueue *tx_queue_AC_VI; /* Video */
	struct b43_pio_txqueue *tx_queue_AC_VO; /* Voice */
	struct b43_pio_txqueue *tx_queue_mcast; /* Multicast */

	struct b43_pio_rxqueue *rx_queue;
};

/* Context information for a noise calculation (Link Quality). */
struct b43_noise_calculation {
	bool calculation_running;
	u8 nr_samples;
	s8 samples[8][4];
};

struct b43_stats {
	u8 link_noise;
};

struct b43_key {
	/* If keyconf is NULL, this key is disabled.
	 * keyconf is a cookie. Don't derefenrence it outside of the set_key
	 * path, because b43 doesn't own it. */
	struct ieee80211_key_conf *keyconf;
	u8 algorithm;
};

/* SHM offsets to the QOS data structures for the 4 different queues. */
#define B43_QOS_PARAMS(queue)	(B43_SHM_SH_EDCFQ + \
				 (B43_NR_QOSPARAMS * sizeof(u16) * (queue)))
#define B43_QOS_BACKGROUND	B43_QOS_PARAMS(0)
#define B43_QOS_BESTEFFORT	B43_QOS_PARAMS(1)
#define B43_QOS_VIDEO		B43_QOS_PARAMS(2)
#define B43_QOS_VOICE		B43_QOS_PARAMS(3)

/* QOS parameter hardware data structure offsets. */
#define B43_NR_QOSPARAMS	16
enum {
	B43_QOSPARAM_TXOP = 0,
	B43_QOSPARAM_CWMIN,
	B43_QOSPARAM_CWMAX,
	B43_QOSPARAM_CWCUR,
	B43_QOSPARAM_AIFS,
	B43_QOSPARAM_BSLOTS,
	B43_QOSPARAM_REGGAP,
	B43_QOSPARAM_STATUS,
};

/* QOS parameters for a queue. */
struct b43_qos_params {
	/* The QOS parameters */
	struct ieee80211_tx_queue_params p;
};

struct b43_wl;

/* The type of the firmware file. */
enum b43_firmware_file_type {
	B43_FWTYPE_PROPRIETARY,
	B43_FWTYPE_OPENSOURCE,
	B43_NR_FWTYPES,
};

/* Context data for fetching firmware. */
struct b43_request_fw_context {
	/* The device we are requesting the fw for. */
	struct b43_wldev *dev;
	/* The type of firmware to request. */
	enum b43_firmware_file_type req_type;
	/* Error messages for each firmware type. */
	char errors[B43_NR_FWTYPES][128];
	/* Temporary buffer for storing the firmware name. */
	char fwname[64];
	/* A fatal error occured while requesting. Firmware reqest
	 * can not continue, as any other reqest will also fail. */
	int fatal_failure;
};

/* In-memory representation of a cached microcode file. */
struct b43_firmware_file {
	const char *filename;
	const struct firmware *data;
	/* Type of the firmware file name. Note that this does only indicate
	 * the type by the firmware name. NOT the file contents.
	 * If you want to check for proprietary vs opensource, use (struct b43_firmware)->opensource
	 * instead! The (struct b43_firmware)->opensource flag is derived from the actual firmware
	 * binary code, not just the filename.
	 */
	enum b43_firmware_file_type type;
};

/* Pointers to the firmware data and meta information about it. */
struct b43_firmware {
	/* Microcode */
	struct b43_firmware_file ucode;
	/* PCM code */
	struct b43_firmware_file pcm;
	/* Initial MMIO values for the firmware */
	struct b43_firmware_file initvals;
	/* Initial MMIO values for the firmware, band-specific */
	struct b43_firmware_file initvals_band;

	/* Firmware revision */
	u16 rev;
	/* Firmware patchlevel */
	u16 patch;

	/* Set to true, if we are using an opensource firmware.
	 * Use this to check for proprietary vs opensource. */
	bool opensource;
	/* Set to true, if the core needs a PCM firmware, but
	 * we failed to load one. This is always false for
	 * core rev > 10, as these don't need PCM firmware. */
	bool pcm_request_failed;
};

/* Device (802.11 core) initialization status. */
enum {
	B43_STAT_UNINIT = 0,	/* Uninitialized. */
	B43_STAT_INITIALIZED = 1,	/* Initialized, but not started, yet. */
	B43_STAT_STARTED = 2,	/* Up and running. */
};
#define b43_status(wldev)		atomic_read(&(wldev)->__init_status)
#define b43_set_status(wldev, stat)	do {			\
		atomic_set(&(wldev)->__init_status, (stat));	\
		smp_wmb();					\
					} while (0)

/* Data structure for one wireless device (802.11 core) */
struct b43_wldev {
	struct ssb_device *dev;
	struct b43_wl *wl;

	/* The device initialization status.
	 * Use b43_status() to query. */
	atomic_t __init_status;

	bool bad_frames_preempt;	/* Use "Bad Frames Preemption" (default off) */
	bool dfq_valid;		/* Directed frame queue valid (IBSS PS mode, ATIM) */
	bool radio_hw_enable;	/* saved state of radio hardware enabled state */
	bool qos_enabled;		/* TRUE, if QoS is used. */
	bool hwcrypto_enabled;		/* TRUE, if HW crypto acceleration is enabled. */

	/* PHY/Radio device. */
	struct b43_phy phy;

	union {
		/* DMA engines. */
		struct b43_dma dma;
		/* PIO engines. */
		struct b43_pio pio;
	};
	/* Use b43_using_pio_transfers() to check whether we are using
	 * DMA or PIO data transfers. */
	bool __using_pio_transfers;

	/* Various statistics about the physical device. */
	struct b43_stats stats;

	/* Reason code of the last interrupt. */
	u32 irq_reason;
	u32 dma_reason[6];
	/* The currently active generic-interrupt mask. */
	u32 irq_mask;

	/* Link Quality calculation context. */
	struct b43_noise_calculation noisecalc;
	/* if > 0 MAC is suspended. if == 0 MAC is enabled. */
	int mac_suspended;

	/* Periodic tasks */
	struct delayed_work periodic_work;
	unsigned int periodic_state;

	struct work_struct restart_work;

	/* encryption/decryption */
	u16 ktp;		/* Key table pointer */
	struct b43_key key[B43_NR_GROUP_KEYS * 2 + B43_NR_PAIRWISE_KEYS];

	/* Firmware data */
	struct b43_firmware fw;

	/* Devicelist in struct b43_wl (all 802.11 cores) */
	struct list_head list;

	/* Debugging stuff follows. */
#ifdef CONFIG_B43_DEBUG
	struct b43_dfsentry *dfsentry;
	unsigned int irq_count;
	unsigned int irq_bit_count[32];
	unsigned int tx_count;
	unsigned int rx_count;
#endif
};

/*
 * Include goes here to avoid a dependency problem.
 * A better fix would be to integrate xmit.h into b43.h.
 */
#include "xmit.h"

/* Data structure for the WLAN parts (802.11 cores) of the b43 chip. */
struct b43_wl {
	/* Pointer to the active wireless device on this chip */
	struct b43_wldev *current_dev;
	/* Pointer to the ieee80211 hardware data structure */
	struct ieee80211_hw *hw;

	/* Global driver mutex. Every operation must run with this mutex locked. */
	struct mutex mutex;
	/* Hard-IRQ spinlock. This lock protects things used in the hard-IRQ
	 * handler, only. This basically is just the IRQ mask register. */
	spinlock_t hardirq_lock;

	/* The number of queues that were registered with the mac80211 subsystem
	 * initially. This is a backup copy of hw->queues in case hw->queues has
	 * to be dynamically lowered at runtime (Firmware does not support QoS).
	 * hw->queues has to be restored to the original value before unregistering
	 * from the mac80211 subsystem. */
	u16 mac80211_initially_registered_queues;

	/* We can only have one operating interface (802.11 core)
	 * at a time. General information about this interface follows.
	 */

	struct ieee80211_vif *vif;
	/* The MAC address of the operating interface. */
	u8 mac_addr[ETH_ALEN];
	/* Current BSSID */
	u8 bssid[ETH_ALEN];
	/* Interface type. (NL80211_IFTYPE_XXX) */
	int if_type;
	/* Is the card operating in AP, STA or IBSS mode? */
	bool operating;
	/* filter flags */
	unsigned int filter_flags;
	/* Stats about the wireless interface */
	struct ieee80211_low_level_stats ieee_stats;

#ifdef CONFIG_B43_HWRNG
	struct hwrng rng;
	bool rng_initialized;
	char rng_name[30 + 1];
#endif /* CONFIG_B43_HWRNG */

	/* List of all wireless devices on this chip */
	struct list_head devlist;
	u8 nr_devs;

	bool radiotap_enabled;
	bool radio_enabled;

	/* The beacon we are currently using (AP or IBSS mode). */
	struct sk_buff *current_beacon;
	bool beacon0_uploaded;
	bool beacon1_uploaded;
	bool beacon_templates_virgin; /* Never wrote the templates? */
	struct work_struct beacon_update_trigger;

	/* The current QOS parameters for the 4 queues. */
	struct b43_qos_params qos_params[4];

	/* Work for adjustment of the transmission power.
	 * This is scheduled when we determine that the actual TX output
	 * power doesn't match what we want. */
	struct work_struct txpower_adjust_work;

	/* Packet transmit work */
	struct work_struct tx_work;
	/* Queue of packets to be transmitted. */
	struct sk_buff_head tx_queue;

	/* The device LEDs. */
	struct b43_leds leds;

#ifdef CONFIG_B43_PIO
	/*
	 * RX/TX header/tail buffers used by the frame transmit functions.
	 */
	struct b43_rxhdr_fw4 rxhdr;
	struct b43_txhdr txhdr;
	u8 rx_tail[4];
	u8 tx_tail[4];
#endif /* CONFIG_B43_PIO */
};

static inline struct b43_wl *hw_to_b43_wl(struct ieee80211_hw *hw)
{
	return hw->priv;
}

static inline struct b43_wldev *dev_to_b43_wldev(struct device *dev)
{
	struct ssb_device *ssb_dev = dev_to_ssb_dev(dev);
	return ssb_get_drvdata(ssb_dev);
}

/* Is the device operating in a specified mode (NL80211_IFTYPE_XXX). */
static inline int b43_is_mode(struct b43_wl *wl, int type)
{
	return (wl->operating && wl->if_type == type);
}

/**
 * b43_current_band - Returns the currently used band.
 * Returns one of IEEE80211_BAND_2GHZ and IEEE80211_BAND_5GHZ.
 */
static inline enum ieee80211_band b43_current_band(struct b43_wl *wl)
{
	return wl->hw->conf.channel->band;
}

static inline u16 b43_read16(struct b43_wldev *dev, u16 offset)
{
	return ssb_read16(dev->dev, offset);
}

static inline void b43_write16(struct b43_wldev *dev, u16 offset, u16 value)
{
	ssb_write16(dev->dev, offset, value);
}

static inline u32 b43_read32(struct b43_wldev *dev, u16 offset)
{
	return ssb_read32(dev->dev, offset);
}

static inline void b43_write32(struct b43_wldev *dev, u16 offset, u32 value)
{
	ssb_write32(dev->dev, offset, value);
}

static inline bool b43_using_pio_transfers(struct b43_wldev *dev)
{
#ifdef CONFIG_B43_PIO
	return dev->__using_pio_transfers;
#else
	return 0;
#endif
}

#ifdef CONFIG_B43_FORCE_PIO
# define B43_FORCE_PIO	1
#else
# define B43_FORCE_PIO	0
#endif


/* Message printing */
void b43info(struct b43_wl *wl, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));
void b43err(struct b43_wl *wl, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));
void b43warn(struct b43_wl *wl, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));
void b43dbg(struct b43_wl *wl, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));


/* A WARN_ON variant that vanishes when b43 debugging is disabled.
 * This _also_ evaluates the arg with debugging disabled. */
#if B43_DEBUG
# define B43_WARN_ON(x)	WARN_ON(x)
#else
static inline bool __b43_warn_on_dummy(bool x) { return x; }
# define B43_WARN_ON(x)	__b43_warn_on_dummy(unlikely(!!(x)))
#endif

/* Convert an integer to a Q5.2 value */
#define INT_TO_Q52(i)	((i) << 2)
/* Convert a Q5.2 value to an integer (precision loss!) */
#define Q52_TO_INT(q52)	((q52) >> 2)
/* Macros for printing a value in Q5.2 format */
#define Q52_FMT		"%u.%u"
#define Q52_ARG(q52)	Q52_TO_INT(q52), ((((q52) & 0x3) * 100) / 4)

#endif /* B43_H_ */
