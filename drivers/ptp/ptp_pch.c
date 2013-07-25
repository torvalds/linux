/*
 * PTP 1588 clock using the EG20T PCH
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 * Copyright (C) 2011-2012 LAPIS SEMICONDUCTOR Co., LTD.
 *
 * This code was derived from the IXP46X driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/slab.h>

#define STATION_ADDR_LEN	20
#define PCI_DEVICE_ID_PCH_1588	0x8819
#define IO_MEM_BAR 1

#define DEFAULT_ADDEND 0xA0000000
#define TICKS_NS_SHIFT  5
#define N_EXT_TS	2

enum pch_status {
	PCH_SUCCESS,
	PCH_INVALIDPARAM,
	PCH_NOTIMESTAMP,
	PCH_INTERRUPTMODEINUSE,
	PCH_FAILED,
	PCH_UNSUPPORTED,
};
/**
 * struct pch_ts_regs - IEEE 1588 registers
 */
struct pch_ts_regs {
	u32 control;
	u32 event;
	u32 addend;
	u32 accum;
	u32 test;
	u32 ts_compare;
	u32 rsystime_lo;
	u32 rsystime_hi;
	u32 systime_lo;
	u32 systime_hi;
	u32 trgt_lo;
	u32 trgt_hi;
	u32 asms_lo;
	u32 asms_hi;
	u32 amms_lo;
	u32 amms_hi;
	u32 ch_control;
	u32 ch_event;
	u32 tx_snap_lo;
	u32 tx_snap_hi;
	u32 rx_snap_lo;
	u32 rx_snap_hi;
	u32 src_uuid_lo;
	u32 src_uuid_hi;
	u32 can_status;
	u32 can_snap_lo;
	u32 can_snap_hi;
	u32 ts_sel;
	u32 ts_st[6];
	u32 reserve1[14];
	u32 stl_max_set_en;
	u32 stl_max_set;
	u32 reserve2[13];
	u32 srst;
};

#define PCH_TSC_RESET		(1 << 0)
#define PCH_TSC_TTM_MASK	(1 << 1)
#define PCH_TSC_ASMS_MASK	(1 << 2)
#define PCH_TSC_AMMS_MASK	(1 << 3)
#define PCH_TSC_PPSM_MASK	(1 << 4)
#define PCH_TSE_TTIPEND		(1 << 1)
#define PCH_TSE_SNS		(1 << 2)
#define PCH_TSE_SNM		(1 << 3)
#define PCH_TSE_PPS		(1 << 4)
#define PCH_CC_MM		(1 << 0)
#define PCH_CC_TA		(1 << 1)

#define PCH_CC_MODE_SHIFT	16
#define PCH_CC_MODE_MASK	0x001F0000
#define PCH_CC_VERSION		(1 << 31)
#define PCH_CE_TXS		(1 << 0)
#define PCH_CE_RXS		(1 << 1)
#define PCH_CE_OVR		(1 << 0)
#define PCH_CE_VAL		(1 << 1)
#define PCH_ECS_ETH		(1 << 0)

#define PCH_ECS_CAN		(1 << 1)
#define PCH_STATION_BYTES	6

#define PCH_IEEE1588_ETH	(1 << 0)
#define PCH_IEEE1588_CAN	(1 << 1)
/**
 * struct pch_dev - Driver private data
 */
struct pch_dev {
	struct pch_ts_regs __iomem *regs;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info caps;
	int exts0_enabled;
	int exts1_enabled;

	u32 mem_base;
	u32 mem_size;
	u32 irq;
	struct pci_dev *pdev;
	spinlock_t register_lock;
};

/**
 * struct pch_params - 1588 module parameter
 */
struct pch_params {
	u8 station[STATION_ADDR_LEN];
};

/* structure to hold the module parameters */
static struct pch_params pch_param = {
	"00:00:00:00:00:00"
};

/*
 * Register access functions
 */
static inline void pch_eth_enable_set(struct pch_dev *chip)
{
	u32 val;
	/* SET the eth_enable bit */
	val = ioread32(&chip->regs->ts_sel) | (PCH_ECS_ETH);
	iowrite32(val, (&chip->regs->ts_sel));
}

