/*
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spmi.h>

/* PMIC Arbiter configuration registers */
#define PMIC_ARB_VERSION		0x0000
#define PMIC_ARB_VERSION_V2_MIN		0x20010000
#define PMIC_ARB_VERSION_V3_MIN		0x30000000
#define PMIC_ARB_INT_EN			0x0004

/* PMIC Arbiter channel registers offsets */
#define PMIC_ARB_CMD			0x00
#define PMIC_ARB_CONFIG			0x04
#define PMIC_ARB_STATUS			0x08
#define PMIC_ARB_WDATA0			0x10
#define PMIC_ARB_WDATA1			0x14
#define PMIC_ARB_RDATA0			0x18
#define PMIC_ARB_RDATA1			0x1C
#define PMIC_ARB_REG_APID(N)		(0x800 + 0x4 * (N))

/* Mapping Table */
#define SPMI_MAPPING_TABLE_REG(N)	(0x0B00 + (4 * (N)))
#define SPMI_MAPPING_BIT_INDEX(X)	(((X) >> 18) & 0xF)
#define SPMI_MAPPING_BIT_IS_0_FLAG(X)	(((X) >> 17) & 0x1)
#define SPMI_MAPPING_BIT_IS_0_RESULT(X)	(((X) >> 9) & 0xFF)
#define SPMI_MAPPING_BIT_IS_1_FLAG(X)	(((X) >> 8) & 0x1)
#define SPMI_MAPPING_BIT_IS_1_RESULT(X)	(((X) >> 0) & 0xFF)

#define SPMI_MAPPING_TABLE_TREE_DEPTH	16	/* Maximum of 16-bits */
#define PMIC_ARB_MAX_PPID		BIT(12) /* PPID is 12bit */
#define PMIC_ARB_APID_VALID		BIT(15)

/* Ownership Table */
#define SPMI_OWNERSHIP_TABLE_REG(N)	(0x0700 + (4 * (N)))
#define SPMI_OWNERSHIP_PERIPH2OWNER(X)	((X) & 0x7)

/* Channel Status fields */
enum pmic_arb_chnl_status {
	PMIC_ARB_STATUS_DONE	= BIT(0),
	PMIC_ARB_STATUS_FAILURE	= BIT(1),
	PMIC_ARB_STATUS_DENIED	= BIT(2),
	PMIC_ARB_STATUS_DROPPED	= BIT(3),
};

/* Command register fields */
#define PMIC_ARB_CMD_MAX_BYTE_COUNT	8

/* Command Opcodes */
enum pmic_arb_cmd_op_code {
	PMIC_ARB_OP_EXT_WRITEL = 0,
	PMIC_ARB_OP_EXT_READL = 1,
	PMIC_ARB_OP_EXT_WRITE = 2,
	PMIC_ARB_OP_RESET = 3,
	PMIC_ARB_OP_SLEEP = 4,
	PMIC_ARB_OP_SHUTDOWN = 5,
	PMIC_ARB_OP_WAKEUP = 6,
	PMIC_ARB_OP_AUTHENTICATE = 7,
	PMIC_ARB_OP_MSTR_READ = 8,
	PMIC_ARB_OP_MSTR_WRITE = 9,
	PMIC_ARB_OP_EXT_READ = 13,
	PMIC_ARB_OP_WRITE = 14,
	PMIC_ARB_OP_READ = 15,
	PMIC_ARB_OP_ZERO_WRITE = 16,
};

/* Maximum number of support PMIC peripherals */
#define PMIC_ARB_MAX_PERIPHS		512
#define PMIC_ARB_TIMEOUT_US		100
#define PMIC_ARB_MAX_TRANS_BYTES	(8)

#define PMIC_ARB_APID_MASK		0xFF
#define PMIC_ARB_PPID_MASK		0xFFF

/* interrupt enable bit */
#define SPMI_PIC_ACC_ENABLE_BIT		BIT(0)

#define spec_to_hwirq(slave_id, periph_id, irq_id, apid) \
	((((slave_id) & 0xF)   << 28) | \
	(((periph_id) & 0xFF)  << 20) | \
	(((irq_id)    & 0x7)   << 16) | \
	(((apid)      & 0x1FF) << 0))

#define hwirq_to_sid(hwirq)  (((hwirq) >> 28) & 0xF)
#define hwirq_to_per(hwirq)  (((hwirq) >> 20) & 0xFF)
#define hwirq_to_irq(hwirq)  (((hwirq) >> 16) & 0x7)
#define hwirq_to_apid(hwirq) (((hwirq) >> 0)  & 0x1FF)

struct pmic_arb_ver_ops;

struct apid_data {
	u16		ppid;
	u8		owner;
};

/**
 * spmi_pmic_arb - SPMI PMIC Arbiter object
 *
 * @rd_base:		on v1 "core", on v2 "observer" register base off DT.
 * @wr_base:		on v1 "core", on v2 "chnls"    register base off DT.
 * @intr:		address of the SPMI interrupt control registers.
 * @cnfg:		address of the PMIC Arbiter configuration registers.
 * @lock:		lock to synchronize accesses.
 * @channel:		execution environment channel to use for accesses.
 * @irq:		PMIC ARB interrupt.
 * @ee:			the current Execution Environment
 * @min_apid:		minimum APID (used for bounding IRQ search)
 * @max_apid:		maximum APID
 * @mapping_table:	in-memory copy of PPID -> APID mapping table.
 * @domain:		irq domain object for PMIC IRQ domain
 * @spmic:		SPMI controller object
 * @ver_ops:		version dependent operations.
 * @ppid_to_apid	in-memory copy of PPID -> APID mapping table.
 */
