/* SPDX-License-Identifier: GPL-2.0 */
/*  SuperH Ethernet device driver
 *
 *  Copyright (C) 2006-2012 Nobuhiro Iwamatsu
 *  Copyright (C) 2008-2012 Renesas Solutions Corp.
 */

#ifndef __SH_ETH_H__
#define __SH_ETH_H__

#define CARDNAME	"sh-eth"
#define TX_TIMEOUT	(5*HZ)
#define TX_RING_SIZE	64	/* Tx ring size */
#define RX_RING_SIZE	64	/* Rx ring size */
#define TX_RING_MIN	64
#define RX_RING_MIN	64
#define TX_RING_MAX	1024
#define RX_RING_MAX	1024
#define PKT_BUF_SZ	1538
#define SH_ETH_TSU_TIMEOUT_MS	500
#define SH_ETH_TSU_CAM_ENTRIES	32

enum {
	/* IMPORTANT: To keep ethtool register dump working, add new
	 * register names immediately before SH_ETH_MAX_REGISTER_OFFSET.
	 */

	/* E-DMAC registers */
	EDSR = 0,
	EDMR,
	EDTRR,
	EDRRR,
	EESR,
	EESIPR,
	TDLAR,
	TDFAR,
	TDFXR,
	TDFFR,
	RDLAR,
	RDFAR,
	RDFXR,
	RDFFR,
	TRSCER,
	RMFCR,
	TFTR,
	FDR,
	RMCR,
	EDOCR,
	TFUCR,
	RFOCR,
	RMIIMODE,
	FCFTR,
	RPADIR,
	TRIMD,
	RBWAR,
	TBRAR,

	/* Ether registers */
	ECMR,
	ECSR,
	ECSIPR,
	PIR,
	PSR,
	RDMLR,
	PIPR,
	RFLR,
	IPGR,
	APR,
	MPR,
	PFTCR,
	PFRCR,
	RFCR,
	RFCF,
	TPAUSER,
	TPAUSECR,
	BCFR,
	BCFRR,
	GECMR,
	BCULR,
	MAHR,
	MALR,
	TROCR,
	CDCR,
	LCCR,
	CNDCR,
	CEFCR,
	FRECR,
	TSFRCR,
	TLFRCR,
	CERCR,
	CEECR,
	MAFCR,
	RTRATE,
	CSMR,
	RMII_MII,

	/* TSU Absolute address */
	ARSTR,
	TSU_CTRST,
	TSU_FWEN0,
	TSU_FWEN1,
	TSU_FCM,
	TSU_BSYSL0,
	TSU_BSYSL1,
	TSU_PRISL0,
	TSU_PRISL1,
	TSU_FWSL0,
	TSU_FWSL1,
	TSU_FWSLC,
	TSU_QTAG0,			/* Same as TSU_QTAGM0 */
	TSU_QTAG1,			/* Same as TSU_QTAGM1 */
	TSU_QTAGM0,
	TSU_QTAGM1,
	TSU_FWSR,
	TSU_FWINMK,
	TSU_ADQT0,
	TSU_ADQT1,
	TSU_VTAG0,
	TSU_VTAG1,
	TSU_ADSBSY,
	TSU_TEN,
	TSU_POST1,
	TSU_POST2,
	TSU_POST3,
	TSU_POST4,
	TSU_ADRH0,
	/* TSU_ADR{H,L}{0..31} are assumed to be contiguous */

	TXNLCR0,
	TXALCR0,
	RXNLCR0,
	RXALCR0,
	FWNLCR0,
	FWALCR0,
	TXNLCR1,
	TXALCR1,
	RXNLCR1,
	RXALCR1,
	FWNLCR1,
	FWALCR1,

	/* This value must be written at last. */
	SH_ETH_MAX_REGISTER_OFFSET,
};

enum {
	SH_ETH_REG_GIGABIT,
	SH_ETH_REG_FAST_RZ,
	SH_ETH_REG_FAST_RCAR,
	SH_ETH_REG_FAST_SH4,
	SH_ETH_REG_FAST_SH3_SH2
};

/* Driver's parameters */
#if defined(CONFIG_CPU_SH4) || defined(CONFIG_ARCH_RENESAS)
#define SH_ETH_RX_ALIGN		32
#else
#define SH_ETH_RX_ALIGN		2
#endif

/* Register's bits
 */
