/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  ahci.h - Common AHCI SATA definitions and declarations
 *
 *  Maintained by:  Tejun Heo <tj@kernel.org>
 *    		    Please ALWAYS copy linux-ide@vger.kernel.org
 *		    on emails.
 *
 *  Copyright 2004-2005 Red Hat, Inc.
 *
 * libata documentation is available via 'make {ps|pdf}docs',
 * as Documentation/driver-api/libata.rst
 *
 * AHCI hardware documentation:
 * http://www.intel.com/technology/serialata/pdf/rev1_0.pdf
 * http://www.intel.com/technology/serialata/pdf/rev1_1.pdf
 */

#ifndef _AHCI_H
#define _AHCI_H

#include <linux/pci.h>
#include <linux/clk.h>
#include <linux/libata.h>
#include <linux/phy/phy.h>
#include <linux/regulator/consumer.h>
#include <linux/bits.h>

/* Enclosure Management Control */
#define EM_CTRL_MSG_TYPE              0x000f0000

/* Enclosure Management LED Message Type */
#define EM_MSG_LED_HBA_PORT           0x0000000f
#define EM_MSG_LED_PMP_SLOT           0x0000ff00
#define EM_MSG_LED_VALUE              0xffff0000
#define EM_MSG_LED_VALUE_ACTIVITY     0x00070000
#define EM_MSG_LED_VALUE_OFF          0xfff80000
#define EM_MSG_LED_VALUE_ON           0x00010000

enum {
	AHCI_MAX_PORTS		= 32,
	AHCI_MAX_SG		= 168, /* hardware max is 64K */
	AHCI_DMA_BOUNDARY	= 0xffffffff,
	AHCI_MAX_CMDS		= 32,
	AHCI_CMD_SZ		= 32,
	AHCI_CMD_SLOT_SZ	= AHCI_MAX_CMDS * AHCI_CMD_SZ,
	AHCI_RX_FIS_SZ		= 256,
	AHCI_CMD_TBL_CDB	= 0x40,
	AHCI_CMD_TBL_HDR_SZ	= 0x80,
	AHCI_CMD_TBL_SZ		= AHCI_CMD_TBL_HDR_SZ + (AHCI_MAX_SG * 16),
	AHCI_CMD_TBL_AR_SZ	= AHCI_CMD_TBL_SZ * AHCI_MAX_CMDS,
	AHCI_PORT_PRIV_DMA_SZ	= AHCI_CMD_SLOT_SZ + AHCI_CMD_TBL_AR_SZ +
				  AHCI_RX_FIS_SZ,
	AHCI_PORT_PRIV_FBS_DMA_SZ	= AHCI_CMD_SLOT_SZ +
					  AHCI_CMD_TBL_AR_SZ +
					  (AHCI_RX_FIS_SZ * 16),
	AHCI_IRQ_ON_SG		= BIT(31),
	AHCI_CMD_ATAPI		= BIT(5),
	AHCI_CMD_WRITE		= BIT(6),
	AHCI_CMD_PREFETCH	= BIT(7),
	AHCI_CMD_RESET		= BIT(8),
	AHCI_CMD_CLR_BUSY	= BIT(10),

	RX_FIS_PIO_SETUP	= 0x20,	/* offset of PIO Setup FIS data */
	RX_FIS_D2H_REG		= 0x40,	/* offset of D2H Register FIS data */
	RX_FIS_SDB		= 0x58, /* offset of SDB FIS data */
	RX_FIS_UNK		= 0x60, /* offset of Unknown FIS data */

	/* global controller registers */
	HOST_CAP		= 0x00, /* host capabilities */
	HOST_CTL		= 0x04, /* global host control */
	HOST_IRQ_STAT		= 0x08, /* interrupt status */
	HOST_PORTS_IMPL		= 0x0c, /* bitmap of implemented ports */
	HOST_VERSION		= 0x10, /* AHCI spec. version compliancy */
	HOST_EM_LOC		= 0x1c, /* Enclosure Management location */
	HOST_EM_CTL		= 0x20, /* Enclosure Management Control */
	HOST_CAP2		= 0x24, /* host capabilities, extended */

	/* HOST_CTL bits */
	HOST_RESET		= BIT(0),  /* reset controller; self-clear */
	HOST_IRQ_EN		= BIT(1),  /* global IRQ enable */
	HOST_MRSM		= BIT(2),  /* MSI Revert to Single Message */
	HOST_AHCI_EN		= BIT(31), /* AHCI enabled */

