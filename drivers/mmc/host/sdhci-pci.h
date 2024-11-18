/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SDHCI_PCI_H
#define __SDHCI_PCI_H

/*
 * PCI device IDs, sub IDs
 */

#define PCI_DEVICE_ID_O2_SDS0		0x8420
#define PCI_DEVICE_ID_O2_SDS1		0x8421
#define PCI_DEVICE_ID_O2_FUJIN2		0x8520
#define PCI_DEVICE_ID_O2_SEABIRD0	0x8620
#define PCI_DEVICE_ID_O2_SEABIRD1	0x8621
#define PCI_DEVICE_ID_O2_GG8_9860	0x9860
#define PCI_DEVICE_ID_O2_GG8_9861	0x9861
#define PCI_DEVICE_ID_O2_GG8_9862	0x9862
#define PCI_DEVICE_ID_O2_GG8_9863	0x9863

#define PCI_DEVICE_ID_INTEL_PCH_SDIO0	0x8809
#define PCI_DEVICE_ID_INTEL_PCH_SDIO1	0x880a
#define PCI_DEVICE_ID_INTEL_BYT_EMMC	0x0f14
#define PCI_DEVICE_ID_INTEL_BYT_SDIO	0x0f15
#define PCI_DEVICE_ID_INTEL_BYT_SD	0x0f16
#define PCI_DEVICE_ID_INTEL_BYT_EMMC2	0x0f50
#define PCI_DEVICE_ID_INTEL_BSW_EMMC	0x2294
#define PCI_DEVICE_ID_INTEL_BSW_SDIO	0x2295
#define PCI_DEVICE_ID_INTEL_BSW_SD	0x2296
#define PCI_DEVICE_ID_INTEL_MRFLD_MMC	0x1190
#define PCI_DEVICE_ID_INTEL_CLV_SDIO0	0x08f9
#define PCI_DEVICE_ID_INTEL_CLV_SDIO1	0x08fa
#define PCI_DEVICE_ID_INTEL_CLV_SDIO2	0x08fb
#define PCI_DEVICE_ID_INTEL_CLV_EMMC0	0x08e5
#define PCI_DEVICE_ID_INTEL_CLV_EMMC1	0x08e6
#define PCI_DEVICE_ID_INTEL_QRK_SD	0x08A7
#define PCI_DEVICE_ID_INTEL_SPT_EMMC	0x9d2b
#define PCI_DEVICE_ID_INTEL_SPT_SDIO	0x9d2c
#define PCI_DEVICE_ID_INTEL_SPT_SD	0x9d2d
#define PCI_DEVICE_ID_INTEL_DNV_EMMC	0x19db
#define PCI_DEVICE_ID_INTEL_CDF_EMMC	0x18db
#define PCI_DEVICE_ID_INTEL_BXT_SD	0x0aca
#define PCI_DEVICE_ID_INTEL_BXT_EMMC	0x0acc
#define PCI_DEVICE_ID_INTEL_BXT_SDIO	0x0ad0
#define PCI_DEVICE_ID_INTEL_BXTM_SD	0x1aca
#define PCI_DEVICE_ID_INTEL_BXTM_EMMC	0x1acc
#define PCI_DEVICE_ID_INTEL_BXTM_SDIO	0x1ad0
#define PCI_DEVICE_ID_INTEL_APL_SD	0x5aca
#define PCI_DEVICE_ID_INTEL_APL_EMMC	0x5acc
#define PCI_DEVICE_ID_INTEL_APL_SDIO	0x5ad0
#define PCI_DEVICE_ID_INTEL_GLK_SD	0x31ca
#define PCI_DEVICE_ID_INTEL_GLK_EMMC	0x31cc
#define PCI_DEVICE_ID_INTEL_GLK_SDIO	0x31d0
#define PCI_DEVICE_ID_INTEL_CNP_EMMC	0x9dc4
#define PCI_DEVICE_ID_INTEL_CNP_SD	0x9df5
#define PCI_DEVICE_ID_INTEL_CNPH_SD	0xa375
#define PCI_DEVICE_ID_INTEL_ICP_EMMC	0x34c4
#define PCI_DEVICE_ID_INTEL_ICP_SD	0x34f8
#define PCI_DEVICE_ID_INTEL_EHL_EMMC	0x4b47
#define PCI_DEVICE_ID_INTEL_EHL_SD	0x4b48
#define PCI_DEVICE_ID_INTEL_CML_EMMC	0x02c4
#define PCI_DEVICE_ID_INTEL_CML_SD	0x02f5
#define PCI_DEVICE_ID_INTEL_CMLH_SD	0x06f5
#define PCI_DEVICE_ID_INTEL_JSL_EMMC	0x4dc4
#define PCI_DEVICE_ID_INTEL_JSL_SD	0x4df8
#define PCI_DEVICE_ID_INTEL_LKF_EMMC	0x98c4
#define PCI_DEVICE_ID_INTEL_LKF_SD	0x98f8
#define PCI_DEVICE_ID_INTEL_ADL_EMMC	0x54c4

#define PCI_DEVICE_ID_SYSKONNECT_8000	0x8000
#define PCI_DEVICE_ID_VIA_95D0		0x95d0
#define PCI_DEVICE_ID_REALTEK_5250	0x5250

