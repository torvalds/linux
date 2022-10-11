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
	AHCI_MAX_CLKS		= 5,
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
	AHCI_IRQ_ON_SG		= (1 << 31),
	AHCI_CMD_ATAPI		= (1 << 5),
	AHCI_CMD_WRITE		= (1 << 6),
	AHCI_CMD_PREFETCH	= (1 << 7),
	AHCI_CMD_RESET		= (1 << 8),
	AHCI_CMD_CLR_BUSY	= (1 << 10),

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
	HOST_RESET		= (1 << 0),  /* reset controller; self-clear */
	HOST_IRQ_EN		= (1 << 1),  /* global IRQ enable */
	HOST_MRSM		= (1 << 2),  /* MSI Revert to Single Message */
	HOST_AHCI_EN		= (1 << 31), /* AHCI enabled */

	/* HOST_CAP bits */
	HOST_CAP_SXS		= (1 << 5),  /* Supports External SATA */
	HOST_CAP_EMS		= (1 << 6),  /* Enclosure Management support */
	HOST_CAP_CCC		= (1 << 7),  /* Command Completion Coalescing */
	HOST_CAP_PART		= (1 << 13), /* Partial state capable */
	HOST_CAP_SSC		= (1 << 14), /* Slumber state capable */
	HOST_CAP_PIO_MULTI	= (1 << 15), /* PIO multiple DRQ support */
	HOST_CAP_FBS		= (1 << 16), /* FIS-based switching support */
	HOST_CAP_PMP		= (1 << 17), /* Port Multiplier support */
	HOST_CAP_ONLY		= (1 << 18), /* Supports AHCI mode only */
	HOST_CAP_CLO		= (1 << 24), /* Command List Override support */
	HOST_CAP_LED		= (1 << 25), /* Supports activity LED */
	HOST_CAP_ALPM		= (1 << 26), /* Aggressive Link PM support */
	HOST_CAP_SSS		= (1 << 27), /* Staggered Spin-up */
	HOST_CAP_MPS		= (1 << 28), /* Mechanical presence switch */
	HOST_CAP_SNTF		= (1 << 29), /* SNotification register */
	HOST_CAP_NCQ		= (1 << 30), /* Native Command Queueing */
	HOST_CAP_64		= (1 << 31), /* PCI DAC (64-bit DMA) support */

	/* HOST_CAP2 bits */
	HOST_CAP2_BOH		= (1 << 0),  /* BIOS/OS handoff supported */
	HOST_CAP2_NVMHCI	= (1 << 1),  /* NVMHCI supported */
	HOST_CAP2_APST		= (1 << 2),  /* Automatic partial to slumber */
	HOST_CAP2_SDS		= (1 << 3),  /* Support device sleep */
	HOST_CAP2_SADM		= (1 << 4),  /* Support aggressive DevSlp */
	HOST_CAP2_DESO		= (1 << 5),  /* DevSlp from slumber only */

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
	PORT_IRQ_COLD_PRES	= (1 << 31), /* cold presence detect */
	PORT_IRQ_TF_ERR		= (1 << 30), /* task file error */
	PORT_IRQ_HBUS_ERR	= (1 << 29), /* host bus fatal error */
	PORT_IRQ_HBUS_DATA_ERR	= (1 << 28), /* host bus data error */
	PORT_IRQ_IF_ERR		= (1 << 27), /* interface fatal error */
	PORT_IRQ_IF_NONFATAL	= (1 << 26), /* interface non-fatal error */
	PORT_IRQ_OVERFLOW	= (1 << 24), /* xfer exhausted available S/G */
	PORT_IRQ_BAD_PMP	= (1 << 23), /* incorrect port multiplier */

	PORT_IRQ_PHYRDY		= (1 << 22), /* PhyRdy changed */
	PORT_IRQ_DEV_ILCK	= (1 << 7), /* device interlock */
	PORT_IRQ_CONNECT	= (1 << 6), /* port connect change status */
	PORT_IRQ_SG_DONE	= (1 << 5), /* descriptor processed */
	PORT_IRQ_UNK_FIS	= (1 << 4), /* unknown FIS rx'd */
	PORT_IRQ_SDB_FIS	= (1 << 3), /* Set Device Bits FIS rx'd */
	PORT_IRQ_DMAS_FIS	= (1 << 2), /* DMA Setup FIS rx'd */
	PORT_IRQ_PIOS_FIS	= (1 << 1), /* PIO Setup FIS rx'd */
	PORT_IRQ_D2H_REG_FIS	= (1 << 0), /* D2H Register FIS rx'd */

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
	PORT_CMD_ASP		= (1 << 27), /* Aggressive Slumber/Partial */
	PORT_CMD_ALPE		= (1 << 26), /* Aggressive Link PM enable */
	PORT_CMD_ATAPI		= (1 << 24), /* Device is ATAPI */
	PORT_CMD_FBSCP		= (1 << 22), /* FBS Capable Port */
	PORT_CMD_ESP		= (1 << 21), /* External Sata Port */
	PORT_CMD_HPCP		= (1 << 18), /* HotPlug Capable Port */
	PORT_CMD_PMP		= (1 << 17), /* PMP attached */
	PORT_CMD_LIST_ON	= (1 << 15), /* cmd list DMA engine running */
	PORT_CMD_FIS_ON		= (1 << 14), /* FIS DMA engine running */
	PORT_CMD_FIS_RX		= (1 << 4), /* Enable FIS receive DMA engine */
	PORT_CMD_CLO		= (1 << 3), /* Command list override */
	PORT_CMD_POWER_ON	= (1 << 2), /* Power up device */
	PORT_CMD_SPIN_UP	= (1 << 1), /* Spin up device */
	PORT_CMD_START		= (1 << 0), /* Enable port DMA engine */

	PORT_CMD_ICC_MASK	= (0xf << 28), /* i/f ICC state mask */
	PORT_CMD_ICC_ACTIVE	= (0x1 << 28), /* Put i/f in active state */
	PORT_CMD_ICC_PARTIAL	= (0x2 << 28), /* Put i/f in partial state */
	PORT_CMD_ICC_SLUMBER	= (0x6 << 28), /* Put i/f in slumber state */

	/* PORT_FBS bits */
	PORT_FBS_DWE_OFFSET	= 16, /* FBS device with error offset */
	PORT_FBS_ADO_OFFSET	= 12, /* FBS active dev optimization offset */
	PORT_FBS_DEV_OFFSET	= 8,  /* FBS device to issue offset */
	PORT_FBS_DEV_MASK	= (0xf << PORT_FBS_DEV_OFFSET),  /* FBS.DEV */
	PORT_FBS_SDE		= (1 << 2), /* FBS single device error */
	PORT_FBS_DEC		= (1 << 1), /* FBS device error clear */
	PORT_FBS_EN		= (1 << 0), /* Enable FBS */

	/* PORT_DEVSLP bits */
	PORT_DEVSLP_DM_OFFSET	= 25,             /* DITO multiplier offset */
	PORT_DEVSLP_DM_MASK	= (0xf << 25),    /* DITO multiplier mask */
	PORT_DEVSLP_DITO_OFFSET	= 15,             /* DITO offset */
	PORT_DEVSLP_MDAT_OFFSET	= 10,             /* Minimum assertion time */
	PORT_DEVSLP_DETO_OFFSET	= 2,              /* DevSlp exit timeout */
	PORT_DEVSLP_DSP		= (1 << 1),       /* DevSlp present */
	PORT_DEVSLP_ADSE	= (1 << 0),       /* Aggressive DevSlp enable */

	/* hpriv->flags bits */

