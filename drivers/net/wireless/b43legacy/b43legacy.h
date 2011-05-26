#ifndef B43legacy_H_
#define B43legacy_H_

#include <linux/hw_random.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/stringify.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <asm/atomic.h>
#include <linux/io.h>

#include <linux/ssb/ssb.h>
#include <linux/ssb/ssb_driver_chipcommon.h>

#include <linux/wireless.h>
#include <net/mac80211.h>

#include "debugfs.h"
#include "leds.h"
#include "rfkill.h"
#include "phy.h"


/* The unique identifier of the firmware that's officially supported by this
 * driver version. */
#define B43legacy_SUPPORTED_FIRMWARE_ID	"FW10"

#define B43legacy_IRQWAIT_MAX_RETRIES	20

/* MMIO offsets */
#define B43legacy_MMIO_DMA0_REASON	0x20
#define B43legacy_MMIO_DMA0_IRQ_MASK	0x24
#define B43legacy_MMIO_DMA1_REASON	0x28
#define B43legacy_MMIO_DMA1_IRQ_MASK	0x2C
#define B43legacy_MMIO_DMA2_REASON	0x30
#define B43legacy_MMIO_DMA2_IRQ_MASK	0x34
#define B43legacy_MMIO_DMA3_REASON	0x38
#define B43legacy_MMIO_DMA3_IRQ_MASK	0x3C
#define B43legacy_MMIO_DMA4_REASON	0x40
#define B43legacy_MMIO_DMA4_IRQ_MASK	0x44
#define B43legacy_MMIO_DMA5_REASON	0x48
#define B43legacy_MMIO_DMA5_IRQ_MASK	0x4C
#define B43legacy_MMIO_MACCTL		0x120	/* MAC control */
#define B43legacy_MMIO_MACCMD		0x124	/* MAC command */
#define B43legacy_MMIO_GEN_IRQ_REASON	0x128
#define B43legacy_MMIO_GEN_IRQ_MASK	0x12C
#define B43legacy_MMIO_RAM_CONTROL	0x130
#define B43legacy_MMIO_RAM_DATA		0x134
#define B43legacy_MMIO_PS_STATUS		0x140
#define B43legacy_MMIO_RADIO_HWENABLED_HI	0x158
#define B43legacy_MMIO_SHM_CONTROL	0x160
#define B43legacy_MMIO_SHM_DATA		0x164
#define B43legacy_MMIO_SHM_DATA_UNALIGNED	0x166
#define B43legacy_MMIO_XMITSTAT_0		0x170
#define B43legacy_MMIO_XMITSTAT_1		0x174
#define B43legacy_MMIO_REV3PLUS_TSF_LOW	0x180 /* core rev >= 3 only */
#define B43legacy_MMIO_REV3PLUS_TSF_HIGH	0x184 /* core rev >= 3 only */
#define B43legacy_MMIO_TSF_CFP_REP	0x188
#define B43legacy_MMIO_TSF_CFP_START	0x18C
/* 32-bit DMA */
#define B43legacy_MMIO_DMA32_BASE0	0x200
#define B43legacy_MMIO_DMA32_BASE1	0x220
#define B43legacy_MMIO_DMA32_BASE2	0x240
#define B43legacy_MMIO_DMA32_BASE3	0x260
#define B43legacy_MMIO_DMA32_BASE4	0x280
#define B43legacy_MMIO_DMA32_BASE5	0x2A0
/* 64-bit DMA */
#define B43legacy_MMIO_DMA64_BASE0	0x200
#define B43legacy_MMIO_DMA64_BASE1	0x240
#define B43legacy_MMIO_DMA64_BASE2	0x280
#define B43legacy_MMIO_DMA64_BASE3	0x2C0
#define B43legacy_MMIO_DMA64_BASE4	0x300
#define B43legacy_MMIO_DMA64_BASE5	0x340
/* PIO */
#define B43legacy_MMIO_PIO1_BASE		0x300
#define B43legacy_MMIO_PIO2_BASE		0x310
#define B43legacy_MMIO_PIO3_BASE		0x320
#define B43legacy_MMIO_PIO4_BASE		0x330

