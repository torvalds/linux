/*
 * USB block power/access management abstraction.
 *
 * Au1000+: The OHCI block control register is at the far end of the OHCI memory
 *	    area. Au1550 has OHCI on different base address. No need to handle
 *	    UDC here.
 * Au1200:  one register to control access and clocks to O/EHCI, UDC and OTG
 *	    as well as the PHY for EHCI and UDC.
 *
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/syscore_ops.h>
#include <asm/mach-au1x00/au1000.h>

/* control register offsets */
#define AU1000_OHCICFG	0x7fffc
#define AU1550_OHCICFG	0x07ffc
#define AU1200_USBCFG	0x04

/* Au1000 USB block config bits */
#define USBHEN_RD	(1 << 4)		/* OHCI reset-done indicator */
#define USBHEN_CE	(1 << 3)		/* OHCI block clock enable */
#define USBHEN_E	(1 << 2)		/* OHCI block enable */
#define USBHEN_C	(1 << 1)		/* OHCI block coherency bit */
#define USBHEN_BE	(1 << 0)		/* OHCI Big-Endian */

/* Au1200 USB config bits */
#define USBCFG_PFEN	(1 << 31)		/* prefetch enable (undoc) */
#define USBCFG_RDCOMB	(1 << 30)		/* read combining (undoc) */
#define USBCFG_UNKNOWN	(5 << 20)		/* unknown, leave this way */
#define USBCFG_SSD	(1 << 23)		/* serial short detect en */
#define USBCFG_PPE	(1 << 19)		/* HS PHY PLL */
#define USBCFG_UCE	(1 << 18)		/* UDC clock enable */
#define USBCFG_ECE	(1 << 17)		/* EHCI clock enable */
#define USBCFG_OCE	(1 << 16)		/* OHCI clock enable */
#define USBCFG_FLA(x)	(((x) & 0x3f) << 8)
#define USBCFG_UCAM	(1 << 7)		/* coherent access (undoc) */
#define USBCFG_GME	(1 << 6)		/* OTG mem access */
#define USBCFG_DBE	(1 << 5)		/* UDC busmaster enable */
#define USBCFG_DME	(1 << 4)		/* UDC mem enable */
#define USBCFG_EBE	(1 << 3)		/* EHCI busmaster enable */
#define USBCFG_EME	(1 << 2)		/* EHCI mem enable */
#define USBCFG_OBE	(1 << 1)		/* OHCI busmaster enable */
#define USBCFG_OME	(1 << 0)		/* OHCI mem enable */
#define USBCFG_INIT_AU1200	(USBCFG_PFEN | USBCFG_RDCOMB | USBCFG_UNKNOWN |\
				 USBCFG_SSD | USBCFG_FLA(0x20) | USBCFG_UCAM | \
				 USBCFG_GME | USBCFG_DBE | USBCFG_DME |	       \
				 USBCFG_EBE | USBCFG_EME | USBCFG_OBE |	       \
				 USBCFG_OME)


static DEFINE_SPINLOCK(alchemy_usb_lock);


static inline void __au1200_ohci_control(void __iomem *base, int enable)
{
	unsigned long r = __raw_readl(base + AU1200_USBCFG);
	if (enable) {
		__raw_writel(r | USBCFG_OCE, base + AU1200_USBCFG);
		wmb();
		udelay(2000);
	} else {
		__raw_writel(r & ~USBCFG_OCE, base + AU1200_USBCFG);
		wmb();
		udelay(1000);
	}
}

static inline void __au1200_ehci_control(void __iomem *base, int enable)
{
	unsigned long r = __raw_readl(base + AU1200_USBCFG);
	if (enable) {
		__raw_writel(r | USBCFG_ECE | USBCFG_PPE, base + AU1200_USBCFG);
		wmb();
		udelay(1000);
	} else {
		if (!(r & USBCFG_UCE))		/* UDC also off? */
			r &= ~USBCFG_PPE;	/* yes: disable HS PHY PLL */
		__raw_writel(r & ~USBCFG_ECE, base + AU1200_USBCFG);
		wmb();
		udelay(1000);
	}
}

