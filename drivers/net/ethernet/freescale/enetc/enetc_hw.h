/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2017-2019 NXP */

#include <linux/bitops.h>

/* ENETC device IDs */
#define ENETC_DEV_ID_PF	0xe100
#define ENETC_DEV_ID_VF	0xef00

/* ENETC register block BAR */
#define ENETC_BAR_REGS	0

/** SI regs, offset: 0h */
#define ENETC_SIMR	0
#define ENETC_SIMR_EN	BIT(31)
#define ENETC_SIMR_RSSE	BIT(0)
#define ENETC_SICTR0	0x18
#define ENETC_SICTR1	0x1c
#define ENETC_SIPCAPR0	0x20
#define ENETC_SIPCAPR0_RSS	BIT(8)
#define ENETC_SIPCAPR1	0x24
#define ENETC_SITGTGR	0x30
#define ENETC_SIRBGCR	0x38
/* cache attribute registers for transactions initiated by ENETC */
#define ENETC_SICAR0	0x40
#define ENETC_SICAR1	0x44
#define ENETC_SICAR2	0x48
/* rd snoop, no alloc
 * wr snoop, no alloc, partial cache line update for BDs and full cache line
 * update for data
 */
#define ENETC_SICAR_RD_COHERENT	0x2b2b0000
#define ENETC_SICAR_WR_COHERENT	0x00006727
#define ENETC_SICAR_MSI	0x00300030 /* rd/wr device, no snoop, no alloc */

#define ENETC_SIPMAR0	0x80
#define ENETC_SIPMAR1	0x84

/* VF-PF Message passing */
#define ENETC_DEFAULT_MSG_SIZE	1024	/* and max size */
/* msg size encoding: default and max msg value of 1024B encoded as 0 */
static inline u32 enetc_vsi_set_msize(u32 size)
{
	return size < ENETC_DEFAULT_MSG_SIZE ? size >> 5 : 0;
}

#define ENETC_PSIMSGRR	0x204
#define ENETC_PSIMSGRR_MR_MASK	GENMASK(2, 1)
#define ENETC_PSIMSGRR_MR(n) BIT((n) + 1) /* n = VSI index */
#define ENETC_PSIVMSGRCVAR0(n)	(0x210 + (n) * 0x8) /* n = VSI index */
#define ENETC_PSIVMSGRCVAR1(n)	(0x214 + (n) * 0x8)

#define ENETC_VSIMSGSR	0x204	/* RO */
#define ENETC_VSIMSGSR_MB	BIT(0)
#define ENETC_VSIMSGSR_MS	BIT(1)
#define ENETC_VSIMSGSNDAR0	0x210
#define ENETC_VSIMSGSNDAR1	0x214

#define ENETC_SIMSGSR_SET_MC(val) ((val) << 16)
#define ENETC_SIMSGSR_GET_MC(val) ((val) >> 16)

/* SI statistics */
#define ENETC_SIROCT	0x300
#define ENETC_SIRFRM	0x308
#define ENETC_SIRUCA	0x310
#define ENETC_SIRMCA	0x318
#define ENETC_SITOCT	0x320
#define ENETC_SITFRM	0x328
#define ENETC_SITUCA	0x330
#define ENETC_SITMCA	0x338
#define ENETC_RBDCR(n)	(0x8180 + (n) * 0x200)

/* Control BDR regs */
#define ENETC_SICBDRMR		0x800
#define ENETC_SICBDRSR		0x804	/* RO */
#define ENETC_SICBDRBAR0	0x810
#define ENETC_SICBDRBAR1	0x814
#define ENETC_SICBDRPIR		0x818
#define ENETC_SICBDRCIR		0x81c
#define ENETC_SICBDRLENR	0x820

#define ENETC_SICAPR0	0x900
#define ENETC_SICAPR1	0x904

#define ENETC_PSIIER	0xa00
#define ENETC_PSIIER_MR_MASK	GENMASK(2, 1)
#define ENETC_PSIIDR	0xa08
#define ENETC_SITXIDR	0xa18
#define ENETC_SIRXIDR	0xa28
#define ENETC_SIMSIVR	0xa30