/* EDSR : sh7734, sh7757, sh7763, r8a7740, and r7s72100 only */
enum EDSR_BIT {
	EDSR_ENT = 0x01, EDSR_ENR = 0x02,
};
#define EDSR_ENALL (EDSR_ENT|EDSR_ENR)

/* GECMR : sh7734, sh7763 and r8a7740 only */
enum GECMR_BIT {
	GECMR_10 = 0x0, GECMR_100 = 0x04, GECMR_1000 = 0x01,
};

/* EDMR */
enum DMAC_M_BIT {
	EDMR_NBST = 0x80,
	EDMR_EL = 0x40, /* Litte endian */
	EDMR_DL1 = 0x20, EDMR_DL0 = 0x10,
	EDMR_SRST_GETHER = 0x03,
	EDMR_SRST_ETHER = 0x01,
};

/* EDTRR */
enum DMAC_T_BIT {
	EDTRR_TRNS_GETHER = 0x03,
	EDTRR_TRNS_ETHER = 0x01,
};

/* EDRRR */
enum EDRRR_R_BIT {
	EDRRR_R = 0x01,
};

/* TPAUSER */
enum TPAUSER_BIT {
	TPAUSER_TPAUSE = 0x0000ffff,
	TPAUSER_UNLIMITED = 0,
};

/* BCFR */
enum BCFR_BIT {
	BCFR_RPAUSE = 0x0000ffff,
	BCFR_UNLIMITED = 0,
};

/* PIR */
enum PIR_BIT {
	PIR_MDI = 0x08, PIR_MDO = 0x04, PIR_MMD = 0x02, PIR_MDC = 0x01,
};

/* PSR */
enum PHY_STATUS_BIT { PHY_ST_LINK = 0x01, };

/* EESR */
enum EESR_BIT {
	EESR_TWB1	= 0x80000000,
	EESR_TWB	= 0x40000000,	/* same as TWB0 */
	EESR_TC1	= 0x20000000,
	EESR_TUC	= 0x10000000,
	EESR_ROC	= 0x08000000,
	EESR_TABT	= 0x04000000,
	EESR_RABT	= 0x02000000,
	EESR_RFRMER	= 0x01000000,	/* same as RFCOF */
	EESR_ADE	= 0x00800000,
	EESR_ECI	= 0x00400000,
	EESR_FTC	= 0x00200000,	/* same as TC or TC0 */
	EESR_TDE	= 0x00100000,
	EESR_TFE	= 0x00080000,	/* same as TFUF */
	EESR_FRC	= 0x00040000,	/* same as FR */
	EESR_RDE	= 0x00020000,
	EESR_RFE	= 0x00010000,
	EESR_CND	= 0x00000800,
	EESR_DLC	= 0x00000400,
	EESR_CD		= 0x00000200,
	EESR_TRO	= 0x00000100,
	EESR_RMAF	= 0x00000080,
	EESR_CEEF	= 0x00000040,
	EESR_CELF	= 0x00000020,
	EESR_RRF	= 0x00000010,
	EESR_RTLF	= 0x00000008,
	EESR_RTSF	= 0x00000004,
	EESR_PRE	= 0x00000002,
	EESR_CERF	= 0x00000001,
};

#define EESR_RX_CHECK		(EESR_FRC  | /* Frame recv */		\
				 EESR_RMAF | /* Multicast address recv */ \
				 EESR_RRF  | /* Bit frame recv */	\
				 EESR_RTLF | /* Long frame recv */	\
				 EESR_RTSF | /* Short frame recv */	\
				 EESR_PRE  | /* PHY-LSI recv error */	\
				 EESR_CERF)  /* Recv frame CRC error */

#define DEFAULT_TX_CHECK	(EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | \
				 EESR_TRO)
#define DEFAULT_EESR_ERR_CHECK	(EESR_TWB | EESR_TABT | EESR_RABT | EESR_RFE | \
				 EESR_RDE | EESR_RFRMER | EESR_ADE | \
				 EESR_TFE | EESR_TDE)

