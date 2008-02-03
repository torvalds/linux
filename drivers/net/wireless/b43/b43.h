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
#include "phy.h"

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
#define B43_MMIO_TSF_0			0x632	/* core rev < 3 only */
#define B43_MMIO_TSF_1			0x634	/* core rev < 3 only */
#define B43_MMIO_TSF_2			0x636	/* core rev < 3 only */
#define B43_MMIO_TSF_3			0x638	/* core rev < 3 only */
#define B43_MMIO_RNG			0x65A
#define B43_MMIO_POWERUP_DELAY		0x6A8

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
#define B43_SHM_SH_PHYVER		0x0050	/* PHY version */
#define B43_SHM_SH_PHYTYPE		0x0052	/* PHY type */
#define B43_SHM_SH_ANTSWAP		0x005C	/* Antenna swap threshold */
#define B43_SHM_SH_HOSTFLO		0x005E	/* Hostflags for ucode options (low) */
#define B43_SHM_SH_HOSTFHI		0x0060	/* Hostflags for ucode options (high) */
#define B43_SHM_SH_RFATT		0x0064	/* Current radio attenuation value */
#define B43_SHM_SH_RADAR		0x0066	/* Radar register */
#define B43_SHM_SH_PHYTXNOI		0x006E	/* PHY noise directly after TX (lower 8bit only) */
#define B43_SHM_SH_RFRXSP1		0x0072	/* RF RX SP Register 1 */
#define B43_SHM_SH_CHAN			0x00A0	/* Current channel (low 8bit only) */
#define  B43_SHM_SH_CHAN_5GHZ		0x0100	/* Bit set, if 5Ghz channel */
#define B43_SHM_SH_BCMCFIFOID		0x0108	/* Last posted cookie to the bcast/mcast FIFO */
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
#define B43_HF_ANTDIVHELP		0x00000001	/* ucode antenna div helper */
#define B43_HF_SYMW			0x00000002	/* G-PHY SYM workaround */
#define B43_HF_RXPULLW			0x00000004	/* RX pullup workaround */
#define B43_HF_CCKBOOST			0x00000008	/* 4dB CCK power boost (exclusive with OFDM boost) */
#define B43_HF_BTCOEX			0x00000010	/* Bluetooth coexistance */
#define B43_HF_GDCW			0x00000020	/* G-PHY DV canceller filter bw workaround */
#define B43_HF_OFDMPABOOST		0x00000040	/* Enable PA gain boost for OFDM */
#define B43_HF_ACPR			0x00000080	/* Disable for Japan, channel 14 */
#define B43_HF_EDCF			0x00000100	/* on if WME and MAC suspended */
#define B43_HF_TSSIRPSMW		0x00000200	/* TSSI reset PSM ucode workaround */
#define B43_HF_DSCRQ			0x00000400	/* Disable slow clock request in ucode */
#define B43_HF_ACIW			0x00000800	/* ACI workaround: shift bits by 2 on PHY CRS */
#define B43_HF_2060W			0x00001000	/* 2060 radio workaround */
#define B43_HF_RADARW			0x00002000	/* Radar workaround */
#define B43_HF_USEDEFKEYS		0x00004000	/* Enable use of default keys */
#define B43_HF_BT4PRIOCOEX		0x00010000	/* Bluetooth 2-priority coexistance */
#define B43_HF_FWKUP			0x00020000	/* Fast wake-up ucode */
#define B43_HF_VCORECALC		0x00040000	/* Force VCO recalculation when powering up synthpu */
#define B43_HF_PCISCW			0x00080000	/* PCI slow clock workaround */
#define B43_HF_4318TSSI			0x00200000	/* 4318 TSSI */
#define B43_HF_FBCMCFIFO		0x00400000	/* Flush bcast/mcast FIFO immediately */
#define B43_HF_HWPCTL			0x00800000	/* Enable hardwarre power control */
#define B43_HF_BTCOEXALT		0x01000000	/* Bluetooth coexistance in alternate pins */
#define B43_HF_TXBTCHECK		0x02000000	/* Bluetooth check during transmission */
#define B43_HF_SKCFPUP			0x04000000	/* Skip CFP update */

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
#define B43_IRQ_MASKTEMPLATE		(B43_IRQ_MAC_SUSPENDED | \
					 B43_IRQ_BEACON | \
					 B43_IRQ_TBTT_INDI | \
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
struct b43_pioqueue;

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