#define ENETC_SIMSITRV(n) (0xB00 + (n) * 0x4)
#define ENETC_SIMSIRRV(n) (0xB80 + (n) * 0x4)

#define ENETC_SIUEFDCR	0xe28

#define ENETC_SIRFSCAPR	0x1200
#define ENETC_SIRFSCAPR_GET_NUM_RFS(val) ((val) & 0x7f)
#define ENETC_SIRSSCAPR	0x1600
#define ENETC_SIRSSCAPR_GET_NUM_RSS(val) (BIT((val) & 0xf) * 32)

/** SI BDR sub-blocks, n = 0..7 */
enum enetc_bdr_type {TX, RX};
#define ENETC_BDR_OFF(i)	((i) * 0x200)
#define ENETC_BDR(t, i, r)	(0x8000 + (t) * 0x100 + ENETC_BDR_OFF(i) + (r))
/* RX BDR reg offsets */
#define ENETC_RBMR	0
#define ENETC_RBMR_BDS	BIT(2)
#define ENETC_RBMR_VTE	BIT(5)
#define ENETC_RBMR_EN	BIT(31)
#define ENETC_RBSR	0x4
#define ENETC_RBBSR	0x8
#define ENETC_RBCIR	0xc
#define ENETC_RBBAR0	0x10
#define ENETC_RBBAR1	0x14
#define ENETC_RBPIR	0x18
#define ENETC_RBLENR	0x20
#define ENETC_RBIER	0xa0
#define ENETC_RBIER_RXTIE	BIT(0)
#define ENETC_RBIDR	0xa4
#define ENETC_RBICIR0	0xa8
#define ENETC_RBICIR0_ICEN	BIT(31)

/* TX BDR reg offsets */
#define ENETC_TBMR	0
#define ENETC_TBSR_BUSY	BIT(0)
#define ENETC_TBMR_VIH	BIT(9)
#define ENETC_TBMR_PRIO_MASK		GENMASK(2, 0)
#define ENETC_TBMR_PRIO_SET(val)	val
#define ENETC_TBMR_EN	BIT(31)
#define ENETC_TBSR	0x4
#define ENETC_TBBAR0	0x10
#define ENETC_TBBAR1	0x14
#define ENETC_TBPIR	0x18
#define ENETC_TBCIR	0x1c
#define ENETC_TBCIR_IDX_MASK	0xffff
#define ENETC_TBLENR	0x20
#define ENETC_TBIER	0xa0
#define ENETC_TBIER_TXTIE	BIT(0)
#define ENETC_TBIDR	0xa4
#define ENETC_TBICIR0	0xa8
#define ENETC_TBICIR0_ICEN	BIT(31)

#define ENETC_RTBLENR_LEN(n)	((n) & ~0x7)

/* Port regs, offset: 1_0000h */
#define ENETC_PORT_BASE		0x10000
#define ENETC_PMR		0x0000
#define ENETC_PMR_EN	GENMASK(18, 16)
#define ENETC_PSR		0x0004 /* RO */
#define ENETC_PSIPMR		0x0018
#define ENETC_PSIPMR_SET_UP(n)	BIT(n) /* n = SI index */
#define ENETC_PSIPMR_SET_MP(n)	BIT((n) + 16)
#define ENETC_PSIPVMR		0x001c
#define ENETC_VLAN_PROMISC_MAP_ALL	0x7
#define ENETC_PSIPVMR_SET_VP(simap)	((simap) & 0x7)
#define ENETC_PSIPVMR_SET_VUTA(simap)	(((simap) & 0x7) << 16)
#define ENETC_PSIPMAR0(n)	(0x0100 + (n) * 0x8) /* n = SI index */
#define ENETC_PSIPMAR1(n)	(0x0104 + (n) * 0x8)
#define ENETC_PVCLCTR		0x0208
#define ENETC_VLAN_TYPE_C	BIT(0)
#define ENETC_VLAN_TYPE_S	BIT(1)
#define ENETC_PVCLCTR_OVTPIDL(bmp)	((bmp) & 0xff) /* VLAN_TYPE */
#define ENETC_PSIVLANR(n)	(0x0240 + (n) * 4) /* n = SI index */
#define ENETC_PSIVLAN_EN	BIT(31)
#define ENETC_PSIVLAN_SET_QOS(val)	((u32)(val) << 12)
#define ENETC_PTXMBAR		0x0608
#define ENETC_PCAPR0		0x0900
#define ENETC_PCAPR0_RXBDR(val)	((val) >> 24)
#define ENETC_PCAPR0_TXBDR(val)	(((val) >> 16) & 0xff)
#define ENETC_PCAPR1		0x0904
#define ENETC_PSICFGR0(n)	(0x0940 + (n) * 0xc)  /* n = SI index */
#define ENETC_PSICFGR0_SET_TXBDR(val)	((val) & 0xff)
#define ENETC_PSICFGR0_SET_RXBDR(val)	(((val) & 0xff) << 16)
#define ENETC_PSICFGR0_VTE	BIT(12)
#define ENETC_PSICFGR0_SIVIE	BIT(14)
#define ENETC_PSICFGR0_ASE	BIT(15)
#define ENETC_PSICFGR0_SIVC(bmp)	(((bmp) & 0xff) << 24) /* VLAN_TYPE */

