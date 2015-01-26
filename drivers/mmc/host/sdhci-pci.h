#ifndef __SDHCI_PCI_H
#define __SDHCI_PCI_H

/*
 * PCI device IDs
 */

#define PCI_DEVICE_ID_INTEL_PCH_SDIO0	0x8809
#define PCI_DEVICE_ID_INTEL_PCH_SDIO1	0x880a
#define PCI_DEVICE_ID_INTEL_BYT_EMMC	0x0f14
#define PCI_DEVICE_ID_INTEL_BYT_SDIO	0x0f15
#define PCI_DEVICE_ID_INTEL_BYT_SD	0x0f16
#define PCI_DEVICE_ID_INTEL_BYT_EMMC2	0x0f50
#define PCI_DEVICE_ID_INTEL_BSW_EMMC	0x2294
#define PCI_DEVICE_ID_INTEL_BSW_SDIO	0x2295
#define PCI_DEVICE_ID_INTEL_BSW_SD	0x2296
#define PCI_DEVICE_ID_INTEL_MRFL_MMC	0x1190
#define PCI_DEVICE_ID_INTEL_CLV_SDIO0	0x08f9
#define PCI_DEVICE_ID_INTEL_CLV_SDIO1	0x08fa
#define PCI_DEVICE_ID_INTEL_CLV_SDIO2	0x08fb
#define PCI_DEVICE_ID_INTEL_CLV_EMMC0	0x08e5
#define PCI_DEVICE_ID_INTEL_CLV_EMMC1	0x08e6
#define PCI_DEVICE_ID_INTEL_QRK_SD	0x08A7
#define PCI_DEVICE_ID_INTEL_SPT_EMMC	0x9d2b
#define PCI_DEVICE_ID_INTEL_SPT_SDIO	0x9d2c
#define PCI_DEVICE_ID_INTEL_SPT_SD	0x9d2d

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
	void			(*remove_slot) (struct sdhci_pci_slot *, int);

	int			(*suspend) (struct sdhci_pci_chip *);
	int			(*resume) (struct sdhci_pci_chip *);
};

struct sdhci_pci_slot {
	struct sdhci_pci_chip	*chip;
	struct sdhci_host	*host;
	struct sdhci_pci_data	*data;

	int			pci_bar;
	int			rst_n_gpio;
	int			cd_gpio;
	int			cd_irq;

	char			*cd_con_id;
	int			cd_idx;
	bool			cd_override_level;

	void (*hw_reset)(struct sdhci_host *host);
};

struct sdhci_pci_chip {
	struct pci_dev		*pdev;

	unsigned int		quirks;
	unsigned int		quirks2;
	bool			allow_runtime_pm;
	const struct sdhci_pci_fixes *fixes;

	int			num_slots;	/* Slots on controller */
	struct sdhci_pci_slot	*slots[MAX_SLOTS]; /* Pointers to host slots */
};

#endif /* __SDHCI_PCI_H */