struct spmi_pmic_arb {
	void __iomem		*rd_base;
	void __iomem		*wr_base;
	void __iomem		*intr;
	void __iomem		*cnfg;
	void __iomem		*core;
	resource_size_t		core_size;
	raw_spinlock_t		lock;
	u8			channel;
	int			irq;
	u8			ee;
	u16			min_apid;
	u16			max_apid;
	u32			*mapping_table;
	DECLARE_BITMAP(mapping_table_valid, PMIC_ARB_MAX_PERIPHS);
	struct irq_domain	*domain;
	struct spmi_controller	*spmic;
	const struct pmic_arb_ver_ops *ver_ops;
	u16			*ppid_to_apid;
	u16			last_apid;
	struct apid_data	apid_data[PMIC_ARB_MAX_PERIPHS];
};

/**
 * pmic_arb_ver: version dependent functionality.
 *
 * @ver_str:		version string.
 * @ppid_to_apid:	finds the apid for a given ppid.
 * @non_data_cmd:	on v1 issues an spmi non-data command.
 *			on v2 no HW support, returns -EOPNOTSUPP.
 * @offset:		on v1 offset of per-ee channel.
 *			on v2 offset of per-ee and per-ppid channel.
 * @fmt_cmd:		formats a GENI/SPMI command.
 * @owner_acc_status:	on v1 offset of PMIC_ARB_SPMI_PIC_OWNERm_ACC_STATUSn
 *			on v2 offset of SPMI_PIC_OWNERm_ACC_STATUSn.
 * @acc_enable:		on v1 offset of PMIC_ARB_SPMI_PIC_ACC_ENABLEn
 *			on v2 offset of SPMI_PIC_ACC_ENABLEn.
 * @irq_status:		on v1 offset of PMIC_ARB_SPMI_PIC_IRQ_STATUSn
 *			on v2 offset of SPMI_PIC_IRQ_STATUSn.
 * @irq_clear:		on v1 offset of PMIC_ARB_SPMI_PIC_IRQ_CLEARn
 *			on v2 offset of SPMI_PIC_IRQ_CLEARn.
 */
struct pmic_arb_ver_ops {
	const char *ver_str;
	int (*ppid_to_apid)(struct spmi_pmic_arb *pmic_arb, u8 sid, u16 addr,
			u16 *apid);
	/* spmi commands (read_cmd, write_cmd, cmd) functionality */
	int (*offset)(struct spmi_pmic_arb *pmic_arb, u8 sid, u16 addr,
		      u32 *offset);
	u32 (*fmt_cmd)(u8 opc, u8 sid, u16 addr, u8 bc);
	int (*non_data_cmd)(struct spmi_controller *ctrl, u8 opc, u8 sid);
	/* Interrupts controller functionality (offset of PIC registers) */
	u32 (*owner_acc_status)(u8 m, u16 n);
	u32 (*acc_enable)(u16 n);
	u32 (*irq_status)(u16 n);
	u32 (*irq_clear)(u16 n);
};

static inline void pmic_arb_base_write(struct spmi_pmic_arb *pmic_arb,
				       u32 offset, u32 val)
{
	writel_relaxed(val, pmic_arb->wr_base + offset);
}

static inline void pmic_arb_set_rd_cmd(struct spmi_pmic_arb *pmic_arb,
				       u32 offset, u32 val)
{
	writel_relaxed(val, pmic_arb->rd_base + offset);
}

/**
 * pmic_arb_read_data: reads pmic-arb's register and copy 1..4 bytes to buf
 * @bc:		byte count -1. range: 0..3
 * @reg:	register's address
 * @buf:	output parameter, length must be bc + 1
 */
static void
pmic_arb_read_data(struct spmi_pmic_arb *pmic_arb, u8 *buf, u32 reg, u8 bc)
{
	u32 data = __raw_readl(pmic_arb->rd_base + reg);

	memcpy(buf, &data, (bc & 3) + 1);
}

/**
 * pmic_arb_write_data: write 1..4 bytes from buf to pmic-arb's register
 * @bc:		byte-count -1. range: 0..3.
 * @reg:	register's address.
 * @buf:	buffer to write. length must be bc + 1.
 */
static void pmic_arb_write_data(struct spmi_pmic_arb *pmic_arb, const u8 *buf,
				u32 reg, u8 bc)
{
	u32 data = 0;

	memcpy(&data, buf, (bc & 3) + 1);
	pmic_arb_base_write(pmic_arb, reg, data);
}

static int pmic_arb_wait_for_done(struct spmi_controller *ctrl,
				  void __iomem *base, u8 sid, u16 addr)
{
	struct spmi_pmic_arb *pmic_arb = spmi_controller_get_drvdata(ctrl);
	u32 status = 0;
	u32 timeout = PMIC_ARB_TIMEOUT_US;
	u32 offset;
	int rc;

	rc = pmic_arb->ver_ops->offset(pmic_arb, sid, addr, &offset);
	if (rc)
		return rc;

	offset += PMIC_ARB_STATUS;

	while (timeout--) {
		status = readl_relaxed(base + offset);

		if (status & PMIC_ARB_STATUS_DONE) {
			if (status & PMIC_ARB_STATUS_DENIED) {
				dev_err(&ctrl->dev, "%s: transaction denied (0x%x)\n",
					__func__, status);
				return -EPERM;
			}

			if (status & PMIC_ARB_STATUS_FAILURE) {
				dev_err(&ctrl->dev, "%s: transaction failed (0x%x)\n",
					__func__, status);
				return -EIO;
			}

			if (status & PMIC_ARB_STATUS_DROPPED) {
				dev_err(&ctrl->dev, "%s: transaction dropped (0x%x)\n",
					__func__, status);
				return -EIO;
			}

			return 0;
		}
		udelay(1);
	}

	dev_err(&ctrl->dev, "%s: timeout, status 0x%x\n",
		__func__, status);
	return -ETIMEDOUT;
}