#define B43legacy_MMIO_PHY_VER		0x3E0
#define B43legacy_MMIO_PHY_RADIO		0x3E2
#define B43legacy_MMIO_PHY0		0x3E6
#define B43legacy_MMIO_ANTENNA		0x3E8
#define B43legacy_MMIO_CHANNEL		0x3F0
#define B43legacy_MMIO_CHANNEL_EXT	0x3F4
#define B43legacy_MMIO_RADIO_CONTROL	0x3F6
#define B43legacy_MMIO_RADIO_DATA_HIGH	0x3F8
#define B43legacy_MMIO_RADIO_DATA_LOW	0x3FA
#define B43legacy_MMIO_PHY_CONTROL	0x3FC
#define B43legacy_MMIO_PHY_DATA		0x3FE
#define B43legacy_MMIO_MACFILTER_CONTROL	0x420
#define B43legacy_MMIO_MACFILTER_DATA	0x422
#define B43legacy_MMIO_RCMTA_COUNT	0x43C /* Receive Match Transmitter Addr */
#define B43legacy_MMIO_RADIO_HWENABLED_LO	0x49A
#define B43legacy_MMIO_GPIO_CONTROL	0x49C
#define B43legacy_MMIO_GPIO_MASK		0x49E
#define B43legacy_MMIO_TSF_CFP_PRETBTT	0x612
#define B43legacy_MMIO_TSF_0		0x632 /* core rev < 3 only */
#define B43legacy_MMIO_TSF_1		0x634 /* core rev < 3 only */
#define B43legacy_MMIO_TSF_2		0x636 /* core rev < 3 only */
#define B43legacy_MMIO_TSF_3		0x638 /* core rev < 3 only */
#define B43legacy_MMIO_RNG		0x65A
#define B43legacy_MMIO_POWERUP_DELAY	0x6A8

/* SPROM boardflags_lo values */
#define B43legacy_BFL_PACTRL		0x0002
#define B43legacy_BFL_RSSI		0x0008
#define B43legacy_BFL_EXTLNA		0x1000

/* GPIO register offset, in both ChipCommon and PCI core. */
#define B43legacy_GPIO_CONTROL		0x6c

/* SHM Routing */
#define	B43legacy_SHM_SHARED		0x0001
#define	B43legacy_SHM_WIRELESS		0x0002
#define	B43legacy_SHM_HW		0x0004
#define	B43legacy_SHM_UCODE		0x0300

/* SHM Routing modifiers */
#define B43legacy_SHM_AUTOINC_R		0x0200 /* Read Auto-increment */
#define B43legacy_SHM_AUTOINC_W		0x0100 /* Write Auto-increment */
#define B43legacy_SHM_AUTOINC_RW	(B43legacy_SHM_AUTOINC_R | \
					 B43legacy_SHM_AUTOINC_W)

/* Misc SHM_SHARED offsets */
#define B43legacy_SHM_SH_WLCOREREV	0x0016 /* 802.11 core revision */
#define B43legacy_SHM_SH_HOSTFLO	0x005E /* Hostflags ucode opts (low) */
#define B43legacy_SHM_SH_HOSTFHI	0x0060 /* Hostflags ucode opts (high) */
/* SHM_SHARED crypto engine */
#define B43legacy_SHM_SH_KEYIDXBLOCK	0x05D4 /* Key index/algorithm block */
/* SHM_SHARED beacon/AP variables */
#define B43legacy_SHM_SH_DTIMP		0x0012 /* DTIM period */
#define B43legacy_SHM_SH_BTL0		0x0018 /* Beacon template length 0 */
#define B43legacy_SHM_SH_BTL1		0x001A /* Beacon template length 1 */
#define B43legacy_SHM_SH_BTSFOFF	0x001C /* Beacon TSF offset */
#define B43legacy_SHM_SH_TIMPOS		0x001E /* TIM position in beacon */
#define B43legacy_SHM_SH_BEACPHYCTL	0x0054 /* Beacon PHY TX control word */
/* SHM_SHARED ACK/CTS control */
#define B43legacy_SHM_SH_ACKCTSPHYCTL	0x0022 /* ACK/CTS PHY control word */
/* SHM_SHARED probe response variables */
#define B43legacy_SHM_SH_PRTLEN		0x004A /* Probe Response template length */
#define B43legacy_SHM_SH_PRMAXTIME	0x0074 /* Probe Response max time */
#define B43legacy_SHM_SH_PRPHYCTL	0x0188 /* Probe Resp PHY TX control */
/* SHM_SHARED rate tables */
#define B43legacy_SHM_SH_OFDMDIRECT	0x0480 /* Pointer to OFDM direct map */
#define B43legacy_SHM_SH_OFDMBASIC	0x04A0 /* Pointer to OFDM basic rate map */
#define B43legacy_SHM_SH_CCKDIRECT	0x04C0 /* Pointer to CCK direct map */
#define B43legacy_SHM_SH_CCKBASIC	0x04E0 /* Pointer to CCK basic rate map */
/* SHM_SHARED microcode soft registers */
#define B43legacy_SHM_SH_UCODEREV	0x0000 /* Microcode revision */
#define B43legacy_SHM_SH_UCODEPATCH	0x0002 /* Microcode patchlevel */
#define B43legacy_SHM_SH_UCODEDATE	0x0004 /* Microcode date */
#define B43legacy_SHM_SH_UCODETIME	0x0006 /* Microcode time */
#define B43legacy_SHM_SH_SPUWKUP	0x0094 /* pre-wakeup for synth PU in us */
#define B43legacy_SHM_SH_PRETBTT	0x0096 /* pre-TBTT in us */