#define B43_PHYMODE(phytype)		(1 << (phytype))
#define B43_PHYMODE_A			B43_PHYMODE(B43_PHYTYPE_A)
#define B43_PHYMODE_B			B43_PHYMODE(B43_PHYTYPE_B)
#define B43_PHYMODE_G			B43_PHYMODE(B43_PHYTYPE_G)

struct b43_phy {
	/* Possible PHYMODEs on this PHY */
	u8 possible_phymodes;
	/* GMODE bit enabled? */
	bool gmode;
	/* Possible ieee80211 subsystem hwmodes for this PHY.
	 * Which mode is selected, depends on thr GMODE enabled bit */
#define B43_MAX_PHYHWMODES	2
	struct ieee80211_hw_mode hwmodes[B43_MAX_PHYHWMODES];

	/* Analog Type */
	u8 analog;
	/* B43_PHYTYPE_ */
	u8 type;
	/* PHY revision number. */
	u8 rev;

	/* Radio versioning */
	u16 radio_manuf;	/* Radio manufacturer */
	u16 radio_ver;		/* Radio version */
	u8 radio_rev;		/* Radio revision */

	bool dyn_tssi_tbl;	/* tssi2dbm is kmalloc()ed. */

	/* ACI (adjacent channel interference) flags. */
	bool aci_enable;
	bool aci_wlan_automatic;
	bool aci_hw_rssi;

	/* Radio switched on/off */
	bool radio_on;
	struct {
		/* Values saved when turning the radio off.
		 * They are needed when turning it on again. */
		bool valid;
		u16 rfover;
		u16 rfoverval;
	} radio_off_context;

	u16 minlowsig[2];
	u16 minlowsigpos[2];

	/* TSSI to dBm table in use */
	const s8 *tssi2dbm;
	/* Target idle TSSI */
	int tgt_idle_tssi;
	/* Current idle TSSI */
	int cur_idle_tssi;

	/* LocalOscillator control values. */
	struct b43_txpower_lo_control *lo_control;
	/* Values from b43_calc_loopback_gain() */
	s16 max_lb_gain;	/* Maximum Loopback gain in hdB */
	s16 trsw_rx_gain;	/* TRSW RX gain in hdB */
	s16 lna_lod_gain;	/* LNA lod */
	s16 lna_gain;		/* LNA */
	s16 pga_gain;		/* PGA */

	/* Desired TX power level (in dBm).
	 * This is set by the user and adjusted in b43_phy_xmitpower(). */
	u8 power_level;
	/* A-PHY TX Power control value. */
	u16 txpwr_offset;

	/* Current TX power level attenuation control values */
	struct b43_bbatt bbatt;
	struct b43_rfatt rfatt;
	u8 tx_control;		/* B43_TXCTL_XXX */

	/* Hardware Power Control enabled? */
	bool hardware_power_control;

	/* Current Interference Mitigation mode */
	int interfmode;
	/* Stack of saved values from the Interference Mitigation code.
	 * Each value in the stack is layed out as follows:
	 * bit 0-11:  offset
	 * bit 12-15: register ID
	 * bit 16-32: value
	 * register ID is: 0x1 PHY, 0x2 Radio, 0x3 ILT
	 */
#define B43_INTERFSTACK_SIZE	26
	u32 interfstack[B43_INTERFSTACK_SIZE];	//FIXME: use a data structure

	/* Saved values from the NRSSI Slope calculation */
	s16 nrssi[2];
	s32 nrssislope;
	/* In memory nrssi lookup table. */
	s8 nrssi_lt[64];