static int
pmic_arb_non_data_cmd_v1(struct spmi_controller *ctrl, u8 opc, u8 sid)
{
	struct spmi_pmic_arb *pmic_arb = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	u32 cmd;
	int rc;
	u32 offset;

	rc = pmic_arb->ver_ops->offset(pmic_arb, sid, 0, &offset);
	if (rc)
		return rc;

	cmd = ((opc | 0x40) << 27) | ((sid & 0xf) << 20);

	raw_spin_lock_irqsave(&pmic_arb->lock, flags);
	pmic_arb_base_write(pmic_arb, offset + PMIC_ARB_CMD, cmd);
	rc = pmic_arb_wait_for_done(ctrl, pmic_arb->wr_base, sid, 0);
	raw_spin_unlock_irqrestore(&pmic_arb->lock, flags);

	return rc;
}

static int
pmic_arb_non_data_cmd_v2(struct spmi_controller *ctrl, u8 opc, u8 sid)
{
	return -EOPNOTSUPP;
}

/* Non-data command */
static int pmic_arb_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid)
{
	struct spmi_pmic_arb *pmic_arb = spmi_controller_get_drvdata(ctrl);

	dev_dbg(&ctrl->dev, "cmd op:0x%x sid:%d\n", opc, sid);

	/* Check for valid non-data command */
	if (opc < SPMI_CMD_RESET || opc > SPMI_CMD_WAKEUP)
		return -EINVAL;

	return pmic_arb->ver_ops->non_data_cmd(ctrl, opc, sid);
}

static int pmic_arb_read_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			     u16 addr, u8 *buf, size_t len)
{
	struct spmi_pmic_arb *pmic_arb = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	u8 bc = len - 1;
	u32 cmd;
	int rc;
	u32 offset;

	rc = pmic_arb->ver_ops->offset(pmic_arb, sid, addr, &offset);
	if (rc)
		return rc;

	if (bc >= PMIC_ARB_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev, "pmic-arb supports 1..%d bytes per trans, but:%zu requested",
			PMIC_ARB_MAX_TRANS_BYTES, len);
		return  -EINVAL;
	}

	/* Check the opcode */
	if (opc >= 0x60 && opc <= 0x7F)
		opc = PMIC_ARB_OP_READ;
	else if (opc >= 0x20 && opc <= 0x2F)
		opc = PMIC_ARB_OP_EXT_READ;
	else if (opc >= 0x38 && opc <= 0x3F)
		opc = PMIC_ARB_OP_EXT_READL;
	else
		return -EINVAL;

	cmd = pmic_arb->ver_ops->fmt_cmd(opc, sid, addr, bc);

	raw_spin_lock_irqsave(&pmic_arb->lock, flags);
	pmic_arb_set_rd_cmd(pmic_arb, offset + PMIC_ARB_CMD, cmd);
	rc = pmic_arb_wait_for_done(ctrl, pmic_arb->rd_base, sid, addr);
	if (rc)
		goto done;

	pmic_arb_read_data(pmic_arb, buf, offset + PMIC_ARB_RDATA0,
		     min_t(u8, bc, 3));

	if (bc > 3)
		pmic_arb_read_data(pmic_arb, buf + 4, offset + PMIC_ARB_RDATA1,
					bc - 4);

done:
	raw_spin_unlock_irqrestore(&pmic_arb->lock, flags);
	return rc;
}

static int pmic_arb_write_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			      u16 addr, const u8 *buf, size_t len)
{
	struct spmi_pmic_arb *pmic_arb = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	u8 bc = len - 1;
	u32 cmd;
	int rc;
	u32 offset;

	rc = pmic_arb->ver_ops->offset(pmic_arb, sid, addr, &offset);
	if (rc)
		return rc;

	if (bc >= PMIC_ARB_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev, "pmic-arb supports 1..%d bytes per trans, but:%zu requested",
			PMIC_ARB_MAX_TRANS_BYTES, len);
		return  -EINVAL;
	}

	/* Check the opcode */
	if (opc >= 0x40 && opc <= 0x5F)
		opc = PMIC_ARB_OP_WRITE;
	else if (opc >= 0x00 && opc <= 0x0F)
		opc = PMIC_ARB_OP_EXT_WRITE;
	else if (opc >= 0x30 && opc <= 0x37)
		opc = PMIC_ARB_OP_EXT_WRITEL;
	else if (opc >= 0x80)
		opc = PMIC_ARB_OP_ZERO_WRITE;
	else
		return -EINVAL;

	cmd = pmic_arb->ver_ops->fmt_cmd(opc, sid, addr, bc);

	/* Write data to FIFOs */
	raw_spin_lock_irqsave(&pmic_arb->lock, flags);
	pmic_arb_write_data(pmic_arb, buf, offset + PMIC_ARB_WDATA0,
				min_t(u8, bc, 3));
	if (bc > 3)
		pmic_arb_write_data(pmic_arb, buf + 4, offset + PMIC_ARB_WDATA1,
					bc - 4);

	/* Start the transaction */
	pmic_arb_base_write(pmic_arb, offset + PMIC_ARB_CMD, cmd);
	rc = pmic_arb_wait_for_done(ctrl, pmic_arb->wr_base, sid, addr);
	raw_spin_unlock_irqrestore(&pmic_arb->lock, flags);

	return rc;
}