	/* HOST_CAP bits */
	HOST_CAP_SXS		= BIT(5),  /* Supports External SATA */
	HOST_CAP_EMS		= BIT(6),  /* Enclosure Management support */
	HOST_CAP_CCC		= BIT(7),  /* Command Completion Coalescing */
	HOST_CAP_PART		= BIT(13), /* Partial state capable */
	HOST_CAP_SSC		= BIT(14), /* Slumber state capable */
	HOST_CAP_PIO_MULTI	= BIT(15), /* PIO multiple DRQ support */
	HOST_CAP_FBS		= BIT(16), /* FIS-based switching support */
	HOST_CAP_PMP		= BIT(17), /* Port Multiplier support */
	HOST_CAP_ONLY		= BIT(18), /* Supports AHCI mode only */
	HOST_CAP_CLO		= BIT(24), /* Command List Override support */
	HOST_CAP_LED		= BIT(25), /* Supports activity LED */
	HOST_CAP_ALPM		= BIT(26), /* Aggressive Link PM support */
	HOST_CAP_SSS		= BIT(27), /* Staggered Spin-up */
	HOST_CAP_MPS		= BIT(28), /* Mechanical presence switch */
	HOST_CAP_SNTF		= BIT(29), /* SNotification register */
	HOST_CAP_NCQ		= BIT(30), /* Native Command Queueing */
	HOST_CAP_64		= BIT(31), /* PCI DAC (64-bit DMA) support */

	/* HOST_CAP2 bits */
	HOST_CAP2_BOH		= BIT(0),  /* BIOS/OS handoff supported */
	HOST_CAP2_NVMHCI	= BIT(1),  /* NVMHCI supported */
	HOST_CAP2_APST		= BIT(2),  /* Automatic partial to slumber */
	HOST_CAP2_SDS		= BIT(3),  /* Support device sleep */
	HOST_CAP2_SADM		= BIT(4),  /* Support aggressive DevSlp */
	HOST_CAP2_DESO		= BIT(5),  /* DevSlp from slumber only */

	/* registers for each SATA port */
	PORT_LST_ADDR		= 0x00, /* command list DMA addr */
	PORT_LST_ADDR_HI	= 0x04, /* command list DMA addr hi */
	PORT_FIS_ADDR		= 0x08, /* FIS rx buf addr */
	PORT_FIS_ADDR_HI	= 0x0c, /* FIS rx buf addr hi */
	PORT_IRQ_STAT		= 0x10, /* interrupt status */
	PORT_IRQ_MASK		= 0x14, /* interrupt enable/disable mask */
	PORT_CMD		= 0x18, /* port command */
	PORT_TFDATA		= 0x20,	/* taskfile data */
	PORT_SIG		= 0x24,	/* device TF signature */
	PORT_CMD_ISSUE		= 0x38, /* command issue */
	PORT_SCR_STAT		= 0x28, /* SATA phy register: SStatus */
	PORT_SCR_CTL		= 0x2c, /* SATA phy register: SControl */
	PORT_SCR_ERR		= 0x30, /* SATA phy register: SError */
	PORT_SCR_ACT		= 0x34, /* SATA phy register: SActive */
	PORT_SCR_NTF		= 0x3c, /* SATA phy register: SNotification */
	PORT_FBS		= 0x40, /* FIS-based Switching */
	PORT_DEVSLP		= 0x44, /* device sleep */

	/* PORT_IRQ_{STAT,MASK} bits */
	PORT_IRQ_COLD_PRES	= BIT(31), /* cold presence detect */
	PORT_IRQ_TF_ERR		= BIT(30), /* task file error */
	PORT_IRQ_HBUS_ERR	= BIT(29), /* host bus fatal error */
	PORT_IRQ_HBUS_DATA_ERR	= BIT(28), /* host bus data error */
	PORT_IRQ_IF_ERR		= BIT(27), /* interface fatal error */
	PORT_IRQ_IF_NONFATAL	= BIT(26), /* interface non-fatal error */
	PORT_IRQ_OVERFLOW	= BIT(24), /* xfer exhausted available S/G */
	PORT_IRQ_BAD_PMP	= BIT(23), /* incorrect port multiplier */