#define ENETC_PTCCBSR0(n)	(0x1110 + (n) * 8) /* n = 0 to 7*/
#define ENETC_PTCCBSR1(n)	(0x1114 + (n) * 8) /* n = 0 to 7*/
#define ENETC_RSSHASH_KEY_SIZE	40
#define ENETC_PRSSK(n)		(0x1410 + (n) * 4) /* n = [0..9] */
#define ENETC_PSIVLANFMR	0x1700
#define ENETC_PSIVLANFMR_VS	BIT(0)
#define ENETC_PRFSMR		0x1800
#define ENETC_PRFSMR_RFSE	BIT(31)
#define ENETC_PRFSCAPR		0x1804
#define ENETC_PRFSCAPR_GET_NUM_RFS(val)	((((val) & 0xf) + 1) * 16)
#define ENETC_PSIRFSCFGR(n)	(0x1814 + (n) * 4) /* n = SI index */
#define ENETC_PFPMR		0x1900
#define ENETC_PFPMR_PMACE	BIT(1)
#define ENETC_PFPMR_MWLM	BIT(0)
#define ENETC_PSIUMHFR0(n, err)	(((err) ? 0x1d08 : 0x1d00) + (n) * 0x10)
#define ENETC_PSIUMHFR1(n)	(0x1d04 + (n) * 0x10)
#define ENETC_PSIMMHFR0(n, err)	(((err) ? 0x1d00 : 0x1d08) + (n) * 0x10)
#define ENETC_PSIMMHFR1(n)	(0x1d0c + (n) * 0x10)
#define ENETC_PSIVHFR0(n)	(0x1e00 + (n) * 8) /* n = SI index */
#define ENETC_PSIVHFR1(n)	(0x1e04 + (n) * 8) /* n = SI index */
#define ENETC_MMCSR		0x1f00
#define ENETC_MMCSR_ME		BIT(16)
#define ENETC_PTCMSDUR(n)	(0x2020 + (n) * 4) /* n = TC index [0..7] */

#define ENETC_PM0_CMD_CFG	0x8008
#define ENETC_PM1_CMD_CFG	0x9008
#define ENETC_PM0_TX_EN		BIT(0)
#define ENETC_PM0_RX_EN		BIT(1)
#define ENETC_PM0_PROMISC	BIT(4)
#define ENETC_PM0_CMD_XGLP	BIT(10)
#define ENETC_PM0_CMD_TXP	BIT(11)
#define ENETC_PM0_CMD_PHY_TX_EN	BIT(15)
#define ENETC_PM0_CMD_SFD	BIT(21)
#define ENETC_PM0_MAXFRM	0x8014
#define ENETC_SET_TX_MTU(val)	((val) << 16)
#define ENETC_SET_MAXFRM(val)	((val) & 0xffff)
#define ENETC_PM0_IF_MODE	0x8300
#define ENETC_PMO_IFM_RG	BIT(2)
#define ENETC_PM0_IFM_RLP	(BIT(5) | BIT(11))
#define ENETC_PM0_IFM_RGAUTO	(BIT(15) | ENETC_PMO_IFM_RG | BIT(1))
#define ENETC_PM0_IFM_XGMII	BIT(12)