enum qpnpint_regs {
	QPNPINT_REG_RT_STS		= 0x10,
	QPNPINT_REG_SET_TYPE		= 0x11,
	QPNPINT_REG_POLARITY_HIGH	= 0x12,
	QPNPINT_REG_POLARITY_LOW	= 0x13,
	QPNPINT_REG_LATCHED_CLR		= 0x14,
	QPNPINT_REG_EN_SET		= 0x15,
	QPNPINT_REG_EN_CLR		= 0x16,
	QPNPINT_REG_LATCHED_STS		= 0x18,
};

struct spmi_pmic_arb_qpnpint_type {
	u8 type; /* 1 -> edge */
	u8 polarity_high;
	u8 polarity_low;
} __packed;

/* Simplified accessor functions for irqchip callbacks */
static void qpnpint_spmi_write(struct irq_data *d, u8 reg, void *buf,
			       size_t len)
{
	struct spmi_pmic_arb *pmic_arb = irq_data_get_irq_chip_data(d);
	u8 sid = hwirq_to_sid(d->hwirq);
	u8 per = hwirq_to_per(d->hwirq);

	if (pmic_arb_write_cmd(pmic_arb->spmic, SPMI_CMD_EXT_WRITEL, sid,
			       (per << 8) + reg, buf, len))
		dev_err_ratelimited(&pmic_arb->spmic->dev, "failed irqchip transaction on %x\n",
				    d->irq);
}

static void qpnpint_spmi_read(struct irq_data *d, u8 reg, void *buf, size_t len)
{
	struct spmi_pmic_arb *pmic_arb = irq_data_get_irq_chip_data(d);
	u8 sid = hwirq_to_sid(d->hwirq);
	u8 per = hwirq_to_per(d->hwirq);

	if (pmic_arb_read_cmd(pmic_arb->spmic, SPMI_CMD_EXT_READL, sid,
			      (per << 8) + reg, buf, len))
		dev_err_ratelimited(&pmic_arb->spmic->dev, "failed irqchip transaction on %x\n",
				    d->irq);
}

static void cleanup_irq(struct spmi_pmic_arb *pmic_arb, u16 apid, int id)
{
	u16 ppid = pmic_arb->apid_data[apid].ppid;
	u8 sid = ppid >> 8;
	u8 per = ppid & 0xFF;
	u8 irq_mask = BIT(id);

	writel_relaxed(irq_mask, pmic_arb->intr +
				pmic_arb->ver_ops->irq_clear(apid));

	if (pmic_arb_write_cmd(pmic_arb->spmic, SPMI_CMD_EXT_WRITEL, sid,
			(per << 8) + QPNPINT_REG_LATCHED_CLR, &irq_mask, 1))
		dev_err_ratelimited(&pmic_arb->spmic->dev, "failed to ack irq_mask = 0x%x for ppid = %x\n",
				irq_mask, ppid);

	if (pmic_arb_write_cmd(pmic_arb->spmic, SPMI_CMD_EXT_WRITEL, sid,
			       (per << 8) + QPNPINT_REG_EN_CLR, &irq_mask, 1))
		dev_err_ratelimited(&pmic_arb->spmic->dev, "failed to ack irq_mask = 0x%x for ppid = %x\n",
				irq_mask, ppid);
}

static void periph_interrupt(struct spmi_pmic_arb *pmic_arb, u16 apid)
{
	unsigned int irq;
	u32 status;
	int id;
	u8 sid = (pmic_arb->apid_data[apid].ppid >> 8) & 0xF;
	u8 per = pmic_arb->apid_data[apid].ppid & 0xFF;

	status = readl_relaxed(pmic_arb->intr +
				pmic_arb->ver_ops->irq_status(apid));
	while (status) {
		id = ffs(status) - 1;
		status &= ~BIT(id);
		irq = irq_find_mapping(pmic_arb->domain,
					spec_to_hwirq(sid, per, id, apid));
		if (irq == 0) {
			cleanup_irq(pmic_arb, apid, id);
			continue;
		}
		generic_handle_irq(irq);
	}
}

static void pmic_arb_chained_irq(struct irq_desc *desc)
{
	struct spmi_pmic_arb *pmic_arb = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	void __iomem *intr = pmic_arb->intr;
	int first = pmic_arb->min_apid >> 5;
	int last = pmic_arb->max_apid >> 5;
	u32 status, enable;
	int i, id, apid;

	chained_irq_enter(chip, desc);

	for (i = first; i <= last; ++i) {
		status = readl_relaxed(intr +
			 pmic_arb->ver_ops->owner_acc_status(pmic_arb->ee, i));
		while (status) {
			id = ffs(status) - 1;
			status &= ~BIT(id);
			apid = id + i * 32;
			enable = readl_relaxed(intr +
					pmic_arb->ver_ops->acc_enable(apid));
			if (enable & SPMI_PIC_ACC_ENABLE_BIT)
				periph_interrupt(pmic_arb, apid);
		}
	}

	chained_irq_exit(chip, desc);
}