	PORT_IRQ_PHYRDY		= BIT(22), /* PhyRdy changed */
	PORT_IRQ_DMPS		= BIT(7),  /* mechanical presence status */
	PORT_IRQ_CONNECT	= BIT(6),  /* port connect change status */
	PORT_IRQ_SG_DONE	= BIT(5),  /* descriptor processed */
	PORT_IRQ_UNK_FIS	= BIT(4),  /* unknown FIS rx'd */
	PORT_IRQ_SDB_FIS	= BIT(3),  /* Set Device Bits FIS rx'd */
	PORT_IRQ_DMAS_FIS	= BIT(2),  /* DMA Setup FIS rx'd */
	PORT_IRQ_PIOS_FIS	= BIT(1),  /* PIO Setup FIS rx'd */
	PORT_IRQ_D2H_REG_FIS	= BIT(0),  /* D2H Register FIS rx'd */

	PORT_IRQ_FREEZE		= PORT_IRQ_HBUS_ERR |
				  PORT_IRQ_IF_ERR |
				  PORT_IRQ_CONNECT |
				  PORT_IRQ_PHYRDY |
				  PORT_IRQ_UNK_FIS |
				  PORT_IRQ_BAD_PMP,
	PORT_IRQ_ERROR		= PORT_IRQ_FREEZE |
				  PORT_IRQ_TF_ERR |
				  PORT_IRQ_HBUS_DATA_ERR,
	DEF_PORT_IRQ		= PORT_IRQ_ERROR | PORT_IRQ_SG_DONE |
				  PORT_IRQ_SDB_FIS | PORT_IRQ_DMAS_FIS |
				  PORT_IRQ_PIOS_FIS | PORT_IRQ_D2H_REG_FIS,

	/* PORT_CMD bits */
	PORT_CMD_ASP		= BIT(27), /* Aggressive Slumber/Partial */
	PORT_CMD_ALPE		= BIT(26), /* Aggressive Link PM enable */
	PORT_CMD_ATAPI		= BIT(24), /* Device is ATAPI */
	PORT_CMD_FBSCP		= BIT(22), /* FBS Capable Port */
	PORT_CMD_ESP		= BIT(21), /* External Sata Port */
	PORT_CMD_CPD		= BIT(20), /* Cold Presence Detection */
	PORT_CMD_MPSP		= BIT(19), /* Mechanical Presence Switch */
	PORT_CMD_HPCP		= BIT(18), /* HotPlug Capable Port */
	PORT_CMD_PMP		= BIT(17), /* PMP attached */
	PORT_CMD_LIST_ON	= BIT(15), /* cmd list DMA engine running */
	PORT_CMD_FIS_ON		= BIT(14), /* FIS DMA engine running */
	PORT_CMD_FIS_RX		= BIT(4),  /* Enable FIS receive DMA engine */
	PORT_CMD_CLO		= BIT(3),  /* Command list override */
	PORT_CMD_POWER_ON	= BIT(2),  /* Power up device */
	PORT_CMD_SPIN_UP	= BIT(1),  /* Spin up device */
	PORT_CMD_START		= BIT(0),  /* Enable port DMA engine */

	PORT_CMD_ICC_MASK	= (0xfu << 28), /* i/f ICC state mask */
	PORT_CMD_ICC_ACTIVE	= (0x1u << 28), /* Put i/f in active state */
	PORT_CMD_ICC_PARTIAL	= (0x2u << 28), /* Put i/f in partial state */
	PORT_CMD_ICC_SLUMBER	= (0x6u << 28), /* Put i/f in slumber state */

	/* PORT_CMD capabilities mask */
	PORT_CMD_CAP		= PORT_CMD_HPCP | PORT_CMD_MPSP |
				  PORT_CMD_CPD | PORT_CMD_ESP | PORT_CMD_FBSCP,

	/* PORT_FBS bits */
	PORT_FBS_DWE_OFFSET	= 16, /* FBS device with error offset */
	PORT_FBS_ADO_OFFSET	= 12, /* FBS active dev optimization offset */
	PORT_FBS_DEV_OFFSET	= 8,  /* FBS device to issue offset */
	PORT_FBS_DEV_MASK	= (0xf << PORT_FBS_DEV_OFFSET),  /* FBS.DEV */
	PORT_FBS_SDE		= BIT(2), /* FBS single device error */
	PORT_FBS_DEC		= BIT(1), /* FBS device error clear */
	PORT_FBS_EN		= BIT(0), /* Enable FBS */