static u64 pch_systime_read(struct pch_ts_regs __iomem *regs)
{
	u64 ns;
	u32 lo, hi;

	lo = ioread32(&regs->systime_lo);
	hi = ioread32(&regs->systime_hi);

	ns = ((u64) hi) << 32;
	ns |= lo;
	ns <<= TICKS_NS_SHIFT;

	return ns;
}

static void pch_systime_write(struct pch_ts_regs __iomem *regs, u64 ns)
{
	u32 hi, lo;

	ns >>= TICKS_NS_SHIFT;
	hi = ns >> 32;
	lo = ns & 0xffffffff;

	iowrite32(lo, &regs->systime_lo);
	iowrite32(hi, &regs->systime_hi);
}

static inline void pch_block_reset(struct pch_dev *chip)
{
	u32 val;
	/* Reset Hardware Assist block */
	val = ioread32(&chip->regs->control) | PCH_TSC_RESET;
	iowrite32(val, (&chip->regs->control));
	val = val & ~PCH_TSC_RESET;
	iowrite32(val, (&chip->regs->control));
}

u32 pch_ch_control_read(struct pci_dev *pdev)
{
	struct pch_dev *chip = pci_get_drvdata(pdev);
	u32 val;

	val = ioread32(&chip->regs->ch_control);

	return val;
}
EXPORT_SYMBOL(pch_ch_control_read);

void pch_ch_control_write(struct pci_dev *pdev, u32 val)
{
	struct pch_dev *chip = pci_get_drvdata(pdev);

	iowrite32(val, (&chip->regs->ch_control));
}
EXPORT_SYMBOL(pch_ch_control_write);

u32 pch_ch_event_read(struct pci_dev *pdev)
{
	struct pch_dev *chip = pci_get_drvdata(pdev);
	u32 val;

	val = ioread32(&chip->regs->ch_event);

	return val;
}
EXPORT_SYMBOL(pch_ch_event_read);

void pch_ch_event_write(struct pci_dev *pdev, u32 val)
{
	struct pch_dev *chip = pci_get_drvdata(pdev);

	iowrite32(val, (&chip->regs->ch_event));
}
EXPORT_SYMBOL(pch_ch_event_write);

u32 pch_src_uuid_lo_read(struct pci_dev *pdev)
{
	struct pch_dev *chip = pci_get_drvdata(pdev);
	u32 val;

	val = ioread32(&chip->regs->src_uuid_lo);

	return val;
}
EXPORT_SYMBOL(pch_src_uuid_lo_read);

u32 pch_src_uuid_hi_read(struct pci_dev *pdev)
{
	struct pch_dev *chip = pci_get_drvdata(pdev);
	u32 val;

	val = ioread32(&chip->regs->src_uuid_hi);

	return val;
}
EXPORT_SYMBOL(pch_src_uuid_hi_read);

u64 pch_rx_snap_read(struct pci_dev *pdev)
{
	struct pch_dev *chip = pci_get_drvdata(pdev);
	u64 ns;
	u32 lo, hi;

	lo = ioread32(&chip->regs->rx_snap_lo);
	hi = ioread32(&chip->regs->rx_snap_hi);

	ns = ((u64) hi) << 32;
	ns |= lo;
	ns <<= TICKS_NS_SHIFT;

	return ns;
}
EXPORT_SYMBOL(pch_rx_snap_read);

u64 pch_tx_snap_read(struct pci_dev *pdev)
{
	struct pch_dev *chip = pci_get_drvdata(pdev);
	u64 ns;
	u32 lo, hi;

	lo = ioread32(&chip->regs->tx_snap_lo);
	hi = ioread32(&chip->regs->tx_snap_hi);

	ns = ((u64) hi) << 32;
	ns |= lo;
	ns <<= TICKS_NS_SHIFT;

	return ns;
}
EXPORT_SYMBOL(pch_tx_snap_read);

/* This function enables all 64 bits in system time registers [high & low].
This is a work-around for non continuous value in the SystemTime Register*/
static void pch_set_system_time_count(struct pch_dev *chip)
{
	iowrite32(0x01, &chip->regs->stl_max_set_en);
	iowrite32(0xFFFFFFFF, &chip->regs->stl_max_set);
	iowrite32(0x00, &chip->regs->stl_max_set_en);
}