static void qpnpint_irq_ack(struct irq_data *d)
{
	struct spmi_pmic_arb *pmic_arb = irq_data_get_irq_chip_data(d);
	u8 irq = hwirq_to_irq(d->hwirq);
	u16 apid = hwirq_to_apid(d->hwirq);
	u8 data;

	writel_relaxed(BIT(irq), pmic_arb->intr +
				pmic_arb->ver_ops->irq_clear(apid));

	data = BIT(irq);
	qpnpint_spmi_write(d, QPNPINT_REG_LATCHED_CLR, &data, 1);
}

static void qpnpint_irq_mask(struct irq_data *d)
{
	u8 irq = hwirq_to_irq(d->hwirq);
	u8 data = BIT(irq);

	qpnpint_spmi_write(d, QPNPINT_REG_EN_CLR, &data, 1);
}

static void qpnpint_irq_unmask(struct irq_data *d)
{
	struct spmi_pmic_arb *pmic_arb = irq_data_get_irq_chip_data(d);
	u8 irq = hwirq_to_irq(d->hwirq);
	u16 apid = hwirq_to_apid(d->hwirq);
	u8 buf[2];

	writel_relaxed(SPMI_PIC_ACC_ENABLE_BIT,
		pmic_arb->intr + pmic_arb->ver_ops->acc_enable(apid));

	qpnpint_spmi_read(d, QPNPINT_REG_EN_SET, &buf[0], 1);
	if (!(buf[0] & BIT(irq))) {
		/*
		 * Since the interrupt is currently disabled, write to both the
		 * LATCHED_CLR and EN_SET registers so that a spurious interrupt
		 * cannot be triggered when the interrupt is enabled
		 */
		buf[0] = BIT(irq);
		buf[1] = BIT(irq);
		qpnpint_spmi_write(d, QPNPINT_REG_LATCHED_CLR, &buf, 2);
	}
}

static int qpnpint_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct spmi_pmic_arb_qpnpint_type type;
	irq_flow_handler_t flow_handler;
	u8 irq = hwirq_to_irq(d->hwirq);

	qpnpint_spmi_read(d, QPNPINT_REG_SET_TYPE, &type, sizeof(type));

	if (flow_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		type.type |= BIT(irq);
		if (flow_type & IRQF_TRIGGER_RISING)
			type.polarity_high |= BIT(irq);
		if (flow_type & IRQF_TRIGGER_FALLING)
			type.polarity_low  |= BIT(irq);

		flow_handler = handle_edge_irq;
	} else {
		if ((flow_type & (IRQF_TRIGGER_HIGH)) &&
		    (flow_type & (IRQF_TRIGGER_LOW)))
			return -EINVAL;

		type.type &= ~BIT(irq); /* level trig */
		if (flow_type & IRQF_TRIGGER_HIGH)
			type.polarity_high |= BIT(irq);
		else
			type.polarity_low  |= BIT(irq);

		flow_handler = handle_level_irq;
	}

	qpnpint_spmi_write(d, QPNPINT_REG_SET_TYPE, &type, sizeof(type));
	irq_set_handler_locked(d, flow_handler);

	return 0;
}

static int qpnpint_get_irqchip_state(struct irq_data *d,
				     enum irqchip_irq_state which,
				     bool *state)
{
	u8 irq = hwirq_to_irq(d->hwirq);
	u8 status = 0;

	if (which != IRQCHIP_STATE_LINE_LEVEL)
		return -EINVAL;

	qpnpint_spmi_read(d, QPNPINT_REG_RT_STS, &status, 1);
	*state = !!(status & BIT(irq));

	return 0;
}

static struct irq_chip pmic_arb_irqchip = {
	.name		= "pmic_arb",
	.irq_ack	= qpnpint_irq_ack,
	.irq_mask	= qpnpint_irq_mask,
	.irq_unmask	= qpnpint_irq_unmask,
	.irq_set_type	= qpnpint_irq_set_type,
	.irq_get_irqchip_state	= qpnpint_get_irqchip_state,
	.flags		= IRQCHIP_MASK_ON_SUSPEND
			| IRQCHIP_SKIP_SET_WAKE,
};

static int qpnpint_irq_domain_dt_translate(struct irq_domain *d,
					   struct device_node *controller,
					   const u32 *intspec,
					   unsigned int intsize,
					   unsigned long *out_hwirq,
					   unsigned int *out_type)
{
	struct spmi_pmic_arb *pmic_arb = d->host_data;
	int rc;
	u16 apid;

	dev_dbg(&pmic_arb->spmic->dev, "intspec[0] 0x%1x intspec[1] 0x%02x intspec[2] 0x%02x\n",
		intspec[0], intspec[1], intspec[2]);

	if (irq_domain_get_of_node(d) != controller)
		return -EINVAL;
	if (intsize != 4)
		return -EINVAL;
	if (intspec[0] > 0xF || intspec[1] > 0xFF || intspec[2] > 0x7)
		return -EINVAL;

	rc = pmic_arb->ver_ops->ppid_to_apid(pmic_arb, intspec[0],
					    (intspec[1] << 8), &apid);
	if (rc < 0) {
		dev_err(&pmic_arb->spmic->dev, "failed to xlate sid = 0x%x, periph = 0x%x, irq = %x rc = %d\n",
		intspec[0], intspec[1], intspec[2], rc);
		return rc;
	}

	/* Keep track of {max,min}_apid for bounding search during interrupt */
	if (apid > pmic_arb->max_apid)
		pmic_arb->max_apid = apid;
	if (apid < pmic_arb->min_apid)
		pmic_arb->min_apid = apid;

	*out_hwirq = spec_to_hwirq(intspec[0], intspec[1], intspec[2], apid);
	*out_type  = intspec[3] & IRQ_TYPE_SENSE_MASK;

	dev_dbg(&pmic_arb->spmic->dev, "out_hwirq = %lu\n", *out_hwirq);

	return 0;
}