	/* PORT_DEVSLP bits */
	PORT_DEVSLP_DM_OFFSET	= 25,             /* DITO multiplier offset */
	PORT_DEVSLP_DM_MASK	= (0xf << 25),    /* DITO multiplier mask */
	PORT_DEVSLP_DITO_OFFSET	= 15,             /* DITO offset */
	PORT_DEVSLP_MDAT_OFFSET	= 10,             /* Minimum assertion time */
	PORT_DEVSLP_DETO_OFFSET	= 2,              /* DevSlp exit timeout */
	PORT_DEVSLP_DSP		= BIT(1),         /* DevSlp present */
	PORT_DEVSLP_ADSE	= BIT(0),         /* Aggressive DevSlp enable */

	/* hpriv->flags bits */

#define AHCI_HFLAGS(flags)		.private_data	= (void *)(flags)

	AHCI_HFLAG_NO_NCQ		= BIT(0),
	AHCI_HFLAG_IGN_IRQ_IF_ERR	= BIT(1), /* ignore IRQ_IF_ERR */
	AHCI_HFLAG_IGN_SERR_INTERNAL	= BIT(2), /* ignore SERR_INTERNAL */
	AHCI_HFLAG_32BIT_ONLY		= BIT(3), /* force 32bit */
	AHCI_HFLAG_MV_PATA		= BIT(4), /* PATA port */
	AHCI_HFLAG_NO_MSI		= BIT(5), /* no PCI MSI */
	AHCI_HFLAG_NO_PMP		= BIT(6), /* no PMP */
	AHCI_HFLAG_SECT255		= BIT(8), /* max 255 sectors */
	AHCI_HFLAG_YES_NCQ		= BIT(9), /* force NCQ cap on */
	AHCI_HFLAG_NO_SUSPEND		= BIT(10), /* don't suspend */
	AHCI_HFLAG_SRST_TOUT_IS_OFFLINE	= BIT(11), /* treat SRST timeout as
						      link offline */
	AHCI_HFLAG_NO_SNTF		= BIT(12), /* no sntf */
	AHCI_HFLAG_NO_FPDMA_AA		= BIT(13), /* no FPDMA AA */
	AHCI_HFLAG_YES_FBS		= BIT(14), /* force FBS cap on */
	AHCI_HFLAG_DELAY_ENGINE		= BIT(15), /* do not start engine on
						      port start (wait until
						      error-handling stage) */
	AHCI_HFLAG_NO_DEVSLP		= BIT(17), /* no device sleep */
	AHCI_HFLAG_NO_FBS		= BIT(18), /* no FBS */

#ifdef CONFIG_PCI_MSI
	AHCI_HFLAG_MULTI_MSI		= BIT(20), /* per-port MSI(-X) */
#else
	/* compile out MSI infrastructure */
	AHCI_HFLAG_MULTI_MSI		= 0,
#endif
	AHCI_HFLAG_WAKE_BEFORE_STOP	= BIT(22), /* wake before DMA stop */
	AHCI_HFLAG_YES_ALPM		= BIT(23), /* force ALPM cap on */
	AHCI_HFLAG_NO_WRITE_TO_RO	= BIT(24), /* don't write to read
						      only registers */
	AHCI_HFLAG_SUSPEND_PHYS		= BIT(25), /* handle PHYs during
						      suspend/resume */
	AHCI_HFLAG_NO_SXS		= BIT(26), /* SXS not supported */
	AHCI_HFLAG_43BIT_ONLY		= BIT(27), /* 43bit DMA addr limit */
	AHCI_HFLAG_INTEL_PCS_QUIRK	= BIT(28), /* apply Intel PCS quirk */
	AHCI_HFLAG_ATAPI_DMA_QUIRK	= BIT(29), /* force ATAPI to use DMA */

	/* ap->flags bits */

	AHCI_FLAG_COMMON		= ATA_FLAG_SATA | ATA_FLAG_PIO_DMA |
					  ATA_FLAG_ACPI_SATA | ATA_FLAG_AN,

	ICH_MAP				= 0x90, /* ICH MAP register */
	PCS_6				= 0x92, /* 6 port PCS */
	PCS_7				= 0x94, /* 7+ port PCS (Denverton) */

	/* em constants */
	EM_MAX_SLOTS			= SATA_PMP_MAX_PORTS,
	EM_MAX_RETRY			= 5,