#define B43legacy_UCODEFLAGS_OFFSET     0x005E

/* Hardware Radio Enable masks */
#define B43legacy_MMIO_RADIO_HWENABLED_HI_MASK (1 << 16)
#define B43legacy_MMIO_RADIO_HWENABLED_LO_MASK (1 << 4)

/* HostFlags. See b43legacy_hf_read/write() */
#define B43legacy_HF_SYMW		0x00000002 /* G-PHY SYM workaround */
#define B43legacy_HF_GDCW		0x00000020 /* G-PHY DV cancel filter */
#define B43legacy_HF_OFDMPABOOST	0x00000040 /* Enable PA boost OFDM */
#define B43legacy_HF_EDCF		0x00000100 /* on if WME/MAC suspended */

/* MacFilter offsets. */
#define B43legacy_MACFILTER_SELF	0x0000
#define B43legacy_MACFILTER_BSSID	0x0003
#define B43legacy_MACFILTER_MAC		0x0010

/* PHYVersioning */
#define B43legacy_PHYTYPE_B		0x01
#define B43legacy_PHYTYPE_G		0x02

/* PHYRegisters */
#define B43legacy_PHY_G_LO_CONTROL	0x0810
#define B43legacy_PHY_ILT_G_CTRL	0x0472
#define B43legacy_PHY_ILT_G_DATA1	0x0473
#define B43legacy_PHY_ILT_G_DATA2	0x0474
#define B43legacy_PHY_G_PCTL		0x0029
#define B43legacy_PHY_RADIO_BITFIELD	0x0401
#define B43legacy_PHY_G_CRS		0x0429
#define B43legacy_PHY_NRSSILT_CTRL	0x0803
#define B43legacy_PHY_NRSSILT_DATA	0x0804

/* RadioRegisters */
#define B43legacy_RADIOCTL_ID		0x01

/* MAC Control bitfield */
#define B43legacy_MACCTL_ENABLED	0x00000001 /* MAC Enabled */
#define B43legacy_MACCTL_PSM_RUN	0x00000002 /* Run Microcode */
#define B43legacy_MACCTL_PSM_JMP0	0x00000004 /* Microcode jump to 0 */
#define B43legacy_MACCTL_SHM_ENABLED	0x00000100 /* SHM Enabled */
#define B43legacy_MACCTL_IHR_ENABLED	0x00000400 /* IHR Region Enabled */
#define B43legacy_MACCTL_BE		0x00010000 /* Big Endian mode */
#define B43legacy_MACCTL_INFRA		0x00020000 /* Infrastructure mode */
#define B43legacy_MACCTL_AP		0x00040000 /* AccessPoint mode */
#define B43legacy_MACCTL_RADIOLOCK	0x00080000 /* Radio lock */
#define B43legacy_MACCTL_BEACPROMISC	0x00100000 /* Beacon Promiscuous */
#define B43legacy_MACCTL_KEEP_BADPLCP	0x00200000 /* Keep bad PLCP frames */
#define B43legacy_MACCTL_KEEP_CTL	0x00400000 /* Keep control frames */
#define B43legacy_MACCTL_KEEP_BAD	0x00800000 /* Keep bad frames (FCS) */
#define B43legacy_MACCTL_PROMISC	0x01000000 /* Promiscuous mode */
#define B43legacy_MACCTL_HWPS		0x02000000 /* Hardware Power Saving */
#define B43legacy_MACCTL_AWAKE		0x04000000 /* Device is awake */
#define B43legacy_MACCTL_TBTTHOLD	0x10000000 /* TBTT Hold */
#define B43legacy_MACCTL_GMODE		0x80000000 /* G Mode */

/* MAC Command bitfield */
#define B43legacy_MACCMD_BEACON0_VALID	0x00000001 /* Beacon 0 in template RAM is busy/valid */
#define B43legacy_MACCMD_BEACON1_VALID	0x00000002 /* Beacon 1 in template RAM is busy/valid */
#define B43legacy_MACCMD_DFQ_VALID	0x00000004 /* Directed frame queue valid (IBSS PS mode, ATIM) */
#define B43legacy_MACCMD_CCA		0x00000008 /* Clear channel assessment */
#define B43legacy_MACCMD_BGNOISE	0x00000010 /* Background noise */