#define AHCI_HFLAGS(flags)		.private_data	= (void *)(flags)

	AHCI_HFLAG_NO_NCQ		= (1 << 0),
	AHCI_HFLAG_IGN_IRQ_IF_ERR	= (1 << 1), /* ignore IRQ_IF_ERR */
	AHCI_HFLAG_IGN_SERR_INTERNAL	= (1 << 2), /* ignore SERR_INTERNAL */
	AHCI_HFLAG_32BIT_ONLY		= (1 << 3), /* force 32bit */
	AHCI_HFLAG_MV_PATA		= (1 << 4), /* PATA port */
	AHCI_HFLAG_NO_MSI		= (1 << 5), /* no PCI MSI */
	AHCI_HFLAG_NO_PMP		= (1 << 6), /* no PMP */
	AHCI_HFLAG_SECT255		= (1 << 8), /* max 255 sectors */
	AHCI_HFLAG_YES_NCQ		= (1 << 9), /* force NCQ cap on */
	AHCI_HFLAG_NO_SUSPEND		= (1 << 10), /* don't suspend */
	AHCI_HFLAG_SRST_TOUT_IS_OFFLINE	= (1 << 11), /* treat SRST timeout as
							link offline */
	AHCI_HFLAG_NO_SNTF		= (1 << 12), /* no sntf */
	AHCI_HFLAG_NO_FPDMA_AA		= (1 << 13), /* no FPDMA AA */
	AHCI_HFLAG_YES_FBS		= (1 << 14), /* force FBS cap on */
	AHCI_HFLAG_DELAY_ENGINE		= (1 << 15), /* do not start engine on
						        port start (wait until
						        error-handling stage) */
	AHCI_HFLAG_NO_DEVSLP		= (1 << 17), /* no device sleep */
	AHCI_HFLAG_NO_FBS		= (1 << 18), /* no FBS */