/* EESIPR */
enum EESIPR_BIT {
	EESIPR_TWB1IP	= 0x80000000,
	EESIPR_TWBIP	= 0x40000000,	/* same as TWB0IP */
	EESIPR_TC1IP	= 0x20000000,
	EESIPR_TUCIP	= 0x10000000,
	EESIPR_ROCIP	= 0x08000000,
	EESIPR_TABTIP	= 0x04000000,
	EESIPR_RABTIP	= 0x02000000,
	EESIPR_RFCOFIP	= 0x01000000,
	EESIPR_ADEIP	= 0x00800000,
	EESIPR_ECIIP	= 0x00400000,
	EESIPR_FTCIP	= 0x00200000,	/* same as TC0IP */
	EESIPR_TDEIP	= 0x00100000,
	EESIPR_TFUFIP	= 0x00080000,
	EESIPR_FRIP	= 0x00040000,
	EESIPR_RDEIP	= 0x00020000,
	EESIPR_RFOFIP	= 0x00010000,
	EESIPR_CNDIP	= 0x00000800,
	EESIPR_DLCIP	= 0x00000400,
	EESIPR_CDIP	= 0x00000200,
	EESIPR_TROIP	= 0x00000100,
	EESIPR_RMAFIP	= 0x00000080,
	EESIPR_CEEFIP	= 0x00000040,
	EESIPR_CELFIP	= 0x00000020,
	EESIPR_RRFIP	= 0x00000010,
	EESIPR_RTLFIP	= 0x00000008,
	EESIPR_RTSFIP	= 0x00000004,
	EESIPR_PREIP	= 0x00000002,
	EESIPR_CERFIP	= 0x00000001,
};

/* Receive descriptor 0 bits */
enum RD_STS_BIT {
	RD_RACT = 0x80000000, RD_RDLE = 0x40000000,
	RD_RFP1 = 0x20000000, RD_RFP0 = 0x10000000,
	RD_RFE = 0x08000000, RD_RFS10 = 0x00000200,
	RD_RFS9 = 0x00000100, RD_RFS8 = 0x00000080,
	RD_RFS7 = 0x00000040, RD_RFS6 = 0x00000020,
	RD_RFS5 = 0x00000010, RD_RFS4 = 0x00000008,
	RD_RFS3 = 0x00000004, RD_RFS2 = 0x00000002,
	RD_RFS1 = 0x00000001,
};
#define RDF1ST	RD_RFP1
#define RDFEND	RD_RFP0
#define RD_RFP	(RD_RFP1|RD_RFP0)

/* Receive descriptor 1 bits */
enum RD_LEN_BIT {
	RD_RFL	= 0x0000ffff,	/* receive frame  length */
	RD_RBL	= 0xffff0000,	/* receive buffer length */
};

/* FCFTR */
enum FCFTR_BIT {
	FCFTR_RFF2 = 0x00040000, FCFTR_RFF1 = 0x00020000,
	FCFTR_RFF0 = 0x00010000, FCFTR_RFD2 = 0x00000004,
	FCFTR_RFD1 = 0x00000002, FCFTR_RFD0 = 0x00000001,
};
#define DEFAULT_FIFO_F_D_RFF	(FCFTR_RFF2 | FCFTR_RFF1 | FCFTR_RFF0)
#define DEFAULT_FIFO_F_D_RFD	(FCFTR_RFD2 | FCFTR_RFD1 | FCFTR_RFD0)

/* Transmit descriptor 0 bits */
enum TD_STS_BIT {
	TD_TACT = 0x80000000, TD_TDLE = 0x40000000,
	TD_TFP1 = 0x20000000, TD_TFP0 = 0x10000000,
	TD_TFE  = 0x08000000, TD_TWBI = 0x04000000,
};
#define TDF1ST	TD_TFP1
#define TDFEND	TD_TFP0
#define TD_TFP	(TD_TFP1|TD_TFP0)

/* Transmit descriptor 1 bits */
enum TD_LEN_BIT {
	TD_TBL	= 0xffff0000,	/* transmit buffer length */
};

/* RMCR */
enum RMCR_BIT {
	RMCR_RNC = 0x00000001,
};

/* ECMR */
enum FELIC_MODE_BIT {
	ECMR_TRCCM = 0x04000000, ECMR_RCSC = 0x00800000,
	ECMR_DPAD = 0x00200000, ECMR_RZPF = 0x00100000,
	ECMR_ZPF = 0x00080000, ECMR_PFR = 0x00040000, ECMR_RXF = 0x00020000,
	ECMR_TXF = 0x00010000, ECMR_MCT = 0x00002000, ECMR_PRCEF = 0x00001000,
	ECMR_MPDE = 0x00000200, ECMR_RE = 0x00000040, ECMR_TE = 0x00000020,
	ECMR_RTM = 0x00000010, ECMR_ILB = 0x00000008, ECMR_ELB = 0x00000004,
	ECMR_DM = 0x00000002, ECMR_PRM = 0x00000001,
};