/* 802.11 core specific TM State Low flags */
#define B43legacy_TMSLOW_GMODE		0x20000000 /* G Mode Enable */
#define B43legacy_TMSLOW_PLLREFSEL	0x00200000 /* PLL Freq Ref Select */
#define B43legacy_TMSLOW_MACPHYCLKEN	0x00100000 /* MAC PHY Clock Ctrl Enbl */
#define B43legacy_TMSLOW_PHYRESET	0x00080000 /* PHY Reset */
#define B43legacy_TMSLOW_PHYCLKEN	0x00040000 /* PHY Clock Enable */

/* 802.11 core specific TM State High flags */
#define B43legacy_TMSHIGH_FCLOCK	0x00040000 /* Fast Clock Available */
#define B43legacy_TMSHIGH_GPHY		0x00010000 /* G-PHY avail (rev >= 5) */

#define B43legacy_UCODEFLAG_AUTODIV       0x0001

/* Generic-Interrupt reasons. */
#define B43legacy_IRQ_MAC_SUSPENDED	0x00000001
#define B43legacy_IRQ_BEACON		0x00000002
#define B43legacy_IRQ_TBTT_INDI		0x00000004 /* Target Beacon Transmit Time */
#define B43legacy_IRQ_BEACON_TX_OK	0x00000008
#define B43legacy_IRQ_BEACON_CANCEL	0x00000010
#define B43legacy_IRQ_ATIM_END		0x00000020
#define B43legacy_IRQ_PMQ		0x00000040
#define B43legacy_IRQ_PIO_WORKAROUND	0x00000100
#define B43legacy_IRQ_MAC_TXERR		0x00000200
#define B43legacy_IRQ_PHY_TXERR		0x00000800
#define B43legacy_IRQ_PMEVENT		0x00001000
#define B43legacy_IRQ_TIMER0		0x00002000
#define B43legacy_IRQ_TIMER1		0x00004000
#define B43legacy_IRQ_DMA		0x00008000
#define B43legacy_IRQ_TXFIFO_FLUSH_OK	0x00010000
#define B43legacy_IRQ_CCA_MEASURE_OK	0x00020000
#define B43legacy_IRQ_NOISESAMPLE_OK	0x00040000
#define B43legacy_IRQ_UCODE_DEBUG	0x08000000
#define B43legacy_IRQ_RFKILL		0x10000000
#define B43legacy_IRQ_TX_OK		0x20000000
#define B43legacy_IRQ_PHY_G_CHANGED	0x40000000
#define B43legacy_IRQ_TIMEOUT		0x80000000

#define B43legacy_IRQ_ALL		0xFFFFFFFF
#define B43legacy_IRQ_MASKTEMPLATE	(B43legacy_IRQ_MAC_SUSPENDED |	\
					 B43legacy_IRQ_TBTT_INDI |	\
					 B43legacy_IRQ_ATIM_END |	\
					 B43legacy_IRQ_PMQ |		\
					 B43legacy_IRQ_MAC_TXERR |	\
					 B43legacy_IRQ_PHY_TXERR |	\
					 B43legacy_IRQ_DMA |		\
					 B43legacy_IRQ_TXFIFO_FLUSH_OK | \
					 B43legacy_IRQ_NOISESAMPLE_OK | \
					 B43legacy_IRQ_UCODE_DEBUG |	\
					 B43legacy_IRQ_RFKILL |		\
					 B43legacy_IRQ_TX_OK)

/* Device specific rate values.
 * The actual values defined here are (rate_in_mbps * 2).
 * Some code depends on this. Don't change it. */
#define B43legacy_CCK_RATE_1MB		2
#define B43legacy_CCK_RATE_2MB		4
#define B43legacy_CCK_RATE_5MB		11
#define B43legacy_CCK_RATE_11MB		22
#define B43legacy_OFDM_RATE_6MB		12
#define B43legacy_OFDM_RATE_9MB		18
#define B43legacy_OFDM_RATE_12MB	24
#define B43legacy_OFDM_RATE_18MB	36
#define B43legacy_OFDM_RATE_24MB	48
#define B43legacy_OFDM_RATE_36MB	72
#define B43legacy_OFDM_RATE_48MB	96
#define B43legacy_OFDM_RATE_54MB	108
/* Convert a b43legacy rate value to a rate in 100kbps */
#define B43legacy_RATE_TO_100KBPS(rate)	(((rate) * 10) / 2)


#define B43legacy_DEFAULT_SHORT_RETRY_LIMIT	7
#define B43legacy_DEFAULT_LONG_RETRY_LIMIT	4