static void pch_reset(struct pch_dev *chip)
{
	/* Reset Hardware Assist */
	pch_block_reset(chip);

	/* enable all 32 bits in system time registers */
	pch_set_system_time_count(chip);
}

/**
 * pch_set_station_address() - This API sets the station address used by
 *				    IEEE 1588 hardware when looking at PTP
 *				    traffic on the  ethernet interface
 * @addr:	dress which contain the column separated address to be used.
 */
int pch_set_station_address(u8 *addr, struct pci_dev *pdev)
{
	s32 i;
	struct pch_dev *chip = pci_get_drvdata(pdev);

	/* Verify the parameter */
	if ((chip->regs == NULL) || addr == (u8 *)NULL) {
		dev_err(&pdev->dev,
			"invalid params returning PCH_INVALIDPARAM\n");
		return PCH_INVALIDPARAM;
	}
	/* For all station address bytes */
	for (i = 0; i < PCH_STATION_BYTES; i++) {
		u32 val;
		s32 tmp;

		tmp = hex_to_bin(addr[i * 3]);
		if (tmp < 0) {
			dev_err(&pdev->dev,
				"invalid params returning PCH_INVALIDPARAM\n");
			return PCH_INVALIDPARAM;
		}
		val = tmp * 16;
		tmp = hex_to_bin(addr[(i * 3) + 1]);
		if (tmp < 0) {
			dev_err(&pdev->dev,
				"invalid params returning PCH_INVALIDPARAM\n");
			return PCH_INVALIDPARAM;
		}
		val += tmp;
		/* Expects ':' separated addresses */
		if ((i < 5) && (addr[(i * 3) + 2] != ':')) {
			dev_err(&pdev->dev,
				"invalid params returning PCH_INVALIDPARAM\n");
			return PCH_INVALIDPARAM;
		}

		/* Ideally we should set the address only after validating
							 entire string */
		dev_dbg(&pdev->dev, "invoking pch_station_set\n");
		iowrite32(val, &chip->regs->ts_st[i]);
	}
	return 0;
}
EXPORT_SYMBOL(pch_set_station_address);

/*
 * Interrupt service routine
 */
static irqreturn_t isr(int irq, void *priv)
{
	struct pch_dev *pch_dev = priv;
	struct pch_ts_regs __iomem *regs = pch_dev->regs;
	struct ptp_clock_event event;
	u32 ack = 0, lo, hi, val;

	val = ioread32(&regs->event);

	if (val & PCH_TSE_SNS) {
		ack |= PCH_TSE_SNS;
		if (pch_dev->exts0_enabled) {
			hi = ioread32(&regs->asms_hi);
			lo = ioread32(&regs->asms_lo);
			event.type = PTP_CLOCK_EXTTS;
			event.index = 0;
			event.timestamp = ((u64) hi) << 32;
			event.timestamp |= lo;
			event.timestamp <<= TICKS_NS_SHIFT;
			ptp_clock_event(pch_dev->ptp_clock, &event);
		}
	}

	if (val & PCH_TSE_SNM) {
		ack |= PCH_TSE_SNM;
		if (pch_dev->exts1_enabled) {
			hi = ioread32(&regs->amms_hi);
			lo = ioread32(&regs->amms_lo);
			event.type = PTP_CLOCK_EXTTS;
			event.index = 1;
			event.timestamp = ((u64) hi) << 32;
			event.timestamp |= lo;
			event.timestamp <<= TICKS_NS_SHIFT;
			ptp_clock_event(pch_dev->ptp_clock, &event);
		}
	}

	if (val & PCH_TSE_TTIPEND)
		ack |= PCH_TSE_TTIPEND; /* this bit seems to be always set */

	if (ack) {
		iowrite32(ack, &regs->event);
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;
}

/*
 * PTP clock operations
 */

static int ptp_pch_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	u64 adj;
	u32 diff, addend;
	int neg_adj = 0;
	struct pch_dev *pch_dev = container_of(ptp, struct pch_dev, caps);
	struct pch_ts_regs __iomem *regs = pch_dev->regs;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}
	addend = DEFAULT_ADDEND;
	adj = addend;
	adj *= ppb;
	diff = div_u64(adj, 1000000000ULL);

	addend = neg_adj ? addend - diff : addend + diff;

	iowrite32(addend, &regs->addend);

	return 0;
}