	/* em_ctl bits */
	EM_CTL_RST		= BIT(9), /* Reset */
	EM_CTL_TM		= BIT(8), /* Transmit Message */
	EM_CTL_MR		= BIT(0), /* Message Received */
	EM_CTL_ALHD		= BIT(26), /* Activity LED */
	EM_CTL_XMT		= BIT(25), /* Transmit Only */
	EM_CTL_SMB		= BIT(24), /* Single Message Buffer */
	EM_CTL_SGPIO		= BIT(19), /* SGPIO messages supported */
	EM_CTL_SES		= BIT(18), /* SES-2 messages supported */
	EM_CTL_SAFTE		= BIT(17), /* SAF-TE messages supported */
	EM_CTL_LED		= BIT(16), /* LED messages supported */

	/* em message type */
	EM_MSG_TYPE_LED		= BIT(0), /* LED */
	EM_MSG_TYPE_SAFTE	= BIT(1), /* SAF-TE */
	EM_MSG_TYPE_SES2	= BIT(2), /* SES-2 */
	EM_MSG_TYPE_SGPIO	= BIT(3), /* SGPIO */
};

struct ahci_cmd_hdr {
	__le32			opts;
	__le32			status;
	__le32			tbl_addr;
	__le32			tbl_addr_hi;
	__le32			reserved[4];
};

struct ahci_sg {
	__le32			addr;
	__le32			addr_hi;
	__le32			reserved;
	__le32			flags_size;
};

struct ahci_em_priv {
	enum sw_activity blink_policy;
	struct timer_list timer;
	unsigned long saved_activity;
	unsigned long activity;
	unsigned long led_state;
	struct ata_link *link;
};

struct ahci_port_priv {
	struct ata_link		*active_link;
	struct ahci_cmd_hdr	*cmd_slot;
	dma_addr_t		cmd_slot_dma;
	void			*cmd_tbl;
	dma_addr_t		cmd_tbl_dma;
	void			*rx_fis;
	dma_addr_t		rx_fis_dma;
	/* for NCQ spurious interrupt analysis */
	unsigned int		ncq_saw_d2h:1;
	unsigned int		ncq_saw_dmas:1;
	unsigned int		ncq_saw_sdb:1;
	spinlock_t		lock;		/* protects parent ata_port */
	u32 			intr_mask;	/* interrupts to enable */
	bool			fbs_supported;	/* set iff FBS is supported */
	bool			fbs_enabled;	/* set iff FBS is enabled */
	int			fbs_last_dev;	/* save FBS.DEV of last FIS */
	/* enclosure management info per PM slot */
	struct ahci_em_priv	em_priv[EM_MAX_SLOTS];
	char			*irq_desc;	/* desc in /proc/interrupts */
};

struct ahci_host_priv {
	/* Input fields */
	unsigned int		flags;		/* AHCI_HFLAG_* */
	u32			mask_port_map;	/* Mask of valid ports */

	void __iomem *		mmio;		/* bus-independent mem map */
	u32			cap;		/* cap to use */
	u32			cap2;		/* cap2 to use */
	u32			version;	/* cached version */
	u32			port_map;	/* port map to use */
	u32			saved_cap;	/* saved initial cap */
	u32			saved_cap2;	/* saved initial cap2 */
	u32			saved_port_map;	/* saved initial port_map */
	u32			saved_port_cap[AHCI_MAX_PORTS]; /* saved port_cap */
	u32 			em_loc; /* enclosure management location */
	u32			em_buf_sz;	/* EM buffer size in byte */
	u32			em_msg_type;	/* EM message type */
	u32			remapped_nvme;	/* NVMe remapped device count */
	bool			got_runtime_pm; /* Did we do pm_runtime_get? */
	unsigned int		n_clks;
	struct clk_bulk_data	*clks;		/* Optional */
	unsigned int		f_rsts;
	struct reset_control	*rsts;		/* Optional */
	struct regulator	**target_pwrs;	/* Optional */
	struct regulator	*ahci_regulator;/* Optional */
	struct regulator	*phy_regulator;/* Optional */
	/*
	 * If platform uses PHYs. There is a 1:1 relation between the port number and
	 * the PHY position in this array.
	 */
	struct phy		**phys;
	unsigned		nports;		/* Number of ports */
	void			*plat_data;	/* Other platform data */
	unsigned int		irq;		/* interrupt line */
	/*
	 * Optional ahci_start_engine override, if not set this gets set to the
	 * default ahci_start_engine during ahci_save_initial_config, this can
	 * be overridden anytime before the host is activated.
	 */
	void			(*start_engine)(struct ata_port *ap);
	/*
	 * Optional ahci_stop_engine override, if not set this gets set to the
	 * default ahci_stop_engine during ahci_save_initial_config, this can
	 * be overridden anytime before the host is activated.
	 */
	int			(*stop_engine)(struct ata_port *ap);