#define PCI_SUBDEVICE_ID_NI_7884	0x7884
#define PCI_SUBDEVICE_ID_NI_78E3	0x78e3

#define PCI_VENDOR_ID_ARASAN		0x16e6
#define PCI_DEVICE_ID_ARASAN_PHY_EMMC	0x0670

#define PCI_DEVICE_ID_SYNOPSYS_DWC_MSHC 0xc202

#define PCI_DEVICE_ID_GLI_9755		0x9755
#define PCI_DEVICE_ID_GLI_9750		0x9750
#define PCI_DEVICE_ID_GLI_9763E		0xe763
#define PCI_DEVICE_ID_GLI_9767		0x9767

/*
 * PCI device class and mask
 */

#define SYSTEM_SDHCI			(PCI_CLASS_SYSTEM_SDHCI << 8)
#define PCI_CLASS_MASK			0xFFFF00

/*
 * Macros for PCI device-description
 */

#define _PCI_VEND(vend) PCI_VENDOR_ID_##vend
#define _PCI_DEV(vend, dev) PCI_DEVICE_ID_##vend##_##dev
#define _PCI_SUBDEV(subvend, subdev) PCI_SUBDEVICE_ID_##subvend##_##subdev

#define SDHCI_PCI_DEVICE(vend, dev, cfg) { \
	.vendor = _PCI_VEND(vend), .device = _PCI_DEV(vend, dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, \
	.driver_data = (kernel_ulong_t)&(sdhci_##cfg) \
}

#define SDHCI_PCI_SUBDEVICE(vend, dev, subvend, subdev, cfg) { \
	.vendor = _PCI_VEND(vend), .device = _PCI_DEV(vend, dev), \
	.subvendor = _PCI_VEND(subvend), \
	.subdevice = _PCI_SUBDEV(subvend, subdev), \
	.driver_data = (kernel_ulong_t)&(sdhci_##cfg) \
}

#define SDHCI_PCI_DEVICE_CLASS(vend, cl, cl_msk, cfg) { \
	.vendor = _PCI_VEND(vend), .device = PCI_ANY_ID, \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, \
	.class = (cl), .class_mask = (cl_msk), \
	.driver_data = (kernel_ulong_t)&(sdhci_##cfg) \
}

/*
 * PCI registers
 */

#define PCI_SDHCI_IFPIO			0x00
#define PCI_SDHCI_IFDMA			0x01
#define PCI_SDHCI_IFVENDOR		0x02

#define PCI_SLOT_INFO			0x40	/* 8 bits */
#define  PCI_SLOT_INFO_SLOTS(x)		((x >> 4) & 7)
#define  PCI_SLOT_INFO_FIRST_BAR_MASK	0x07

#define MAX_SLOTS			8

struct sdhci_pci_chip;
struct sdhci_pci_slot;

struct sdhci_pci_fixes {
	unsigned int		quirks;
	unsigned int		quirks2;
	bool			allow_runtime_pm;
	bool			own_cd_for_runtime_pm;

	int			(*probe) (struct sdhci_pci_chip *);

	int			(*probe_slot) (struct sdhci_pci_slot *);
	int			(*add_host) (struct sdhci_pci_slot *);
	void			(*remove_slot) (struct sdhci_pci_slot *, int);

#ifdef CONFIG_PM_SLEEP
	int			(*suspend) (struct sdhci_pci_chip *);
	int			(*resume) (struct sdhci_pci_chip *);
#endif
#ifdef CONFIG_PM
	int			(*runtime_suspend) (struct sdhci_pci_chip *);
	int			(*runtime_resume) (struct sdhci_pci_chip *);
#endif

	const struct sdhci_ops	*ops;
	const struct dmi_system_id *cd_gpio_override;
	size_t			priv_size;
};

struct sdhci_pci_slot {
	struct sdhci_pci_chip	*chip;
	struct sdhci_host	*host;

	int			cd_idx;
	bool			cd_override_level;

	void (*hw_reset)(struct sdhci_host *host);
	unsigned long		private[] ____cacheline_aligned;
};

struct sdhci_pci_chip {
	struct pci_dev		*pdev;

	unsigned int		quirks;
	unsigned int		quirks2;
	bool			allow_runtime_pm;
	bool			pm_retune;
	bool			rpm_retune;
	const struct sdhci_pci_fixes *fixes;

	int			num_slots;	/* Slots on controller */
	struct sdhci_pci_slot	*slots[MAX_SLOTS]; /* Pointers to host slots */
};

static inline void *sdhci_pci_priv(struct sdhci_pci_slot *slot)
{
	return (void *)slot->private;
}

#ifdef CONFIG_PM_SLEEP
int sdhci_pci_resume_host(struct sdhci_pci_chip *chip);
#endif
int sdhci_pci_enable_dma(struct sdhci_host *host);

extern const struct sdhci_pci_fixes sdhci_arasan;
extern const struct sdhci_pci_fixes sdhci_snps;
extern const struct sdhci_pci_fixes sdhci_o2;
extern const struct sdhci_pci_fixes sdhci_gl9750;
extern const struct sdhci_pci_fixes sdhci_gl9755;
extern const struct sdhci_pci_fixes sdhci_gl9763e;
extern const struct sdhci_pci_fixes sdhci_gl9767;

#endif /* __SDHCI_PCI_H */