static int ptp_pch_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	s64 now;
	unsigned long flags;
	struct pch_dev *pch_dev = container_of(ptp, struct pch_dev, caps);
	struct pch_ts_regs __iomem *regs = pch_dev->regs;

	spin_lock_irqsave(&pch_dev->register_lock, flags);
	now = pch_systime_read(regs);
	now += delta;
	pch_systime_write(regs, now);
	spin_unlock_irqrestore(&pch_dev->register_lock, flags);

	return 0;
}

static int ptp_pch_gettime(struct ptp_clock_info *ptp, struct timespec *ts)
{
	u64 ns;
	u32 remainder;
	unsigned long flags;
	struct pch_dev *pch_dev = container_of(ptp, struct pch_dev, caps);
	struct pch_ts_regs __iomem *regs = pch_dev->regs;

	spin_lock_irqsave(&pch_dev->register_lock, flags);
	ns = pch_systime_read(regs);
	spin_unlock_irqrestore(&pch_dev->register_lock, flags);

	ts->tv_sec = div_u64_rem(ns, 1000000000, &remainder);
	ts->tv_nsec = remainder;
	return 0;
}

static int ptp_pch_settime(struct ptp_clock_info *ptp,
			   const struct timespec *ts)
{
	u64 ns;
	unsigned long flags;
	struct pch_dev *pch_dev = container_of(ptp, struct pch_dev, caps);
	struct pch_ts_regs __iomem *regs = pch_dev->regs;

	ns = ts->tv_sec * 1000000000ULL;
	ns += ts->tv_nsec;

	spin_lock_irqsave(&pch_dev->register_lock, flags);
	pch_systime_write(regs, ns);
	spin_unlock_irqrestore(&pch_dev->register_lock, flags);

	return 0;
}

static int ptp_pch_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *rq, int on)
{
	struct pch_dev *pch_dev = container_of(ptp, struct pch_dev, caps);

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
		switch (rq->extts.index) {
		case 0:
			pch_dev->exts0_enabled = on ? 1 : 0;
			break;
		case 1:
			pch_dev->exts1_enabled = on ? 1 : 0;
			break;
		default:
			return -EINVAL;
		}
		return 0;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static struct ptp_clock_info ptp_pch_caps = {
	.owner		= THIS_MODULE,
	.name		= "PCH timer",
	.max_adj	= 50000000,
	.n_ext_ts	= N_EXT_TS,
	.pps		= 0,
	.adjfreq	= ptp_pch_adjfreq,
	.adjtime	= ptp_pch_adjtime,
	.gettime	= ptp_pch_gettime,
	.settime	= ptp_pch_settime,
	.enable		= ptp_pch_enable,
};


#ifdef CONFIG_PM
static s32 pch_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_disable_device(pdev);
	pci_enable_wake(pdev, PCI_D3hot, 0);

	if (pci_save_state(pdev) != 0) {
		dev_err(&pdev->dev, "could not save PCI config state\n");
		return -ENOMEM;
	}
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static s32 pch_resume(struct pci_dev *pdev)
{
	s32 ret;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		return ret;
	}
	pci_enable_wake(pdev, PCI_D3hot, 0);
	return 0;
}
#else
#define pch_suspend NULL
#define pch_resume NULL
#endif

static void pch_remove(struct pci_dev *pdev)
{
	struct pch_dev *chip = pci_get_drvdata(pdev);

	ptp_clock_unregister(chip->ptp_clock);
	/* free the interrupt */
	if (pdev->irq != 0)
		free_irq(pdev->irq, chip);

	/* unmap the virtual IO memory space */
	if (chip->regs != NULL) {
		iounmap(chip->regs);
		chip->regs = NULL;
	}
	/* release the reserved IO memory space */
	if (chip->mem_base != 0) {
		release_mem_region(chip->mem_base, chip->mem_size);
		chip->mem_base = 0;
	}
	pci_disable_device(pdev);
	kfree(chip);
	dev_info(&pdev->dev, "complete\n");
}