#define B43legacy_PHY_TX_BADNESS_LIMIT		1000

/* Max size of a security key */
#define B43legacy_SEC_KEYSIZE		16
/* Security algorithms. */
enum {
	B43legacy_SEC_ALGO_NONE = 0, /* unencrypted, as of TX header. */
	B43legacy_SEC_ALGO_WEP40,
	B43legacy_SEC_ALGO_TKIP,
	B43legacy_SEC_ALGO_AES,
	B43legacy_SEC_ALGO_WEP104,
	B43legacy_SEC_ALGO_AES_LEGACY,
};

/* Core Information Registers */
#define B43legacy_CIR_BASE                0xf00
#define B43legacy_CIR_SBTPSFLAG           (B43legacy_CIR_BASE + 0x18)
#define B43legacy_CIR_SBIMSTATE           (B43legacy_CIR_BASE + 0x90)
#define B43legacy_CIR_SBINTVEC            (B43legacy_CIR_BASE + 0x94)
#define B43legacy_CIR_SBTMSTATELOW        (B43legacy_CIR_BASE + 0x98)
#define B43legacy_CIR_SBTMSTATEHIGH       (B43legacy_CIR_BASE + 0x9c)
#define B43legacy_CIR_SBIMCONFIGLOW       (B43legacy_CIR_BASE + 0xa8)
#define B43legacy_CIR_SB_ID_HI            (B43legacy_CIR_BASE + 0xfc)

/* sbtmstatehigh state flags */
#define B43legacy_SBTMSTATEHIGH_SERROR		0x00000001
#define B43legacy_SBTMSTATEHIGH_BUSY		0x00000004
#define B43legacy_SBTMSTATEHIGH_TIMEOUT		0x00000020
#define B43legacy_SBTMSTATEHIGH_G_PHY_AVAIL	0x00010000
#define B43legacy_SBTMSTATEHIGH_COREFLAGS	0x1FFF0000
#define B43legacy_SBTMSTATEHIGH_DMA64BIT	0x10000000
#define B43legacy_SBTMSTATEHIGH_GATEDCLK	0x20000000
#define B43legacy_SBTMSTATEHIGH_BISTFAILED	0x40000000
#define B43legacy_SBTMSTATEHIGH_BISTCOMPLETE	0x80000000

/* sbimstate flags */
#define B43legacy_SBIMSTATE_IB_ERROR		0x20000
#define B43legacy_SBIMSTATE_TIMEOUT		0x40000