static int qpnpint_irq_domain_map(struct irq_domain *d,
				  unsigned int virq,
				  irq_hw_number_t hwirq)
{
	struct spmi_pmic_arb *pmic_arb = d->host_data;

	dev_dbg(&pmic_arb->spmic->dev, "virq = %u, hwirq = %lu\n", virq, hwirq);

	irq_set_chip_and_handler(virq, &pmic_arb_irqchip, handle_level_irq);
	irq_set_chip_data(virq, d->host_data);
	irq_set_noprobe(virq);
	return 0;
}

static int pmic_arb_ppid_to_apid_v1(struct spmi_pmic_arb *pmic_arb, u8 sid,
				    u16 addr, u16 *apid)
{
	u16 ppid = sid << 8 | ((addr >> 8) & 0xFF);
	u32 *mapping_table = pmic_arb->mapping_table;
	int index = 0, i;
	u16 apid_valid;
	u32 data;

	apid_valid = pmic_arb->ppid_to_apid[ppid];
	if (apid_valid & PMIC_ARB_APID_VALID) {
		*apid = apid_valid & ~PMIC_ARB_APID_VALID;
		return 0;
	}

	for (i = 0; i < SPMI_MAPPING_TABLE_TREE_DEPTH; ++i) {
		if (!test_and_set_bit(index, pmic_arb->mapping_table_valid))
			mapping_table[index] = readl_relaxed(pmic_arb->cnfg +
						SPMI_MAPPING_TABLE_REG(index));

		data = mapping_table[index];

		if (ppid & BIT(SPMI_MAPPING_BIT_INDEX(data))) {
			if (SPMI_MAPPING_BIT_IS_1_FLAG(data)) {
				index = SPMI_MAPPING_BIT_IS_1_RESULT(data);
			} else {
				*apid = SPMI_MAPPING_BIT_IS_1_RESULT(data);
				pmic_arb->ppid_to_apid[ppid]
					= *apid | PMIC_ARB_APID_VALID;
				pmic_arb->apid_data[*apid].ppid = ppid;
				return 0;
			}
		} else {
			if (SPMI_MAPPING_BIT_IS_0_FLAG(data)) {
				index = SPMI_MAPPING_BIT_IS_0_RESULT(data);
			} else {
				*apid = SPMI_MAPPING_BIT_IS_0_RESULT(data);
				pmic_arb->ppid_to_apid[ppid]
					= *apid | PMIC_ARB_APID_VALID;
				pmic_arb->apid_data[*apid].ppid = ppid;
				return 0;
			}
		}
	}

	return -ENODEV;
}

/* v1 offset per ee */
static int pmic_arb_offset_v1(struct spmi_pmic_arb *pmic_arb, u8 sid, u16 addr,
				u32 *offset)
{
	*offset = 0x800 + 0x80 * pmic_arb->channel;
	return 0;
}

static u16 pmic_arb_find_apid(struct spmi_pmic_arb *pmic_arb, u16 ppid)
{
	struct apid_data *apidd = &pmic_arb->apid_data[pmic_arb->last_apid];
	u32 regval, offset;
	u16 id, apid;

	/*
	 * PMIC_ARB_REG_APID is a table in HW mapping apid to ppid.
	 * ppid_to_apid is an in-memory invert of that table.
	 */
	for (apid = pmic_arb->last_apid; ; apid++, apidd++) {
		offset = PMIC_ARB_REG_APID(apid);
		if (offset >= pmic_arb->core_size)
			break;

		regval = readl_relaxed(pmic_arb->cnfg +
				      SPMI_OWNERSHIP_TABLE_REG(apid));
		apidd->owner = SPMI_OWNERSHIP_PERIPH2OWNER(regval);

		regval = readl_relaxed(pmic_arb->core + offset);
		if (!regval)
			continue;

		id = (regval >> 8) & PMIC_ARB_PPID_MASK;
		pmic_arb->ppid_to_apid[id] = apid | PMIC_ARB_APID_VALID;
		apidd->ppid = id;
		if (id == ppid) {
			apid |= PMIC_ARB_APID_VALID;
			break;
		}
	}
	pmic_arb->last_apid = apid & ~PMIC_ARB_APID_VALID;

	return apid;
}

static int pmic_arb_ppid_to_apid_v2(struct spmi_pmic_arb *pmic_arb, u8 sid,
				    u16 addr, u16 *apid)
{
	u16 ppid = (sid << 8) | (addr >> 8);
	u16 apid_valid;

	apid_valid = pmic_arb->ppid_to_apid[ppid];
	if (!(apid_valid & PMIC_ARB_APID_VALID))
		apid_valid = pmic_arb_find_apid(pmic_arb, ppid);
	if (!(apid_valid & PMIC_ARB_APID_VALID))
		return -ENODEV;

	*apid = apid_valid & ~PMIC_ARB_APID_VALID;
	return 0;
}