static s32
pch_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	s32 ret;
	unsigned long flags;
	struct pch_dev *chip;

	chip = kzalloc(sizeof(struct pch_dev), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	/* enable the 1588 pci device */
	ret = pci_enable_device(pdev);
	if (ret != 0) {
		dev_err(&pdev->dev, "could not enable the pci device\n");
		goto err_pci_en;
	}

	chip->mem_base = pci_resource_start(pdev, IO_MEM_BAR);
	if (!chip->mem_base) {
		dev_err(&pdev->dev, "could not locate IO memory address\n");
		ret = -ENODEV;
		goto err_pci_start;
	}

	/* retrieve the available length of the IO memory space */
	chip->mem_size = pci_resource_len(pdev, IO_MEM_BAR);

	/* allocate the memory for the device registers */
	if (!request_mem_region(chip->mem_base, chip->mem_size, "1588_regs")) {
		dev_err(&pdev->dev,
			"could not allocate register memory space\n");
		ret = -EBUSY;
		goto err_req_mem_region;
	}

	/* get the virtual address to the 1588 registers */
	chip->regs = ioremap(chip->mem_base, chip->mem_size);

	if (!chip->regs) {
		dev_err(&pdev->dev, "Could not get virtual address\n");
		ret = -ENOMEM;
		goto err_ioremap;
	}

	chip->caps = ptp_pch_caps;
	chip->ptp_clock = ptp_clock_register(&chip->caps, &pdev->dev);
	if (IS_ERR(chip->ptp_clock)) {
		ret = PTR_ERR(chip->ptp_clock);
		goto err_ptp_clock_reg;
	}

	spin_lock_init(&chip->register_lock);

	ret = request_irq(pdev->irq, &isr, IRQF_SHARED, KBUILD_MODNAME, chip);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to get irq %d\n", pdev->irq);
		goto err_req_irq;
	}

	/* indicate success */
	chip->irq = pdev->irq;
	chip->pdev = pdev;
	pci_set_drvdata(pdev, chip);

	spin_lock_irqsave(&chip->register_lock, flags);
	/* reset the ieee1588 h/w */
	pch_reset(chip);

	iowrite32(DEFAULT_ADDEND, &chip->regs->addend);
	iowrite32(1, &chip->regs->trgt_lo);
	iowrite32(0, &chip->regs->trgt_hi);
	iowrite32(PCH_TSE_TTIPEND, &chip->regs->event);

	pch_eth_enable_set(chip);

	if (strcmp(pch_param.station, "00:00:00:00:00:00") != 0) {
		if (pch_set_station_address(pch_param.station, pdev) != 0) {
			dev_err(&pdev->dev,
			"Invalid station address parameter\n"
			"Module loaded but station address not set correctly\n"
			);
		}
	}
	spin_unlock_irqrestore(&chip->register_lock, flags);
	return 0;

err_req_irq:
	ptp_clock_unregister(chip->ptp_clock);
err_ptp_clock_reg:
	iounmap(chip->regs);
	chip->regs = NULL;

err_ioremap:
	release_mem_region(chip->mem_base, chip->mem_size);

err_req_mem_region:
	chip->mem_base = 0;

err_pci_start:
	pci_disable_device(pdev);

err_pci_en:
	kfree(chip);
	dev_err(&pdev->dev, "probe failed(ret=0x%x)\n", ret);

	return ret;
}

static DEFINE_PCI_DEVICE_TABLE(pch_ieee1588_pcidev_id) = {
	{
	  .vendor = PCI_VENDOR_ID_INTEL,
	  .device = PCI_DEVICE_ID_PCH_1588
	 },
	{0}
};

static struct pci_driver pch_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pch_ieee1588_pcidev_id,
	.probe = pch_probe,
	.remove = pch_remove,
	.suspend = pch_suspend,
	.resume = pch_resume,
};

static void __exit ptp_pch_exit(void)
{
	pci_unregister_driver(&pch_driver);
}

static s32 __init ptp_pch_init(void)
{
	s32 ret;

	/* register the driver with the pci core */
	ret = pci_register_driver(&pch_driver);

	return ret;
}

module_init(ptp_pch_init);
module_exit(ptp_pch_exit);

module_param_string(station,
		    pch_param.station, sizeof(pch_param.station), 0444);
MODULE_PARM_DESC(station,
	 "IEEE 1588 station address to use - colon separated hex values");

MODULE_AUTHOR("LAPIS SEMICONDUCTOR, <tshimizu818@gmail.com>");
MODULE_DESCRIPTION("PTP clock using the EG20T timer");
MODULE_LICENSE("GPL");
