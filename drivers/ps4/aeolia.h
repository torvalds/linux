#ifndef _AEOLIA_H
#define _AEOLIA_H

#include <linux/io.h>
#include <linux/pci.h>

#define PCI_DEVICE_ID_AACPI		0x908f
#define PCI_DEVICE_ID_AGBE		0x909e
#define PCI_DEVICE_ID_AAHCI		0x909f
#define PCI_DEVICE_ID_ASDHCI		0x90a0
#define PCI_DEVICE_ID_APCIE		0x90a1
#define PCI_DEVICE_ID_ADMAC		0x90a2
#define PCI_DEVICE_ID_AMEM		0x90a3
#define PCI_DEVICE_ID_AXHCI		0x90a4

enum aeolia_func_id {
	AEOLIA_FUNC_ID_ACPI = 0,
	AEOLIA_FUNC_ID_GBE,
	AEOLIA_FUNC_ID_AHCI,
	AEOLIA_FUNC_ID_SDHCI,
	AEOLIA_FUNC_ID_PCIE,
	AEOLIA_FUNC_ID_DMAC,
	AEOLIA_FUNC_ID_MEM,
	AEOLIA_FUNC_ID_XHCI,

	AEOLIA_NUM_FUNCS
};

/* MSI registers for up to 31, but only 23 known. */
#define APCIE_NUM_SUBFUNC		23

/* Sub-functions, aka MSI vectors */
enum apcie_subfunc {
	APCIE_SUBFUNC_GLUE	= 0,
	APCIE_SUBFUNC_ICC	= 3,
	APCIE_SUBFUNC_HPET	= 5,
	APCIE_SUBFUNC_SFLASH	= 11,
	APCIE_SUBFUNC_RTC	= 13,
	APCIE_SUBFUNC_UART0	= 19,
	APCIE_SUBFUNC_UART1	= 20,
	APCIE_SUBFUNC_TWSI	= 21,

	APCIE_NUM_SUBFUNCS	= 23
};

#define APCIE_NR_UARTS 2

/* Relative to BAR2 */
#define APCIE_RGN_RTC_BASE		0x0
#define APCIE_RGN_RTC_SIZE		0x1000

#define APCIE_RGN_CHIPID_BASE		0x1000
#define APCIE_RGN_CHIPID_SIZE		0x1000

#define APCIE_REG_CHIPID_0		0x1104
#define APCIE_REG_CHIPID_1		0x1108
#define APCIE_REG_CHIPREV		0x110c

/* Relative to BAR4 */
#define APCIE_RGN_UART_BASE		0x140000
#define APCIE_RGN_UART_SIZE		0x1000

#define APCIE_RGN_PCIE_BASE		0x1c8000
#define APCIE_RGN_PCIE_SIZE		0x1000

#define APCIE_REG_BAR(x)		(APCIE_RGN_PCIE_BASE + (x))
#define APCIE_REG_BAR_MASK(func, bar)	APCIE_REG_BAR(((func) * 0x30) + \
						((bar) << 3))
#define APCIE_REG_BAR_ADDR(func, bar)	APCIE_REG_BAR(((func) * 0x30) + \
						((bar) << 3) + 0x4)

#define APCIE_REG_MSI(x)		(APCIE_RGN_PCIE_BASE + 0x400 + (x))
#define APCIE_REG_MSI_CONTROL		APCIE_REG_MSI(0x0)
#define APCIE_REG_MSI_MASK(func)	APCIE_REG_MSI(0x4c + ((func) << 2))
#define APCIE_REG_MSI_DATA_HI(func)	APCIE_REG_MSI(0x8c + ((func) << 2))
#define APCIE_REG_MSI_ADDR(func)	APCIE_REG_MSI(0xac + ((func) << 2))
/* This register has non-uniform structure per function, dealt with in code */
#define APCIE_REG_MSI_DATA_LO(off)	APCIE_REG_MSI(0x100 + (off))

/* Not sure what the two individual bits do */
#define APCIE_REG_MSI_CONTROL_ENABLE	0x05

/* Enable for the entire function, 4 is special */
#define APCIE_REG_MSI_MASK_FUNC		0x01000000
#define APCIE_REG_MSI_MASK_FUNC4	0x80000000

struct apcie_dev {
	struct pci_dev *pdev;
	struct irq_domain *irqdomain;
	void __iomem *bar0;
	void __iomem *bar2;
	void __iomem *bar4;

	int nvec;
	int serial_line[2];
};

#define sc_err(...) dev_err(&sc->pdev->dev, __VA_ARGS__)
#define sc_warn(...) dev_warn(&sc->pdev->dev, __VA_ARGS__)
#define sc_notice(...) dev_notice(&sc->pdev->dev, __VA_ARGS__)
#define sc_info(...) dev_info(&sc->pdev->dev, __VA_ARGS__)
#define sc_dbg(...) dev_dbg(&sc->pdev->dev, __VA_ARGS__)

static inline int apcie_irqnum(struct apcie_dev *sc, int index)
{
	if (sc->nvec > 1) {
		return sc->pdev->irq + index;
	} else {
		return sc->pdev->irq;
	}
}

#endif