/* MAC counters */
#define ENETC_PM0_REOCT		0x8100
#define ENETC_PM0_RALN		0x8110
#define ENETC_PM0_RXPF		0x8118
#define ENETC_PM0_RFRM		0x8120
#define ENETC_PM0_RFCS		0x8128
#define ENETC_PM0_RVLAN		0x8130
#define ENETC_PM0_RERR		0x8138
#define ENETC_PM0_RUCA		0x8140
#define ENETC_PM0_RMCA		0x8148
#define ENETC_PM0_RBCA		0x8150
#define ENETC_PM0_RDRP		0x8158
#define ENETC_PM0_RPKT		0x8160
#define ENETC_PM0_RUND		0x8168
#define ENETC_PM0_R64		0x8170
#define ENETC_PM0_R127		0x8178
#define ENETC_PM0_R255		0x8180
#define ENETC_PM0_R511		0x8188
#define ENETC_PM0_R1023		0x8190
#define ENETC_PM0_R1518		0x8198
#define ENETC_PM0_R1519X	0x81A0
#define ENETC_PM0_ROVR		0x81A8
#define ENETC_PM0_RJBR		0x81B0
#define ENETC_PM0_RFRG		0x81B8
#define ENETC_PM0_RCNP		0x81C0
#define ENETC_PM0_RDRNTP	0x81C8
#define ENETC_PM0_TEOCT		0x8200
#define ENETC_PM0_TOCT		0x8208
#define ENETC_PM0_TCRSE		0x8210
#define ENETC_PM0_TXPF		0x8218
#define ENETC_PM0_TFRM		0x8220
#define ENETC_PM0_TFCS		0x8228
#define ENETC_PM0_TVLAN		0x8230
#define ENETC_PM0_TERR		0x8238
#define ENETC_PM0_TUCA		0x8240
#define ENETC_PM0_TMCA		0x8248
#define ENETC_PM0_TBCA		0x8250
#define ENETC_PM0_TPKT		0x8260
#define ENETC_PM0_TUND		0x8268
#define ENETC_PM0_T127		0x8278
#define ENETC_PM0_T1023		0x8290
#define ENETC_PM0_T1518		0x8298
#define ENETC_PM0_TCNP		0x82C0
#define ENETC_PM0_TDFR		0x82D0
#define ENETC_PM0_TMCOL		0x82D8
#define ENETC_PM0_TSCOL		0x82E0
#define ENETC_PM0_TLCOL		0x82E8
#define ENETC_PM0_TECOL		0x82F0

/* Port counters */
#define ENETC_PICDR(n)		(0x0700 + (n) * 8) /* n = [0..3] */
#define ENETC_PBFDSIR		0x0810
#define ENETC_PFDMSAPR		0x0814
#define ENETC_UFDMF		0x1680
#define ENETC_MFDMF		0x1684
#define ENETC_PUFDVFR		0x1780
#define ENETC_PMFDVFR		0x1784
#define ENETC_PBFDVFR		0x1788

/** Global regs, offset: 2_0000h */
#define ENETC_GLOBAL_BASE	0x20000
#define ENETC_G_EIPBRR0		0x0bf8
#define ENETC_G_EIPBRR1		0x0bfc
#define ENETC_G_EPFBLPR(n)	(0xd00 + 4 * (n))
#define ENETC_G_EPFBLPR1_XGMII	0x80000000

/* PCI device info */
struct enetc_hw {
	/* SI registers, used by all PCI functions */
	void __iomem *reg;
	/* Port registers, PF only */
	void __iomem *port;
	/* IP global registers, PF only */
	void __iomem *global;
};

/* general register accessors */
#define enetc_rd_reg(reg)	ioread32((reg))
#define enetc_wr_reg(reg, val)	iowrite32((val), (reg))
#ifdef ioread64
#define enetc_rd_reg64(reg)	ioread64((reg))
#else
/* using this to read out stats on 32b systems */
static inline u64 enetc_rd_reg64(void __iomem *reg)
{
	u32 low, high, tmp;

	do {
		high = ioread32(reg + 4);
		low = ioread32(reg);
		tmp = ioread32(reg + 4);
	} while (high != tmp);

	return le64_to_cpu((__le64)high << 32 | low);
}
#endif