#define PFX		KBUILD_MODNAME ": "
#ifdef assert
# undef assert
#endif
#ifdef CONFIG_B43LEGACY_DEBUG
# define B43legacy_WARN_ON(x)	WARN_ON(x)
# define B43legacy_BUG_ON(expr)						\
	do {								\
		if (unlikely((expr))) {					\
			printk(KERN_INFO PFX "Test (%s) failed\n",	\
					      #expr);			\
			BUG_ON(expr);					\
		}							\
	} while (0)
# define B43legacy_DEBUG	1
#else
/* This will evaluate the argument even if debugging is disabled. */
static inline bool __b43legacy_warn_on_dummy(bool x) { return x; }
# define B43legacy_WARN_ON(x)	__b43legacy_warn_on_dummy(unlikely(!!(x)))
# define B43legacy_BUG_ON(x)	do { /* nothing */ } while (0)
# define B43legacy_DEBUG	0
#endif


struct net_device;
struct pci_dev;
struct b43legacy_dmaring;
struct b43legacy_pioqueue;

/* The firmware file header */
#define B43legacy_FW_TYPE_UCODE	'u'
#define B43legacy_FW_TYPE_PCM	'p'
#define B43legacy_FW_TYPE_IV	'i'
struct b43legacy_fw_header {
	/* File type */
	u8 type;
	/* File format version */
	u8 ver;
	u8 __padding[2];
	/* Size of the data. For ucode and PCM this is in bytes.
	 * For IV this is number-of-ivs. */
	__be32 size;
} __packed;

/* Initial Value file format */
#define B43legacy_IV_OFFSET_MASK	0x7FFF
#define B43legacy_IV_32BIT		0x8000
struct b43legacy_iv {
	__be16 offset_size;
	union {
		__be16 d16;
		__be32 d32;
	} data __packed;
} __packed;

#define B43legacy_PHYMODE(phytype)	(1 << (phytype))
#define B43legacy_PHYMODE_B		B43legacy_PHYMODE	\
					((B43legacy_PHYTYPE_B))
#define B43legacy_PHYMODE_G		B43legacy_PHYMODE	\
					((B43legacy_PHYTYPE_G))

/* Value pair to measure the LocalOscillator. */
struct b43legacy_lopair {
	s8 low;
	s8 high;
	u8 used:1;
};
#define B43legacy_LO_COUNT	(14*4)

struct b43legacy_phy {
	/* Possible PHYMODEs on this PHY */
	u8 possible_phymodes;
	/* GMODE bit enabled in MACCTL? */
	bool gmode;

	/* Analog Type */
	u8 analog;
	/* B43legacy_PHYTYPE_ */
	u8 type;
	/* PHY revision number. */
	u8 rev;

	u16 antenna_diversity;
	u16 savedpctlreg;
	/* Radio versioning */
	u16 radio_manuf;	/* Radio manufacturer */
	u16 radio_ver;		/* Radio version */
	u8 calibrated:1;
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

	/* LO Measurement Data.
	 * Use b43legacy_get_lopair() to get a value.
	 */
	struct b43legacy_lopair *_lo_pairs;
	/* TSSI to dBm table in use */
	const s8 *tssi2dbm;
	/* idle TSSI value */
	s8 idle_tssi;
	/* Target idle TSSI */
	int tgt_idle_tssi;
	/* Current idle TSSI */
	int cur_idle_tssi;

	/* LocalOscillator control values. */
	struct b43legacy_txpower_lo_control *lo_control;
	/* Values from b43legacy_calc_loopback_gain() */
	s16 max_lb_gain;	/* Maximum Loopback gain in hdB */
	s16 trsw_rx_gain;	/* TRSW RX gain in hdB */
	s16 lna_lod_gain;	/* LNA lod */
	s16 lna_gain;		/* LNA */
	s16 pga_gain;		/* PGA */

	/* Desired TX power level (in dBm). This is set by the user and
	 * adjusted in b43legacy_phy_xmitpower(). */
	u8 power_level;

	/* Values from b43legacy_calc_loopback_gain() */
	u16 loopback_gain[2];

	/* TX Power control values. */
	/* B/G PHY */
	struct {
		/* Current Radio Attenuation for TXpower recalculation. */
		u16 rfatt;
		/* Current Baseband Attenuation for TXpower recalculation. */
		u16 bbatt;
		/* Current TXpower control value for TXpower recalculation. */
		u16 txctl1;
		u16 txctl2;
	};
	/* A PHY */
	struct {
		u16 txpwr_offset;
	};

	/* Current Interference Mitigation mode */
	int interfmode;
	/* Stack of saved values from the Interference Mitigation code.
	 * Each value in the stack is laid out as follows:
	 * bit 0-11:  offset
	 * bit 12-15: register ID
	 * bit 16-32: value
	 * register ID is: 0x1 PHY, 0x2 Radio, 0x3 ILT
	 */
#define B43legacy_INTERFSTACK_SIZE	26
	u32 interfstack[B43legacy_INTERFSTACK_SIZE];

	/* Saved values from the NRSSI Slope calculation */
	s16 nrssi[2];
	s32 nrssislope;
	/* In memory nrssi lookup table. */
	s8 nrssi_lt[64];

	/* current channel */
	u8 channel;

	u16 lofcal;

	u16 initval;

	/* PHY TX errors counter. */
	atomic_t txerr_cnt;

#if B43legacy_DEBUG
	/* Manual TX-power control enabled? */
	bool manual_txpower_control;
	/* PHY registers locked by b43legacy_phy_lock()? */
	bool phy_locked;
#endif /* B43legacy_DEBUG */
};

/* Data structures for DMA transmission, per 80211 core. */
struct b43legacy_dma {
	struct b43legacy_dmaring *tx_ring0;
	struct b43legacy_dmaring *tx_ring1;
	struct b43legacy_dmaring *tx_ring2;
	struct b43legacy_dmaring *tx_ring3;
	struct b43legacy_dmaring *tx_ring4;
	struct b43legacy_dmaring *tx_ring5;

	struct b43legacy_dmaring *rx_ring0;
	struct b43legacy_dmaring *rx_ring3; /* only on core.rev < 5 */
};

/* Data structures for PIO transmission, per 80211 core. */
struct b43legacy_pio {
	struct b43legacy_pioqueue *queue0;
	struct b43legacy_pioqueue *queue1;
	struct b43legacy_pioqueue *queue2;
	struct b43legacy_pioqueue *queue3;
};

/* Context information for a noise calculation (Link Quality). */
struct b43legacy_noise_calculation {
	u8 channel_at_start;
	bool calculation_running;
	u8 nr_samples;
	s8 samples[8][4];
};

struct b43legacy_stats {
	u8 link_noise;
	/* Store the last TX/RX times here for updating the leds. */
	unsigned long last_tx;
	unsigned long last_rx;
};

struct b43legacy_key {
	void *keyconf;
	bool enabled;
	u8 algorithm;
};

struct b43legacy_wldev;

/* Data structure for the WLAN parts (802.11 cores) of the b43legacy chip. */
struct b43legacy_wl {
	/* Pointer to the active wireless device on this chip */
	struct b43legacy_wldev *current_dev;
	/* Pointer to the ieee80211 hardware data structure */
	struct ieee80211_hw *hw;

	spinlock_t irq_lock;		/* locks IRQ */
	struct mutex mutex;		/* locks wireless core state */
	spinlock_t leds_lock;		/* lock for leds */

	/* We can only have one operating interface (802.11 core)
	 * at a time. General information about this interface follows.
	 */

	struct ieee80211_vif *vif;
	/* MAC address (can be NULL). */
	u8 mac_addr[ETH_ALEN];
	/* Current BSSID (can be NULL). */
	u8 bssid[ETH_ALEN];
	/* Interface type. (IEEE80211_IF_TYPE_XXX) */
	int if_type;
	/* Is the card operating in AP, STA or IBSS mode? */
	bool operating;
	/* filter flags */
	unsigned int filter_flags;
	/* Stats about the wireless interface */
	struct ieee80211_low_level_stats ieee_stats;

#ifdef CONFIG_B43LEGACY_HWRNG
	struct hwrng rng;
	u8 rng_initialized;
	char rng_name[30 + 1];
#endif

	/* List of all wireless devices on this chip */
	struct list_head devlist;
	u8 nr_devs;

	bool radiotap_enabled;
	bool radio_enabled;

	/* The beacon we are currently using (AP or IBSS mode).
	 * This beacon stuff is protected by the irq_lock. */
	struct sk_buff *current_beacon;
	bool beacon0_uploaded;
	bool beacon1_uploaded;
	bool beacon_templates_virgin; /* Never wrote the templates? */
	struct work_struct beacon_update_trigger;
};

/* Pointers to the firmware data and meta information about it. */
struct b43legacy_firmware {
	/* Microcode */
	const struct firmware *ucode;
	/* PCM code */
	const struct firmware *pcm;
	/* Initial MMIO values for the firmware */
	const struct firmware *initvals;
	/* Initial MMIO values for the firmware, band-specific */
	const struct firmware *initvals_band;
	/* Firmware revision */
	u16 rev;
	/* Firmware patchlevel */
	u16 patch;
};

/* Device (802.11 core) initialization status. */
enum {
	B43legacy_STAT_UNINIT		= 0, /* Uninitialized. */
	B43legacy_STAT_INITIALIZED	= 1, /* Initialized, not yet started. */
	B43legacy_STAT_STARTED	= 2, /* Up and running. */
};
#define b43legacy_status(wldev)	atomic_read(&(wldev)->__init_status)
#define b43legacy_set_status(wldev, stat)	do {		\
		atomic_set(&(wldev)->__init_status, (stat));	\
		smp_wmb();					\
					} while (0)