/* ECSR */
enum ECSR_STATUS_BIT {
	ECSR_BRCRX = 0x20, ECSR_PSRTO = 0x10,
	ECSR_LCHNG = 0x04,
	ECSR_MPD = 0x02, ECSR_ICD = 0x01,
};

#define DEFAULT_ECSR_INIT	(ECSR_BRCRX | ECSR_PSRTO | ECSR_LCHNG | \
				 ECSR_ICD | ECSIPR_MPDIP)

/* ECSIPR */
enum ECSIPR_STATUS_MASK_BIT {
	ECSIPR_BRCRXIP = 0x20, ECSIPR_PSRTOIP = 0x10,
	ECSIPR_LCHNGIP = 0x04,
	ECSIPR_MPDIP = 0x02, ECSIPR_ICDIP = 0x01,
};

#define DEFAULT_ECSIPR_INIT	(ECSIPR_BRCRXIP | ECSIPR_PSRTOIP | \
				 ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP)

/* APR */
enum APR_BIT {
	APR_AP = 0x0000ffff,
};

/* MPR */
enum MPR_BIT {
	MPR_MP = 0x0000ffff,
};

/* TRSCER */
enum DESC_I_BIT {
	DESC_I_TINT4 = 0x0800, DESC_I_TINT3 = 0x0400, DESC_I_TINT2 = 0x0200,
	DESC_I_TINT1 = 0x0100, DESC_I_RINT8 = 0x0080, DESC_I_RINT5 = 0x0010,
	DESC_I_RINT4 = 0x0008, DESC_I_RINT3 = 0x0004, DESC_I_RINT2 = 0x0002,
	DESC_I_RINT1 = 0x0001,
};

#define DEFAULT_TRSCER_ERR_MASK (DESC_I_RINT8 | DESC_I_RINT5 | DESC_I_TINT2)

/* RPADIR */
enum RPADIR_BIT {
	RPADIR_PADS = 0x1f0000, RPADIR_PADR = 0xffff,
};

/* FDR */
#define DEFAULT_FDR_INIT	0x00000707

/* ARSTR */
enum ARSTR_BIT { ARSTR_ARST = 0x00000001, };

/* TSU_FWEN0 */
enum TSU_FWEN0_BIT {
	TSU_FWEN0_0 = 0x00000001,
};

/* TSU_ADSBSY */
enum TSU_ADSBSY_BIT {
	TSU_ADSBSY_0 = 0x00000001,
};

/* TSU_TEN */
enum TSU_TEN_BIT {
	TSU_TEN_0 = 0x80000000,
};

/* TSU_FWSL0 */
enum TSU_FWSL0_BIT {
	TSU_FWSL0_FW50 = 0x1000, TSU_FWSL0_FW40 = 0x0800,
	TSU_FWSL0_FW30 = 0x0400, TSU_FWSL0_FW20 = 0x0200,
	TSU_FWSL0_FW10 = 0x0100, TSU_FWSL0_RMSA0 = 0x0010,
};

/* TSU_FWSLC */
enum TSU_FWSLC_BIT {
	TSU_FWSLC_POSTENU = 0x2000, TSU_FWSLC_POSTENL = 0x1000,
	TSU_FWSLC_CAMSEL03 = 0x0080, TSU_FWSLC_CAMSEL02 = 0x0040,
	TSU_FWSLC_CAMSEL01 = 0x0020, TSU_FWSLC_CAMSEL00 = 0x0010,
	TSU_FWSLC_CAMSEL13 = 0x0008, TSU_FWSLC_CAMSEL12 = 0x0004,
	TSU_FWSLC_CAMSEL11 = 0x0002, TSU_FWSLC_CAMSEL10 = 0x0001,
};

/* TSU_VTAGn */
#define TSU_VTAG_ENABLE		0x80000000
#define TSU_VTAG_VID_MASK	0x00000fff

/* The sh ether Tx buffer descriptors.
 * This structure should be 20 bytes.
 */
struct sh_eth_txdesc {
	u32 status;		/* TD0 */
	u32 len;		/* TD1 */
	u32 addr;		/* TD2 */
	u32 pad0;		/* padding data */
} __aligned(2) __packed;