static inline void __au1200_udc_control(void __iomem *base, int enable)
{
	unsigned long r = __raw_readl(base + AU1200_USBCFG);
	if (enable) {
		__raw_writel(r | USBCFG_UCE | USBCFG_PPE, base + AU1200_USBCFG);
		wmb();
	} else {
		if (!(r & USBCFG_ECE))		/* EHCI also off? */
			r &= ~USBCFG_PPE;	/* yes: disable HS PHY PLL */
		__raw_writel(r & ~USBCFG_UCE, base + AU1200_USBCFG);
		wmb();
	}
}

static inline int au1200_coherency_bug(void)
{
#if defined(CONFIG_DMA_COHERENT)
	/* Au1200 AB USB does not support coherent memory */
	if (!(read_c0_prid() & 0xff)) {
		printk(KERN_INFO "Au1200 USB: this is chip revision AB !!\n");
		printk(KERN_INFO "Au1200 USB: update your board or re-configure"
				 " the kernel\n");
		return -ENODEV;
	}
#endif
	return 0;
}

static inline int au1200_usb_control(int block, int enable)
{
	void __iomem *base =
			(void __iomem *)KSEG1ADDR(AU1200_USB_CTL_PHYS_ADDR);
	int ret = 0;

	switch (block) {
	case ALCHEMY_USB_OHCI0:
		ret = au1200_coherency_bug();
		if (ret && enable)
			goto out;
		__au1200_ohci_control(base, enable);
		break;
	case ALCHEMY_USB_UDC0:
		__au1200_udc_control(base, enable);
		break;
	case ALCHEMY_USB_EHCI0:
		ret = au1200_coherency_bug();
		if (ret && enable)
			goto out;
		__au1200_ehci_control(base, enable);
		break;
	default:
		ret = -ENODEV;
	}
out:
	return ret;
}


/* initialize USB block(s) to a known working state */
static inline void au1200_usb_init(void)
{
	void __iomem *base =
			(void __iomem *)KSEG1ADDR(AU1200_USB_CTL_PHYS_ADDR);
	__raw_writel(USBCFG_INIT_AU1200, base + AU1200_USBCFG);
	wmb();
	udelay(1000);
}

static inline void au1000_usb_init(unsigned long rb, int reg)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(rb + reg);
	unsigned long r = __raw_readl(base);

#if defined(__BIG_ENDIAN)
	r |= USBHEN_BE;
#endif
	r |= USBHEN_C;

	__raw_writel(r, base);
	wmb();
	udelay(1000);
}


static inline void __au1xx0_ohci_control(int enable, unsigned long rb, int creg)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(rb);
	unsigned long r = __raw_readl(base + creg);

	if (enable) {
		__raw_writel(r | USBHEN_CE, base + creg);
		wmb();
		udelay(1000);
		__raw_writel(r | USBHEN_CE | USBHEN_E, base + creg);
		wmb();
		udelay(1000);

		/* wait for reset complete (read reg twice: au1500 erratum) */
		while (__raw_readl(base + creg),
			!(__raw_readl(base + creg) & USBHEN_RD))
			udelay(1000);
	} else {
		__raw_writel(r & ~(USBHEN_CE | USBHEN_E), base + creg);
		wmb();
	}
}

static inline int au1000_usb_control(int block, int enable, unsigned long rb,
				     int creg)
{
	int ret = 0;

	switch (block) {
	case ALCHEMY_USB_OHCI0:
		__au1xx0_ohci_control(enable, rb, creg);
		break;
	default:
		ret = -ENODEV;
	}
	return ret;
}

/*
 * alchemy_usb_control - control Alchemy on-chip USB blocks
 * @block:	USB block to target
 * @enable:	set 1 to enable a block, 0 to disable
 */