/* *** ---   HOW LOCKING WORKS IN B43legacy   --- ***
 *
 * You should always acquire both, wl->mutex and wl->irq_lock unless:
 * - You don't need to acquire wl->irq_lock, if the interface is stopped.
 * - You don't need to acquire wl->mutex in the IRQ handler, IRQ tasklet
 *   and packet TX path (and _ONLY_ there.)
 */

/* Data structure for one wireless device (802.11 core) */
struct b43legacy_wldev {
	struct ssb_device *dev;
	struct b43legacy_wl *wl;

	/* The device initialization status.
	 * Use b43legacy_status() to query. */
	atomic_t __init_status;
	/* Saved init status for handling suspend. */
	int suspend_init_status;

	bool __using_pio;	/* Using pio rather than dma. */
	bool bad_frames_preempt;/* Use "Bad Frames Preemption". */
	bool dfq_valid;		/* Directed frame queue valid (IBSS PS mode, ATIM). */
	bool short_preamble;	/* TRUE if using short preamble. */
	bool radio_hw_enable;	/* State of radio hardware enable bit. */

	/* PHY/Radio device. */
	struct b43legacy_phy phy;
	union {
		/* DMA engines. */
		struct b43legacy_dma dma;
		/* PIO engines. */
		struct b43legacy_pio pio;
	};

	/* Various statistics about the physical device. */
	struct b43legacy_stats stats;

	/* The device LEDs. */
	struct b43legacy_led led_tx;
	struct b43legacy_led led_rx;
	struct b43legacy_led led_assoc;
	struct b43legacy_led led_radio;