/* The sh ether Rx buffer descriptors.
 * This structure should be 20 bytes.
 */
struct sh_eth_rxdesc {
	u32 status;		/* RD0 */
	u32 len;		/* RD1 */
	u32 addr;		/* RD2 */
	u32 pad0;		/* padding data */
} __aligned(2) __packed;

/* This structure is used by each CPU dependency handling. */
struct sh_eth_cpu_data {
	/* mandatory functions */
	int (*soft_reset)(struct net_device *ndev);

	/* optional functions */
	void (*chip_reset)(struct net_device *ndev);
	void (*set_duplex)(struct net_device *ndev);
	void (*set_rate)(struct net_device *ndev);

	/* mandatory initialize value */
	int register_type;
	u32 edtrr_trns;
	u32 eesipr_value;

	/* optional initialize value */
	u32 ecsr_value;
	u32 ecsipr_value;
	u32 fdr_value;
	u32 fcftr_value;

	/* interrupt checking mask */
	u32 tx_check;
	u32 eesr_err_check;

	/* Error mask */
	u32 trscer_err_mask;

	/* hardware features */
	unsigned long irq_flags; /* IRQ configuration flags */
	unsigned no_psr:1;	/* EtherC DOES NOT have PSR */
	unsigned apr:1;		/* EtherC has APR */
	unsigned mpr:1;		/* EtherC has MPR */
	unsigned tpauser:1;	/* EtherC has TPAUSER */
	unsigned bculr:1;	/* EtherC has BCULR */
	unsigned tsu:1;		/* EtherC has TSU */
	unsigned hw_swap:1;	/* E-DMAC has DE bit in EDMR */
	unsigned nbst:1;	/* E-DMAC has NBST bit in EDMR */
	unsigned rpadir:1;	/* E-DMAC has RPADIR */
	unsigned no_trimd:1;	/* E-DMAC DOES NOT have TRIMD */
	unsigned no_ade:1;	/* E-DMAC DOES NOT have ADE bit in EESR */
	unsigned no_xdfar:1;	/* E-DMAC DOES NOT have RDFAR/TDFAR */
	unsigned xdfar_rw:1;	/* E-DMAC has writeable RDFAR/TDFAR */
	unsigned csmr:1;	/* E-DMAC has CSMR */
	unsigned rx_csum:1;	/* EtherC has ECMR.RCSC */
	unsigned select_mii:1;	/* EtherC has RMII_MII (MII select register) */
	unsigned rmiimode:1;	/* EtherC has RMIIMODE register */
	unsigned rtrate:1;	/* EtherC has RTRATE register */
	unsigned magic:1;	/* EtherC has ECMR.MPDE and ECSR.MPD */
	unsigned no_tx_cntrs:1;	/* EtherC DOES NOT have TX error counters */
	unsigned cexcr:1;	/* EtherC has CERCR/CEECR */
	unsigned dual_port:1;	/* Dual EtherC/E-DMAC */
};

struct sh_eth_private {
	struct platform_device *pdev;
	struct sh_eth_cpu_data *cd;
	const u16 *reg_offset;
	void __iomem *addr;
	void __iomem *tsu_addr;
	struct clk *clk;
	u32 num_rx_ring;
	u32 num_tx_ring;
	dma_addr_t rx_desc_dma;
	dma_addr_t tx_desc_dma;
	struct sh_eth_rxdesc *rx_ring;
	struct sh_eth_txdesc *tx_ring;
	struct sk_buff **rx_skbuff;
	struct sk_buff **tx_skbuff;
	spinlock_t lock;		/* Register access lock */
	u32 cur_rx, dirty_rx;		/* Producer/consumer ring indices */
	u32 cur_tx, dirty_tx;
	u32 rx_buf_sz;			/* Based on MTU+slack. */
	struct napi_struct napi;
	bool irq_enabled;
	/* MII transceiver section. */
	u32 phy_id;			/* PHY ID */
	struct mii_bus *mii_bus;	/* MDIO bus control */
	int link;
	phy_interface_t phy_interface;
	int msg_enable;
	int speed;
	int duplex;
	int port;			/* for TSU */
	int vlan_num_ids;		/* for VLAN tag filter */

	unsigned no_ether_link:1;
	unsigned ether_link_active_low:1;
	unsigned is_opened:1;
	unsigned wol_enabled:1;
};

#endif	/* #ifndef __SH_ETH_H__ */