int alchemy_usb_control(int block, int enable)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&alchemy_usb_lock, flags);
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1000:
	case ALCHEMY_CPU_AU1500:
	case ALCHEMY_CPU_AU1100:
		ret = au1000_usb_control(block, enable,
				AU1000_USB_OHCI_PHYS_ADDR, AU1000_OHCICFG);
		break;
	case ALCHEMY_CPU_AU1550:
		ret = au1000_usb_control(block, enable,
				AU1550_USB_OHCI_PHYS_ADDR, AU1550_OHCICFG);
		break;
	case ALCHEMY_CPU_AU1200:
		ret = au1200_usb_control(block, enable);
		break;
	default:
		ret = -ENODEV;
	}
	spin_unlock_irqrestore(&alchemy_usb_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(alchemy_usb_control);


static unsigned long alchemy_usb_pmdata[2];

static void au1000_usb_pm(unsigned long br, int creg, int susp)
{
	void __iomem *base = (void __iomem *)KSEG1ADDR(br);

	if (susp) {
		alchemy_usb_pmdata[0] = __raw_readl(base + creg);
		/* There appears to be some undocumented reset register.... */
		__raw_writel(0, base + 0x04);
		wmb();
		__raw_writel(0, base + creg);
		wmb();
	} else {
		__raw_writel(alchemy_usb_pmdata[0], base + creg);
		wmb();
	}
}

static void au1200_usb_pm(int susp)
{
	void __iomem *base =
			(void __iomem *)KSEG1ADDR(AU1200_USB_OTG_PHYS_ADDR);
	if (susp) {
		/* save OTG_CAP/MUX registers which indicate port routing */
		/* FIXME: write an OTG driver to do that */
		alchemy_usb_pmdata[0] = __raw_readl(base + 0x00);
		alchemy_usb_pmdata[1] = __raw_readl(base + 0x04);
	} else {
		/* restore access to all MMIO areas */
		au1200_usb_init();

		/* restore OTG_CAP/MUX registers */
		__raw_writel(alchemy_usb_pmdata[0], base + 0x00);
		__raw_writel(alchemy_usb_pmdata[1], base + 0x04);
		wmb();
	}
}

static void alchemy_usb_pm(int susp)
{
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1000:
	case ALCHEMY_CPU_AU1500:
	case ALCHEMY_CPU_AU1100:
		au1000_usb_pm(AU1000_USB_OHCI_PHYS_ADDR, AU1000_OHCICFG, susp);
		break;
	case ALCHEMY_CPU_AU1550:
		au1000_usb_pm(AU1550_USB_OHCI_PHYS_ADDR, AU1550_OHCICFG, susp);
		break;
	case ALCHEMY_CPU_AU1200:
		au1200_usb_pm(susp);
		break;
	}
}

static int alchemy_usb_suspend(void)
{
	alchemy_usb_pm(1);
	return 0;
}

static void alchemy_usb_resume(void)
{
	alchemy_usb_pm(0);
}

static struct syscore_ops alchemy_usb_pm_ops = {
	.suspend	= alchemy_usb_suspend,
	.resume		= alchemy_usb_resume,
};

static int __init alchemy_usb_init(void)
{
	switch (alchemy_get_cputype()) {
	case ALCHEMY_CPU_AU1000:
	case ALCHEMY_CPU_AU1500:
	case ALCHEMY_CPU_AU1100:
		au1000_usb_init(AU1000_USB_OHCI_PHYS_ADDR, AU1000_OHCICFG);
		break;
	case ALCHEMY_CPU_AU1550:
		au1000_usb_init(AU1550_USB_OHCI_PHYS_ADDR, AU1550_OHCICFG);
		break;
	case ALCHEMY_CPU_AU1200:
		au1200_usb_init();
		break;
	}

	register_syscore_ops(&alchemy_usb_pm_ops);

	return 0;
}
arch_initcall(alchemy_usb_init);