	/* current channel */
	u8 channel;

	u16 lofcal;

	u16 initval;		//FIXME rename?

	/* PHY TX errors counter. */
	atomic_t txerr_cnt;

	/* The device does address auto increment for the OFDM tables.
	 * We cache the previously used address here and omit the address
	 * write on the next table access, if possible. */
	u16 ofdmtab_addr; /* The address currently set in hardware. */
	enum { /* The last data flow direction. */
		B43_OFDMTAB_DIRECTION_UNKNOWN = 0,
		B43_OFDMTAB_DIRECTION_READ,
		B43_OFDMTAB_DIRECTION_WRITE,
	} ofdmtab_addr_direction;

#if B43_DEBUG
	/* Manual TX-power control enabled? */
	bool manual_txpower_control;
	/* PHY registers locked by b43_phy_lock()? */
	bool phy_locked;
#endif /* B43_DEBUG */
};

/* Data structures for DMA transmission, per 80211 core. */
struct b43_dma {
	struct b43_dmaring *tx_ring0;
	struct b43_dmaring *tx_ring1;
	struct b43_dmaring *tx_ring2;
	struct b43_dmaring *tx_ring3;
	struct b43_dmaring *tx_ring4;
	struct b43_dmaring *tx_ring5;

	struct b43_dmaring *rx_ring0;
	struct b43_dmaring *rx_ring3;	/* only available on core.rev < 5 */
};

/* Context information for a noise calculation (Link Quality). */
struct b43_noise_calculation {
	u8 channel_at_start;
	bool calculation_running;
	u8 nr_samples;
	s8 samples[8][4];
};

struct b43_stats {
	u8 link_noise;
	/* Store the last TX/RX times here for updating the leds. */
	unsigned long last_tx;
	unsigned long last_rx;
};

struct b43_key {
	/* If keyconf is NULL, this key is disabled.
	 * keyconf is a cookie. Don't derefenrence it outside of the set_key
	 * path, because b43 doesn't own it. */
	struct ieee80211_key_conf *keyconf;
	u8 algorithm;
};

struct b43_wldev;

/* Data structure for the WLAN parts (802.11 cores) of the b43 chip. */
struct b43_wl {
	/* Pointer to the active wireless device on this chip */
	struct b43_wldev *current_dev;
	/* Pointer to the ieee80211 hardware data structure */
	struct ieee80211_hw *hw;

	struct mutex mutex;
	spinlock_t irq_lock;
	/* Lock for LEDs access. */
	spinlock_t leds_lock;
	/* Lock for SHM access. */
	spinlock_t shm_lock;

	/* We can only have one operating interface (802.11 core)
	 * at a time. General information about this interface follows.
	 */

	struct ieee80211_vif *vif;
	/* The MAC address of the operating interface. */
	u8 mac_addr[ETH_ALEN];
	/* Current BSSID */
	u8 bssid[ETH_ALEN];
	/* Interface type. (IEEE80211_IF_TYPE_XXX) */
	int if_type;
	/* Is the card operating in AP, STA or IBSS mode? */
	bool operating;
	/* filter flags */
	unsigned int filter_flags;
	/* Stats about the wireless interface */
	struct ieee80211_low_level_stats ieee_stats;

	struct hwrng rng;
	u8 rng_initialized;
	char rng_name[30 + 1];

	/* The RF-kill button */
	struct b43_rfkill rfkill;

	/* List of all wireless devices on this chip */
	struct list_head devlist;
	u8 nr_devs;

	bool radiotap_enabled;

	/* The beacon we are currently using (AP or IBSS mode).
	 * This beacon stuff is protected by the irq_lock. */
	struct sk_buff *current_beacon;
	bool beacon0_uploaded;
	bool beacon1_uploaded;
};