	/* Reason code of the last interrupt. */
	u32 irq_reason;
	u32 dma_reason[6];
	/* The currently active generic-interrupt mask. */
	u32 irq_mask;
	/* Link Quality calculation context. */
	struct b43legacy_noise_calculation noisecalc;
	/* if > 0 MAC is suspended. if == 0 MAC is enabled. */
	int mac_suspended;

	/* Interrupt Service Routine tasklet (bottom-half) */
	struct tasklet_struct isr_tasklet;

	/* Periodic tasks */
	struct delayed_work periodic_work;
	unsigned int periodic_state;

	struct work_struct restart_work;

	/* encryption/decryption */
	u16 ktp; /* Key table pointer */
	u8 max_nr_keys;
	struct b43legacy_key key[58];

	/* Firmware data */
	struct b43legacy_firmware fw;

	/* Devicelist in struct b43legacy_wl (all 802.11 cores) */
	struct list_head list;

	/* Debugging stuff follows. */
#ifdef CONFIG_B43LEGACY_DEBUG
	struct b43legacy_dfsentry *dfsentry;
#endif
};


static inline
struct b43legacy_wl *hw_to_b43legacy_wl(struct ieee80211_hw *hw)
{
	return hw->priv;
}

/* Helper function, which returns a boolean.
 * TRUE, if PIO is used; FALSE, if DMA is used.
 */
#if defined(CONFIG_B43LEGACY_DMA) && defined(CONFIG_B43LEGACY_PIO)
static inline
int b43legacy_using_pio(struct b43legacy_wldev *dev)
{
	return dev->__using_pio;
}
#elif defined(CONFIG_B43LEGACY_DMA)
static inline
int b43legacy_using_pio(struct b43legacy_wldev *dev)
{
	return 0;
}
#elif defined(CONFIG_B43LEGACY_PIO)
static inline
int b43legacy_using_pio(struct b43legacy_wldev *dev)
{
	return 1;
}
#else
# error "Using neither DMA nor PIO? Confused..."
#endif


static inline
struct b43legacy_wldev *dev_to_b43legacy_wldev(struct device *dev)
{
	struct ssb_device *ssb_dev = dev_to_ssb_dev(dev);
	return ssb_get_drvdata(ssb_dev);
}

/* Is the device operating in a specified mode (IEEE80211_IF_TYPE_XXX). */
static inline
int b43legacy_is_mode(struct b43legacy_wl *wl, int type)
{
	return (wl->operating &&
		wl->if_type == type);
}

static inline
bool is_bcm_board_vendor(struct b43legacy_wldev *dev)
{
	return  (dev->dev->bus->boardinfo.vendor == PCI_VENDOR_ID_BROADCOM);
}

static inline
u16 b43legacy_read16(struct b43legacy_wldev *dev, u16 offset)
{
	return ssb_read16(dev->dev, offset);
}

static inline
void b43legacy_write16(struct b43legacy_wldev *dev, u16 offset, u16 value)
{
	ssb_write16(dev->dev, offset, value);
}

static inline
u32 b43legacy_read32(struct b43legacy_wldev *dev, u16 offset)
{
	return ssb_read32(dev->dev, offset);
}

static inline
void b43legacy_write32(struct b43legacy_wldev *dev, u16 offset, u32 value)
{
	ssb_write32(dev->dev, offset, value);
}

static inline
struct b43legacy_lopair *b43legacy_get_lopair(struct b43legacy_phy *phy,
					      u16 radio_attenuation,
					      u16 baseband_attenuation)
{
	return phy->_lo_pairs + (radio_attenuation
			+ 14 * (baseband_attenuation / 2));
}



/* Message printing */
void b43legacyinfo(struct b43legacy_wl *wl, const char *fmt, ...)
		__attribute__((format(printf, 2, 3)));
void b43legacyerr(struct b43legacy_wl *wl, const char *fmt, ...)
		__attribute__((format(printf, 2, 3)));
void b43legacywarn(struct b43legacy_wl *wl, const char *fmt, ...)
		__attribute__((format(printf, 2, 3)));
#if B43legacy_DEBUG
void b43legacydbg(struct b43legacy_wl *wl, const char *fmt, ...)
		__attribute__((format(printf, 2, 3)));
#else /* DEBUG */
# define b43legacydbg(wl, fmt...) do { /* nothing */ } while (0)
#endif /* DEBUG */

/* Macros for printing a value in Q5.2 format */
#define Q52_FMT		"%u.%u"
#define Q52_ARG(q52)	((q52) / 4), (((q52) & 3) * 100 / 4)

#endif /* B43legacy_H_ */