#ifdef CONFIG_PCI_MSI
	AHCI_HFLAG_MULTI_MSI		= (1 << 20), /* per-port MSI(-X) */
#else
	/* compile out MSI infrastructure */
	AHCI_HFLAG_MULTI_MSI		= 0,
#endif
	AHCI_HFLAG_WAKE_BEFORE_STOP	= (1 << 22), /* wake before DMA stop */
	AHCI_HFLAG_YES_ALPM		= (1 << 23), /* force ALPM cap on */
	AHCI_HFLAG_NO_WRITE_TO_RO	= (1 << 24), /* don't write to read
							only registers */
	AHCI_HFLAG_IS_MOBILE		= (1 << 25), /* mobile chipset, use
							SATA_MOBILE_LPM_POLICY
							as default lpm_policy */
	AHCI_HFLAG_SUSPEND_PHYS		= (1 << 26), /* handle PHYs during
							suspend/resume */
	AHCI_HFLAG_IGN_NOTSUPP_POWER_ON	= (1 << 27), /* ignore -EOPNOTSUPP
							from phy_power_on() */
	AHCI_HFLAG_NO_SXS		= (1 << 28), /* SXS not supported */

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
	EM_CTL_RST		= (1 << 9), /* Reset */
	EM_CTL_TM		= (1 << 8), /* Transmit Message */
	EM_CTL_MR		= (1 << 0), /* Message Received */
	EM_CTL_ALHD		= (1 << 26), /* Activity LED */
	EM_CTL_XMT		= (1 << 25), /* Transmit Only */
	EM_CTL_SMB		= (1 << 24), /* Single Message Buffer */
	EM_CTL_SGPIO		= (1 << 19), /* SGPIO messages supported */
	EM_CTL_SES		= (1 << 18), /* SES-2 messages supported */
	EM_CTL_SAFTE		= (1 << 17), /* SAF-TE messages supported */
	EM_CTL_LED		= (1 << 16), /* LED messages supported */

	/* em message type */
	EM_MSG_TYPE_LED		= (1 << 0), /* LED */
	EM_MSG_TYPE_SAFTE	= (1 << 1), /* SAF-TE */
	EM_MSG_TYPE_SES2	= (1 << 2), /* SES-2 */
	EM_MSG_TYPE_SGPIO	= (1 << 3), /* SGPIO */
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
	u32			force_port_map;	/* force port map */
	u32			mask_port_map;	/* mask out particular bits */

	void __iomem *		mmio;		/* bus-independent mem map */
	u32			cap;		/* cap to use */
	u32			cap2;		/* cap2 to use */
	u32			version;	/* cached version */
	u32			port_map;	/* port map to use */
	u32			saved_cap;	/* saved initial cap */
	u32			saved_cap2;	/* saved initial cap2 */
	u32			saved_port_map;	/* saved initial port_map */
	u32 			em_loc; /* enclosure management location */
	u32			em_buf_sz;	/* EM buffer size in byte */
	u32			em_msg_type;	/* EM message type */
	u32			remapped_nvme;	/* NVMe remapped device count */
	bool			got_runtime_pm; /* Did we do pm_runtime_get? */
	struct clk		*clks[AHCI_MAX_CLKS]; /* Optional */
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

extern int ahci_ignore_sss;

extern struct device_attribute *ahci_shost_attrs[];
extern struct device_attribute *ahci_sdev_attrs[];

/*
 * This must be instantiated by the edge drivers.  Read the comments
 * for ATA_BASE_SHT
 */
#define AHCI_SHT(drv_name)						\
	ATA_NCQ_SHT(drv_name),						\
	.can_queue		= AHCI_MAX_CMDS,			\
	.sg_tablesize		= AHCI_MAX_SG,				\
	.dma_boundary		= AHCI_DMA_BOUNDARY,			\
	.shost_attrs		= ahci_shost_attrs,			\
	.sdev_attrs		= ahci_sdev_attrs

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
int ahci_host_activate(struct ata_host *host, struct scsi_host_template *sht);
void ahci_error_handler(struct ata_port *ap);
u32 ahci_handle_port_intr(struct ata_host *host, u32 irq_masked);

static inline void __iomem *__ahci_port_base(struct ata_host *host,
					     unsigned int port_no)
{
	struct ahci_host_priv *hpriv = host->private_data;
	void __iomem *mmio = hpriv->mmio;

	return mmio + 0x100 + (port_no * 0x80);
}

static inline void __iomem *ahci_port_base(struct ata_port *ap)
{
	return __ahci_port_base(ap->host, ap->port_no);
}

static inline int ahci_nr_ports(u32 cap)
{
	return (cap & 0x1f) + 1;
}

#endif /* _AHCI_H */