#define enetc_rd(hw, off)		enetc_rd_reg((hw)->reg + (off))
#define enetc_wr(hw, off, val)		enetc_wr_reg((hw)->reg + (off), val)
#define enetc_rd64(hw, off)		enetc_rd_reg64((hw)->reg + (off))
/* port register accessors - PF only */
#define enetc_port_rd(hw, off)		enetc_rd_reg((hw)->port + (off))
#define enetc_port_wr(hw, off, val)	enetc_wr_reg((hw)->port + (off), val)
/* global register accessors - PF only */
#define enetc_global_rd(hw, off)	enetc_rd_reg((hw)->global + (off))
#define enetc_global_wr(hw, off, val)	enetc_wr_reg((hw)->global + (off), val)
/* BDR register accessors, see ENETC_BDR() */
#define enetc_bdr_rd(hw, t, n, off) \
				enetc_rd(hw, ENETC_BDR(t, n, off))
#define enetc_bdr_wr(hw, t, n, off, val) \
				enetc_wr(hw, ENETC_BDR(t, n, off), val)
#define enetc_txbdr_rd(hw, n, off) enetc_bdr_rd(hw, TX, n, off)
#define enetc_rxbdr_rd(hw, n, off) enetc_bdr_rd(hw, RX, n, off)
#define enetc_txbdr_wr(hw, n, off, val) \
				enetc_bdr_wr(hw, TX, n, off, val)
#define enetc_rxbdr_wr(hw, n, off, val) \
				enetc_bdr_wr(hw, RX, n, off, val)

/* Buffer Descriptors (BD) */
union enetc_tx_bd {
	struct {
		__le64 addr;
		__le16 buf_len;
		__le16 frm_len;
		union {
			struct {
				__le16 l3_csoff;
				u8 l4_csoff;
				u8 flags;
			}; /* default layout */
			__le32 lstatus;
		};
	};
	struct {
		__le32 tstamp;
		__le16 tpid;
		__le16 vid;
		u8 reserved[6];
		u8 e_flags;
		u8 flags;
	} ext; /* Tx BD extension */
};

#define ENETC_TXBD_FLAGS_L4CS	BIT(0)
#define ENETC_TXBD_FLAGS_W	BIT(2)
#define ENETC_TXBD_FLAGS_CSUM	BIT(3)
#define ENETC_TXBD_FLAGS_EX	BIT(6)
#define ENETC_TXBD_FLAGS_F	BIT(7)

static inline void enetc_clear_tx_bd(union enetc_tx_bd *txbd)
{
	memset(txbd, 0, sizeof(*txbd));
}

/* L3 csum flags */
#define ENETC_TXBD_L3_IPCS	BIT(7)
#define ENETC_TXBD_L3_IPV6	BIT(15)

#define ENETC_TXBD_L3_START_MASK	GENMASK(6, 0)
#define ENETC_TXBD_L3_SET_HSIZE(val)	((((val) >> 2) & 0x7f) << 8)

/* Extension flags */
#define ENETC_TXBD_E_FLAGS_VLAN_INS	BIT(0)
#define ENETC_TXBD_E_FLAGS_TWO_STEP_PTP	BIT(2)

static inline __le16 enetc_txbd_l3_csoff(int start, int hdr_sz, u16 l3_flags)
{
	return cpu_to_le16(l3_flags | ENETC_TXBD_L3_SET_HSIZE(hdr_sz) |
			   (start & ENETC_TXBD_L3_START_MASK));
}

/* L4 csum flags */
#define ENETC_TXBD_L4_UDP	BIT(5)
#define ENETC_TXBD_L4_TCP	BIT(6)

union enetc_rx_bd {
	struct {
		__le64 addr;
		u8 reserved[8];
	} w;
	struct {
		__le16 inet_csum;
		__le16 parse_summary;
		__le32 rss_hash;
		__le16 buf_len;
		__le16 vlan_opt;
		union {
			struct {
				__le16 flags;
				__le16 error;
			};
			__le32 lstatus;
		};
	} r;
};