/* v2 offset per ppid and per ee */
static int pmic_arb_offset_v2(struct spmi_pmic_arb *pmic_arb, u8 sid, u16 addr,
				u32 *offset)
{
	u16 apid;
	int rc;

	rc = pmic_arb_ppid_to_apid_v2(pmic_arb, sid, addr, &apid);
	if (rc < 0)
		return rc;

	*offset = 0x1000 * pmic_arb->ee + 0x8000 * apid;
	return 0;
}

static u32 pmic_arb_fmt_cmd_v1(u8 opc, u8 sid, u16 addr, u8 bc)
{
	return (opc << 27) | ((sid & 0xf) << 20) | (addr << 4) | (bc & 0x7);
}

static u32 pmic_arb_fmt_cmd_v2(u8 opc, u8 sid, u16 addr, u8 bc)
{
	return (opc << 27) | ((addr & 0xff) << 4) | (bc & 0x7);
}

static u32 pmic_arb_owner_acc_status_v1(u8 m, u16 n)
{
	return 0x20 * m + 0x4 * n;
}

static u32 pmic_arb_owner_acc_status_v2(u8 m, u16 n)
{
	return 0x100000 + 0x1000 * m + 0x4 * n;
}

static u32 pmic_arb_owner_acc_status_v3(u8 m, u16 n)
{
	return 0x200000 + 0x1000 * m + 0x4 * n;
}

static u32 pmic_arb_acc_enable_v1(u16 n)
{
	return 0x200 + 0x4 * n;
}

static u32 pmic_arb_acc_enable_v2(u16 n)
{
	return 0x1000 * n;
}

static u32 pmic_arb_irq_status_v1(u16 n)
{
	return 0x600 + 0x4 * n;
}

static u32 pmic_arb_irq_status_v2(u16 n)
{
	return 0x4 + 0x1000 * n;
}

static u32 pmic_arb_irq_clear_v1(u16 n)
{
	return 0xA00 + 0x4 * n;
}

static u32 pmic_arb_irq_clear_v2(u16 n)
{
	return 0x8 + 0x1000 * n;
}

static const struct pmic_arb_ver_ops pmic_arb_v1 = {
	.ver_str		= "v1",
	.ppid_to_apid		= pmic_arb_ppid_to_apid_v1,
	.non_data_cmd		= pmic_arb_non_data_cmd_v1,
	.offset			= pmic_arb_offset_v1,
	.fmt_cmd		= pmic_arb_fmt_cmd_v1,
	.owner_acc_status	= pmic_arb_owner_acc_status_v1,
	.acc_enable		= pmic_arb_acc_enable_v1,
	.irq_status		= pmic_arb_irq_status_v1,
	.irq_clear		= pmic_arb_irq_clear_v1,
};

static const struct pmic_arb_ver_ops pmic_arb_v2 = {
	.ver_str		= "v2",
	.ppid_to_apid		= pmic_arb_ppid_to_apid_v2,
	.non_data_cmd		= pmic_arb_non_data_cmd_v2,
	.offset			= pmic_arb_offset_v2,
	.fmt_cmd		= pmic_arb_fmt_cmd_v2,
	.owner_acc_status	= pmic_arb_owner_acc_status_v2,
	.acc_enable		= pmic_arb_acc_enable_v2,
	.irq_status		= pmic_arb_irq_status_v2,
	.irq_clear		= pmic_arb_irq_clear_v2,
};

static const struct pmic_arb_ver_ops pmic_arb_v3 = {
	.ver_str		= "v3",
	.ppid_to_apid		= pmic_arb_ppid_to_apid_v2,
	.non_data_cmd		= pmic_arb_non_data_cmd_v2,
	.offset			= pmic_arb_offset_v2,
	.fmt_cmd		= pmic_arb_fmt_cmd_v2,
	.owner_acc_status	= pmic_arb_owner_acc_status_v3,
	.acc_enable		= pmic_arb_acc_enable_v2,
	.irq_status		= pmic_arb_irq_status_v2,
	.irq_clear		= pmic_arb_irq_clear_v2,
};

static const struct irq_domain_ops pmic_arb_irq_domain_ops = {
	.map	= qpnpint_irq_domain_map,
	.xlate	= qpnpint_irq_domain_dt_translate,
};