	irqreturn_t 		(*irq_handler)(int irq, void *dev_instance);

	/* only required for per-port MSI(-X) support */
	int			(*get_irq_vector)(struct ata_host *host,
						  int port);
};

/*
 * Return true if a port should be ignored because it is excluded from
 * the host port map.
 */
static inline bool ahci_ignore_port(struct ahci_host_priv *hpriv,
				    unsigned int portid)
{
	if (portid >= hpriv->nports)
		return true;
	/* mask_port_map not set means that all ports are available */
	if (!hpriv->mask_port_map)
		return false;
	return !(hpriv->mask_port_map & (1 << portid));
}

extern int ahci_ignore_sss;

extern const struct attribute_group *ahci_shost_groups[];
extern const struct attribute_group *ahci_sdev_groups[];

/*
 * This must be instantiated by the edge drivers.  Read the comments
 * for ATA_BASE_SHT
 */
#define AHCI_SHT(drv_name)						\
	__ATA_BASE_SHT(drv_name),					\
	.can_queue		= AHCI_MAX_CMDS,			\
	.sg_tablesize		= AHCI_MAX_SG,				\
	.dma_boundary		= AHCI_DMA_BOUNDARY,			\
	.shost_groups		= ahci_shost_groups,			\
	.sdev_groups		= ahci_sdev_groups,			\
	.change_queue_depth     = ata_scsi_change_queue_depth,		\
	.tag_alloc_policy_rr	= true,					\
	.sdev_configure		= ata_scsi_sdev_configure

extern struct ata_port_operations ahci_ops;
extern struct ata_port_operations ahci_platform_ops;
extern struct ata_port_operations ahci_pmp_retry_srst_ops;

unsigned int ahci_dev_classify(struct ata_port *ap);
void ahci_fill_cmd_slot(struct ahci_port_priv *pp, unsigned int tag,
			u32 opts);
void ahci_save_initial_config(struct device *dev,
			      struct ahci_host_priv *hpriv);
void ahci_init_controller(struct ata_host *host);
int ahci_reset_controller(struct ata_host *host);

int ahci_do_softreset(struct ata_link *link, unsigned int *class,
		      int pmp, unsigned long deadline,
		      int (*check_ready)(struct ata_link *link));

int ahci_do_hardreset(struct ata_link *link, unsigned int *class,
		      unsigned long deadline, bool *online);

unsigned int ahci_qc_issue(struct ata_queued_cmd *qc);
int ahci_stop_engine(struct ata_port *ap);
void ahci_start_fis_rx(struct ata_port *ap);
void ahci_start_engine(struct ata_port *ap);
int ahci_check_ready(struct ata_link *link);
int ahci_kick_engine(struct ata_port *ap);
int ahci_port_resume(struct ata_port *ap);
void ahci_set_em_messages(struct ahci_host_priv *hpriv,
			  struct ata_port_info *pi);
int ahci_reset_em(struct ata_host *host);
void ahci_print_info(struct ata_host *host, const char *scc_s);
int ahci_host_activate(struct ata_host *host, const struct scsi_host_template *sht);
void ahci_error_handler(struct ata_port *ap);
u32 ahci_handle_port_intr(struct ata_host *host, u32 irq_masked);

static inline void __iomem *__ahci_port_base(struct ahci_host_priv *hpriv,
					     unsigned int port_no)
{
	void __iomem *mmio = hpriv->mmio;

	return mmio + 0x100 + (port_no * 0x80);
}

static inline void __iomem *ahci_port_base(struct ata_port *ap)
{
	struct ahci_host_priv *hpriv = ap->host->private_data;

	return __ahci_port_base(hpriv, ap->port_no);
}

static inline int ahci_nr_ports(u32 cap)
{
	return (cap & 0x1f) + 1;
}

#endif /* _AHCI_H */