#define ENETC_RXBD_LSTATUS_R	BIT(30)
#define ENETC_RXBD_LSTATUS_F	BIT(31)
#define ENETC_RXBD_ERR_MASK	0xff
#define ENETC_RXBD_LSTATUS(flags)	((flags) << 16)
#define ENETC_RXBD_FLAG_VLAN	BIT(9)
#define ENETC_RXBD_FLAG_TSTMP	BIT(10)

#define ENETC_MAC_ADDR_FILT_CNT	8 /* # of supported entries per port */
#define EMETC_MAC_ADDR_FILT_RES	3 /* # of reserved entries at the beginning */
#define ENETC_MAX_NUM_VFS	2

struct enetc_cbd {
	union {
		struct {
			__le32 addr[2];
			__le32 opt[4];
		};
		__le32 data[6];
	};
	__le16 index;
	__le16 length;
	u8 cmd;
	u8 cls;
	u8 _res;
	u8 status_flags;
};

#define ENETC_CBD_FLAGS_SF	BIT(7) /* short format */
#define ENETC_CBD_STATUS_MASK	0xf

struct enetc_cmd_rfse {
	u8 smac_h[6];
	u8 smac_m[6];
	u8 dmac_h[6];
	u8 dmac_m[6];
	u32 sip_h[4];
	u32 sip_m[4];
	u32 dip_h[4];
	u32 dip_m[4];
	u16 ethtype_h;
	u16 ethtype_m;
	u16 ethtype4_h;
	u16 ethtype4_m;
	u16 sport_h;
	u16 sport_m;
	u16 dport_h;
	u16 dport_m;
	u16 vlan_h;
	u16 vlan_m;
	u8 proto_h;
	u8 proto_m;
	u16 flags;
	u16 result;
	u16 mode;
};

#define ENETC_RFSE_EN	BIT(15)
#define ENETC_RFSE_MODE_BD	2

static inline void enetc_get_primary_mac_addr(struct enetc_hw *hw, u8 *addr)
{
	*(u32 *)addr = __raw_readl(hw->reg + ENETC_SIPMAR0);
	*(u16 *)(addr + 4) = __raw_readw(hw->reg + ENETC_SIPMAR1);
}

#define ENETC_SI_INT_IDX	0
/* base index for Rx/Tx interrupts */
#define ENETC_BDR_INT_BASE_IDX	1

/* Messaging */

/* Command completion status */
enum enetc_msg_cmd_status {
	ENETC_MSG_CMD_STATUS_OK,
	ENETC_MSG_CMD_STATUS_FAIL
};

/* VSI-PSI command message types */
enum enetc_msg_cmd_type {
	ENETC_MSG_CMD_MNG_MAC = 1, /* manage MAC address */
	ENETC_MSG_CMD_MNG_RX_MAC_FILTER,/* manage RX MAC table */
	ENETC_MSG_CMD_MNG_RX_VLAN_FILTER /* manage RX VLAN table */
};

/* VSI-PSI command action types */
enum enetc_msg_cmd_action_type {
	ENETC_MSG_CMD_MNG_ADD = 1,
	ENETC_MSG_CMD_MNG_REMOVE
};

/* PSI-VSI command header format */
struct enetc_msg_cmd_header {
	u16 type;	/* command class type */
	u16 id;		/* denotes the specific required action */
};

/* Common H/W utility functions */

static inline void enetc_enable_rxvlan(struct enetc_hw *hw, int si_idx,
				       bool en)
{
	u32 val = enetc_rxbdr_rd(hw, si_idx, ENETC_RBMR);

	val = (val & ~ENETC_RBMR_VTE) | (en ? ENETC_RBMR_VTE : 0);
	enetc_rxbdr_wr(hw, si_idx, ENETC_RBMR, val);
}

static inline void enetc_enable_txvlan(struct enetc_hw *hw, int si_idx,
				       bool en)
{
	u32 val = enetc_txbdr_rd(hw, si_idx, ENETC_TBMR);

	val = (val & ~ENETC_TBMR_VIH) | (en ? ENETC_TBMR_VIH : 0);
	enetc_txbdr_wr(hw, si_idx, ENETC_TBMR, val);
}