static int spmi_pmic_arb_probe(struct platform_device *pdev)
{
	struct spmi_pmic_arb *pmic_arb;
	struct spmi_controller *ctrl;
	struct resource *res;
	void __iomem *core;
	u32 *mapping_table;
	u32 channel, ee, hw_ver;
	int err;

	ctrl = spmi_controller_alloc(&pdev->dev, sizeof(*pmic_arb));
	if (!ctrl)
		return -ENOMEM;

	pmic_arb = spmi_controller_get_drvdata(ctrl);
	pmic_arb->spmic = ctrl;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "core");
	pmic_arb->core_size = resource_size(res);

	core = devm_ioremap_resource(&ctrl->dev, res);
	if (IS_ERR(core)) {
		err = PTR_ERR(core);
		goto err_put_ctrl;
	}

	pmic_arb->ppid_to_apid = devm_kcalloc(&ctrl->dev, PMIC_ARB_MAX_PPID,
					      sizeof(*pmic_arb->ppid_to_apid),
					      GFP_KERNEL);
	if (!pmic_arb->ppid_to_apid) {
		err = -ENOMEM;
		goto err_put_ctrl;
	}

	hw_ver = readl_relaxed(core + PMIC_ARB_VERSION);

	if (hw_ver < PMIC_ARB_VERSION_V2_MIN) {
		pmic_arb->ver_ops = &pmic_arb_v1;
		pmic_arb->wr_base = core;
		pmic_arb->rd_base = core;
	} else {
		pmic_arb->core = core;

		if (hw_ver < PMIC_ARB_VERSION_V3_MIN)
			pmic_arb->ver_ops = &pmic_arb_v2;
		else
			pmic_arb->ver_ops = &pmic_arb_v3;

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "obsrvr");
		pmic_arb->rd_base = devm_ioremap_resource(&ctrl->dev, res);
		if (IS_ERR(pmic_arb->rd_base)) {
			err = PTR_ERR(pmic_arb->rd_base);
			goto err_put_ctrl;
		}

		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "chnls");
		pmic_arb->wr_base = devm_ioremap_resource(&ctrl->dev, res);
		if (IS_ERR(pmic_arb->wr_base)) {
			err = PTR_ERR(pmic_arb->wr_base);
			goto err_put_ctrl;
		}
	}

	dev_info(&ctrl->dev, "PMIC arbiter version %s (0x%x)\n",
		 pmic_arb->ver_ops->ver_str, hw_ver);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "intr");
	pmic_arb->intr = devm_ioremap_resource(&ctrl->dev, res);
	if (IS_ERR(pmic_arb->intr)) {
		err = PTR_ERR(pmic_arb->intr);
		goto err_put_ctrl;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cnfg");
	pmic_arb->cnfg = devm_ioremap_resource(&ctrl->dev, res);
	if (IS_ERR(pmic_arb->cnfg)) {
		err = PTR_ERR(pmic_arb->cnfg);
		goto err_put_ctrl;
	}

	pmic_arb->irq = platform_get_irq_byname(pdev, "periph_irq");
	if (pmic_arb->irq < 0) {
		err = pmic_arb->irq;
		goto err_put_ctrl;
	}

	err = of_property_read_u32(pdev->dev.of_node, "qcom,channel", &channel);
	if (err) {
		dev_err(&pdev->dev, "channel unspecified.\n");
		goto err_put_ctrl;
	}

	if (channel > 5) {
		dev_err(&pdev->dev, "invalid channel (%u) specified.\n",
			channel);
		err = -EINVAL;
		goto err_put_ctrl;
	}

	pmic_arb->channel = channel;

	err = of_property_read_u32(pdev->dev.of_node, "qcom,ee", &ee);
	if (err) {
		dev_err(&pdev->dev, "EE unspecified.\n");
		goto err_put_ctrl;
	}

	if (ee > 5) {
		dev_err(&pdev->dev, "invalid EE (%u) specified\n", ee);
		err = -EINVAL;
		goto err_put_ctrl;
	}

	pmic_arb->ee = ee;
	mapping_table = devm_kcalloc(&ctrl->dev, PMIC_ARB_MAX_PERIPHS,
					sizeof(*mapping_table), GFP_KERNEL);
	if (!mapping_table) {
		err = -ENOMEM;
		goto err_put_ctrl;
	}

	pmic_arb->mapping_table = mapping_table;
	/* Initialize max_apid/min_apid to the opposite bounds, during
	 * the irq domain translation, we are sure to update these */
	pmic_arb->max_apid = 0;
	pmic_arb->min_apid = PMIC_ARB_MAX_PERIPHS - 1;

	platform_set_drvdata(pdev, ctrl);
	raw_spin_lock_init(&pmic_arb->lock);

	ctrl->cmd = pmic_arb_cmd;
	ctrl->read_cmd = pmic_arb_read_cmd;
	ctrl->write_cmd = pmic_arb_write_cmd;

	dev_dbg(&pdev->dev, "adding irq domain\n");
	pmic_arb->domain = irq_domain_add_tree(pdev->dev.of_node,
					 &pmic_arb_irq_domain_ops, pmic_arb);
	if (!pmic_arb->domain) {
		dev_err(&pdev->dev, "unable to create irq_domain\n");
		err = -ENOMEM;
		goto err_put_ctrl;
	}

	irq_set_chained_handler_and_data(pmic_arb->irq, pmic_arb_chained_irq,
					pmic_arb);
	enable_irq_wake(pmic_arb->irq);

	err = spmi_controller_add(ctrl);
	if (err)
		goto err_domain_remove;

	return 0;

err_domain_remove:
	irq_set_chained_handler_and_data(pmic_arb->irq, NULL, NULL);
	irq_domain_remove(pmic_arb->domain);
err_put_ctrl:
	spmi_controller_put(ctrl);
	return err;
}

static int spmi_pmic_arb_remove(struct platform_device *pdev)
{
	struct spmi_controller *ctrl = platform_get_drvdata(pdev);
	struct spmi_pmic_arb *pmic_arb = spmi_controller_get_drvdata(ctrl);
	spmi_controller_remove(ctrl);
	irq_set_chained_handler_and_data(pmic_arb->irq, NULL, NULL);
	irq_domain_remove(pmic_arb->domain);
	spmi_controller_put(ctrl);
	return 0;
}

static const struct of_device_id spmi_pmic_arb_match_table[] = {
	{ .compatible = "qcom,spmi-pmic-arb", },
	{},
};
MODULE_DEVICE_TABLE(of, spmi_pmic_arb_match_table);

static struct platform_driver spmi_pmic_arb_driver = {
	.probe		= spmi_pmic_arb_probe,
	.remove		= spmi_pmic_arb_remove,
	.driver		= {
		.name	= "spmi_pmic_arb",
		.of_match_table = spmi_pmic_arb_match_table,
	},
};
module_platform_driver(spmi_pmic_arb_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:spmi_pmic_arb");
