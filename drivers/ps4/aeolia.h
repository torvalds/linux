#ifndef _AEOLIA_H
#define _AEOLIA_H

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/i2c.h>

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

#define APCIE_RGN_ICC_BASE		0x184000
#define APCIE_RGN_ICC_SIZE		0x1000

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

#define APCIE_REG_ICC(x)		(APCIE_RGN_ICC_BASE + (x))
#define APCIE_REG_ICC_DOORBELL		APCIE_REG_ICC(0x804)
#define APCIE_REG_ICC_STATUS		APCIE_REG_ICC(0x814)
#define APCIE_REG_ICC_IRQ_MASK		APCIE_REG_ICC(0x824)

/* Apply to both DOORBELL and STATUS */
#define APCIE_ICC_SEND			0x01
#define APCIE_ICC_ACK			0x02

/* Relative to func6 BAR5 */
#define APCIE_SPM_ICC_BASE		0x2c000
#define APCIE_SPM_ICC_SIZE		0x1000

#define APCIE_SPM_ICC_REQUEST		0x0
#define APCIE_SPM_ICC_REPLY		0x800

#define ICC_REPLY 0x4000
#define ICC_EVENT 0x8000

#define ICC_MAGIC 0x42
#define ICC_EVENT_MAGIC 0x24

struct icc_message_hdr {
	u8 magic;
	u8 major;
	u16 minor;
	u16 unknown;
	u16 cookie;
	u16 length;
	u16 checksum;
} __packed;

#define ICC_HDR_SIZE sizeof(struct icc_message_hdr)
#define ICC_MIN_SIZE 0x20
#define ICC_MAX_SIZE 0x7f0
#define ICC_MIN_PAYLOAD (ICC_MIN_SIZE - ICC_HDR_SIZE)
#define ICC_MAX_PAYLOAD (ICC_MAX_SIZE - ICC_HDR_SIZE)

struct apcie_icc_dev {
	phys_addr_t spm_base;
	void __iomem *spm;

	spinlock_t reply_lock;
	bool reply_pending;

	struct icc_message_hdr request;
	struct icc_message_hdr reply;
	u16 reply_extra_checksum;
	void *reply_buffer;
	int reply_length;
	wait_queue_head_t wq;

	struct i2c_adapter i2c;
	struct input_dev *pwrbutton_dev;
};

struct apcie_dev {
	struct pci_dev *pdev;
	struct irq_domain *irqdomain;
	void __iomem *bar0;
	void __iomem *bar2;
	void __iomem *bar4;

	int nvec;
	int serial_line[2];
	struct apcie_icc_dev icc;
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

int apcie_icc_cmd(u8 major, u16 minor, const void *data, u16 length,
	    void *reply, u16 reply_length);

#endif