/* In-memory representation of a cached microcode file. */
struct b43_firmware_file {
	const char *filename;
	const struct firmware *data;
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

/* XXX---   HOW LOCKING WORKS IN B43   ---XXX
 *
 * You should always acquire both, wl->mutex and wl->irq_lock unless:
 * - You don't need to acquire wl->irq_lock, if the interface is stopped.
 * - You don't need to acquire wl->mutex in the IRQ handler, IRQ tasklet
 *   and packet TX path (and _ONLY_ there.)
 */

/* Data structure for one wireless device (802.11 core) */
struct b43_wldev {
	struct ssb_device *dev;
	struct b43_wl *wl;

	/* The device initialization status.
	 * Use b43_status() to query. */
	atomic_t __init_status;
	/* Saved init status for handling suspend. */
	int suspend_init_status;

	bool bad_frames_preempt;	/* Use "Bad Frames Preemption" (default off) */
	bool dfq_valid;		/* Directed frame queue valid (IBSS PS mode, ATIM) */
	bool short_preamble;	/* TRUE, if short preamble is enabled. */
	bool short_slot;	/* TRUE, if short slot timing is enabled. */
	bool radio_hw_enable;	/* saved state of radio hardware enabled state */

	/* PHY/Radio device. */
	struct b43_phy phy;

	/* DMA engines. */
	struct b43_dma dma;

	/* Various statistics about the physical device. */
	struct b43_stats stats;

	/* The device LEDs. */
	struct b43_led led_tx;
	struct b43_led led_rx;
	struct b43_led led_assoc;
	struct b43_led led_radio;

	/* Reason code of the last interrupt. */
	u32 irq_reason;
	u32 dma_reason[6];
	/* saved irq enable/disable state bitfield. */
	u32 irq_savedstate;
	/* Link Quality calculation context. */
	struct b43_noise_calculation noisecalc;
	/* if > 0 MAC is suspended. if == 0 MAC is enabled. */
	int mac_suspended;

	/* Interrupt Service Routine tasklet (bottom-half) */
	struct tasklet_struct isr_tasklet;

	/* Periodic tasks */
	struct delayed_work periodic_work;
	unsigned int periodic_state;

	struct work_struct restart_work;

	/* encryption/decryption */
	u16 ktp;		/* Key table pointer */
	u8 max_nr_keys;
	struct b43_key key[58];

	/* Firmware data */
	struct b43_firmware fw;

	/* Devicelist in struct b43_wl (all 802.11 cores) */
	struct list_head list;

	/* Debugging stuff follows. */
#ifdef CONFIG_B43_DEBUG
	struct b43_dfsentry *dfsentry;
#endif
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

/* Is the device operating in a specified mode (IEEE80211_IF_TYPE_XXX). */
static inline int b43_is_mode(struct b43_wl *wl, int type)
{
	return (wl->operating && wl->if_type == type);
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

/* Message printing */
void b43info(struct b43_wl *wl, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));
void b43err(struct b43_wl *wl, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));
void b43warn(struct b43_wl *wl, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));
#if B43_DEBUG
void b43dbg(struct b43_wl *wl, const char *fmt, ...)
    __attribute__ ((format(printf, 2, 3)));
#else /* DEBUG */
# define b43dbg(wl, fmt...) do { /* nothing */ } while (0)
#endif /* DEBUG */

/* A WARN_ON variant that vanishes when b43 debugging is disabled.
 * This _also_ evaluates the arg with debugging disabled. */
#if B43_DEBUG
# define B43_WARN_ON(x)	WARN_ON(x)
#else
static inline bool __b43_warn_on_dummy(bool x) { return x; }
# define B43_WARN_ON(x)	__b43_warn_on_dummy(unlikely(!!(x)))
#endif

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

/* Convert an integer to a Q5.2 value */
#define INT_TO_Q52(i)	((i) << 2)
/* Convert a Q5.2 value to an integer (precision loss!) */
#define Q52_TO_INT(q52)	((q52) >> 2)
/* Macros for printing a value in Q5.2 format */
#define Q52_FMT		"%u.%u"
#define Q52_ARG(q52)	Q52_TO_INT(q52), ((((q52) & 0x3) * 100) / 4)

#endif /* B43_H_ */
