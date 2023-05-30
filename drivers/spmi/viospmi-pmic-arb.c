// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <linux/mutex.h>

#include <linux/virtio.h>
#include <linux/virtio_spmi.h>
#include <linux/scatterlist.h>

/* Virtio ID of SPMI : 0xC003 */
#define VIRTIO_ID_SPMI			49155

/* Mapping Table */
#define PMIC_ARB_MAX_PPID		BIT(12) /* PPID is 12bit */
#define PMIC_ARB_APID_VALID		BIT(15)

/* type and subtype registers base address offsets */
#define PMIC_GPIO_REG_TYPE                      0x4
#define PMIC_GPIO_REG_SUBTYPE                   0x5

/* GPIO peripheral type and subtype out_values */
#define PMIC_GPIO_TYPE                          0x10
#define PMIC_GPIO_SUBTYPE_GPIO_4CH              0x1
#define PMIC_GPIO_SUBTYPE_GPIOC_4CH             0x5
#define PMIC_GPIO_SUBTYPE_GPIO_8CH              0x9
#define PMIC_GPIO_SUBTYPE_GPIOC_8CH             0xd
#define PMIC_GPIO_SUBTYPE_GPIO_LV               0x10
#define PMIC_GPIO_SUBTYPE_GPIO_MV               0x11

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
	struct irq_desc *desc;
};

struct virtio_spmi {
	struct virtio_device	*vdev;
	struct virtqueue		*txq;
	struct virtqueue		*rxq;
	raw_spinlock_t          txlock;
	raw_spinlock_t          rxlock;
	struct spmi_pmic_arb    *pa;
	struct virtio_spmi_config config;
	struct virtio_spmi_msg txmsg;
	struct virtio_spmi_msg rxmsgs[4];
};

/**
 * spmi_pmic_arb - SPMI PMIC Arbiter object
 *
 * @irq:		PMIC ARB interrupt.
 * @ee:			the current Execution Environment
 * @min_apid:		minimum APID (used for bounding IRQ search)
 * @max_apid:		maximum APID
 * @domain:		irq domain object for PMIC IRQ domain
 * @spmic:		SPMI controller object
 * @ver_ops:		version dependent operations.
 * @ppid_to_apid	in-memory copy of PPID -> APID mapping table.
 */
struct spmi_pmic_arb {
	int			irq;
	u8			ee;
	u16			min_apid;
	u16			max_apid;
	struct irq_domain	*domain;
	struct spmi_controller	*spmic;
	struct virtio_spmi	*vs;
	const struct pmic_arb_ver_ops *ver_ops;
	u16			*ppid_to_apid;
	struct apid_data	apid_data[PMIC_ARB_MAX_PERIPHS];
};

/**
 * pmic_arb_ver: version dependent functionality.
 *
 * @ver_str:		version string.
 * @ppid_to_apid:	finds the apid for a given ppid.
 * @fmt_cmd:		formats a GENI/SPMI command.
 */
struct pmic_arb_ver_ops {
	const char *ver_str;
	int (*ppid_to_apid)(struct spmi_pmic_arb *pa, u16 ppid);
	u32 (*fmt_cmd)(u8 opc, u8 sid, u16 addr, u8 bc);
};

static int
vspmi_pmic_arb_xfer(struct spmi_pmic_arb *pa)
{
	struct virtio_spmi *vs = pa->vs;
	struct virtio_spmi_msg *msg = &vs->txmsg;
	struct virtio_spmi_msg *rsp;

	struct scatterlist sg[1];
	unsigned int len;
	int rc = 0;

	sg_init_one(sg, msg, sizeof(*msg));

	rc = virtqueue_add_outbuf(vs->txq, sg, 1, msg, GFP_ATOMIC);
	if (rc) {
		dev_err(&vs->vdev->dev, "fail to add output buffer\n");
		goto out;
	}
	virtqueue_kick(vs->txq);

	do {
		rsp = virtqueue_get_buf(vs->txq, &len);
	} while (!rsp);
	rc = virtio32_to_cpu(vs->vdev, rsp->res);

out:
	return rc;
}

static struct virtio_spmi_msg *vspmi_fill_txmsg(struct spmi_pmic_arb *pa,
		u32 type, u32 cmd, u16 ppid, u32 regval)
{
	struct virtio_spmi *vs = pa->vs;
	struct virtio_spmi_msg *msg = &vs->txmsg;

	memset(msg, 0x0, sizeof(*msg));

	if (type > VIO_SPMI_BUS_CMDMAX) {
		msg->payload.irqd.ppid =
			cpu_to_virtio16(vs->vdev, ppid);
		msg->payload.irqd.regval =
			cpu_to_virtio32(vs->vdev, regval);
	} else {
		msg->payload.cmdd.cmd =
			cpu_to_virtio32(vs->vdev, cmd);
	}
	msg->type = cpu_to_virtio32(vs->vdev, type);

	return msg;
}

static void vspmi_queue_rxmsg(struct virtio_spmi *vspmi,
		struct virtio_spmi_msg *msg)
{
	struct scatterlist sg[1];

	memset(msg, 0x0, sizeof(*msg));
	sg_init_one(sg, msg, sizeof(*msg));
	virtqueue_add_inbuf(vspmi->rxq, sg, 1, msg, GFP_ATOMIC);
}

static void vspmi_fill_rxmsgs(struct virtio_spmi *vs)
{
	unsigned long flags;
	int i, size;

	raw_spin_lock_irqsave(&vs->rxlock, flags);
	size = virtqueue_get_vring_size(vs->rxq);
	if (size > ARRAY_SIZE(vs->rxmsgs))
		size = ARRAY_SIZE(vs->rxmsgs);
	for (i = 0; i < size; i++)
		vspmi_queue_rxmsg(vs, &vs->rxmsgs[i]);
	virtqueue_kick(vs->rxq);
	raw_spin_unlock_irqrestore(&vs->rxlock, flags);
}

static int pmic_arb_read_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			     u16 addr, u8 *buf, size_t len)
{
	struct spmi_pmic_arb *pa = spmi_controller_get_drvdata(ctrl);
	struct virtio_spmi *vs = pa->vs;
	struct virtio_spmi_msg *msg;
	u8 bc = len - 1;
	u32 data, cmd;
	int rc;
	unsigned long flags;

	if (bc >= PMIC_ARB_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev,
			"pmic-arb supports 1..%d bytes per trans, but:%zu requested\n",
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

	cmd = pa->ver_ops->fmt_cmd(opc, sid, addr, bc);
	raw_spin_lock_irqsave(&vs->txlock, flags);
	msg = vspmi_fill_txmsg(pa, VIO_SPMI_BUS_READ, cmd, 0, 0);
	rc = vspmi_pmic_arb_xfer(pa);
	if (rc)
		goto out;

	data = virtio32_to_cpu(vs->vdev,
			msg->payload.cmdd.data[0]);
	memcpy(buf, &data, (bc & 3) + 1);

	if (bc > 3) {
		data = virtio32_to_cpu(vs->vdev,
				msg->payload.cmdd.data[1]);
		memcpy((buf + 4), &data, ((bc - 4) & 3) + 1);
	}

out:
	raw_spin_unlock_irqrestore(&vs->txlock, flags);
	if (rc == EPERM) {
		/* spmi bus driver try to read disallowd gpio in probe
		 * give correct type and subtype and ignore other read command
		 */
		if ((addr & 0xff) == PMIC_GPIO_REG_TYPE && len == 1) {
			data = PMIC_GPIO_TYPE;
			memcpy(buf, &data, 1);
		}

		if ((addr & 0xff) == PMIC_GPIO_REG_SUBTYPE && len == 1) {
			data = PMIC_GPIO_SUBTYPE_GPIO_LV;
			memcpy(buf, &data, 1);
		}

		rc = 0;
	}
	return rc;
}

static int pmic_arb_write_cmd(struct spmi_controller *ctrl, u8 opc,
			    u8 sid, u16 addr, const u8 *buf, size_t len)
{
	struct spmi_pmic_arb *pa = spmi_controller_get_drvdata(ctrl);
	struct virtio_spmi *vs = pa->vs;
	struct virtio_spmi_msg *msg;
	u8 bc = len - 1;
	u32 data, cmd;
	int rc;
	unsigned long flags;

	if (bc >= PMIC_ARB_MAX_TRANS_BYTES) {
		dev_err(&ctrl->dev,
			"pmic-arb supports 1..%d bytes per trans, but:%zu requested\n",
			PMIC_ARB_MAX_TRANS_BYTES, len);
		return  -EINVAL;
	}

	/* Check the opcode */
	if (opc >= 0x40 && opc <= 0x5F)
		opc = PMIC_ARB_OP_WRITE;
	else if (opc <= 0x0F)
		opc = PMIC_ARB_OP_EXT_WRITE;
	else if (opc >= 0x30 && opc <= 0x37)
		opc = PMIC_ARB_OP_EXT_WRITEL;
	else if (opc >= 0x80)
		opc = PMIC_ARB_OP_ZERO_WRITE;
	else
		return -EINVAL;

	cmd = pa->ver_ops->fmt_cmd(opc, sid, addr, bc);
	raw_spin_lock_irqsave(&vs->txlock, flags);
	msg = vspmi_fill_txmsg(pa, VIO_SPMI_BUS_WRITE, cmd, 0, 0);

	memcpy(&data, buf, (bc & 3) + 1);
	msg->payload.cmdd.data[0] = cpu_to_virtio32(vs->vdev, data);
	if (bc > 3) {
		memcpy(&data, (buf + 4), ((bc - 4) & 3) + 1);
		msg->payload.cmdd.data[1] =
			cpu_to_virtio32(vs->vdev, data);
	}

	rc = vspmi_pmic_arb_xfer(pa);
	raw_spin_unlock_irqrestore(&vs->txlock, flags);
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
	struct spmi_pmic_arb *pa = irq_data_get_irq_chip_data(d);
	u8 sid = hwirq_to_sid(d->hwirq);
	u8 per = hwirq_to_per(d->hwirq);

	if (pmic_arb_write_cmd(pa->spmic, SPMI_CMD_EXT_WRITEL, sid,
			       (per << 8) + reg, buf, len))
		dev_err_ratelimited(&pa->spmic->dev,
				"failed irqchip transaction on %x\n", d->irq);
}

static void qpnpint_spmi_read(struct irq_data *d, u8 reg, void *buf, size_t len)
{
	struct spmi_pmic_arb *pa = irq_data_get_irq_chip_data(d);
	u8 sid = hwirq_to_sid(d->hwirq);
	u8 per = hwirq_to_per(d->hwirq);

	if (pmic_arb_read_cmd(pa->spmic, SPMI_CMD_EXT_READL, sid,
			      (per << 8) + reg, buf, len))
		dev_err_ratelimited(&pa->spmic->dev,
				"failed irqchip transaction on %x\n", d->irq);
}

static void periph_interrupt(struct spmi_pmic_arb *pa, u16 apid)
{
	unsigned int irq;
	u32 id = 0;
	u8 sid = (pa->apid_data[apid].ppid >> 8) & 0xF;
	u8 per = pa->apid_data[apid].ppid & 0xFF;

	irq = irq_find_mapping(pa->domain,
					spec_to_hwirq(sid, per, id, apid));
	generic_handle_irq(irq);
}

static void pmic_arb_chained_irq(struct virtio_spmi *vs,
		struct virtio_spmi_msg *msg)
{
	struct spmi_pmic_arb *pa = vs->pa;
	struct apid_data *apidd = pa->apid_data;

	u16 ppid = virtio16_to_cpu(vs->vdev, msg->payload.irqd.ppid);
	u16 apid = pa->ver_ops->ppid_to_apid(pa, ppid);

	struct irq_desc *desc = apidd[apid].desc;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);
	dev_dbg(&pa->spmic->dev,
			"Dispatching IRQ for apid=%x ppid=%x\n",
			apid, ppid);
	periph_interrupt(pa, apid);

	chained_irq_exit(chip, desc);
}

static void qpnpint_irq_ack(struct irq_data *d)
{
	struct spmi_pmic_arb *pa = irq_data_get_irq_chip_data(d);
	u8 irq = hwirq_to_irq(d->hwirq);
	u16 apid = hwirq_to_apid(d->hwirq);
	u16 ppid = pa->apid_data[apid].ppid;
	u8 data;
	unsigned long flags;

	raw_spin_lock_irqsave(&pa->vs->txlock, flags);
	vspmi_fill_txmsg(pa, VIO_IRQ_CLEAR, 0, ppid, BIT(irq));
	vspmi_pmic_arb_xfer(pa);
	raw_spin_unlock_irqrestore(&pa->vs->txlock, flags);

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
	struct spmi_pmic_arb *pa = irq_data_get_irq_chip_data(d);
	u8 irq = hwirq_to_irq(d->hwirq);
	u16 apid = hwirq_to_apid(d->hwirq);
	u16 ppid = pa->apid_data[apid].ppid;
	struct apid_data *apidd = pa->apid_data;
	u8 buf[2] = {0};
	unsigned long flags;

	apidd[apid].desc = irq_data_to_desc(d);

	raw_spin_lock_irqsave(&pa->vs->txlock, flags);
	vspmi_fill_txmsg(pa, VIO_ACC_ENABLE_WR, 0,
			ppid, SPMI_PIC_ACC_ENABLE_BIT);
	vspmi_pmic_arb_xfer(pa);
	raw_spin_unlock_irqrestore(&pa->vs->txlock, flags);

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
		else
			type.polarity_high &= ~BIT(irq);
		if (flow_type & IRQF_TRIGGER_FALLING)
			type.polarity_low  |= BIT(irq);
		else
			type.polarity_low  &= ~BIT(irq);

		flow_handler = handle_edge_irq;
	} else {
		if ((flow_type & (IRQF_TRIGGER_HIGH)) &&
		    (flow_type & (IRQF_TRIGGER_LOW)))
			return -EINVAL;

		type.type &= ~BIT(irq); /* level trig */
		if (flow_type & IRQF_TRIGGER_HIGH) {
			type.polarity_high |= BIT(irq);
			type.polarity_low  &= ~BIT(irq);
		} else {
			type.polarity_low  |= BIT(irq);
			type.polarity_high &= ~BIT(irq);
		}

		flow_handler = handle_level_irq;
	}

	qpnpint_spmi_write(d, QPNPINT_REG_SET_TYPE, &type, sizeof(type));
	irq_set_handler_locked(d, flow_handler);

	return 0;
}

static int qpnpint_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct spmi_pmic_arb *pa = irq_data_get_irq_chip_data(d);

	return irq_set_irq_wake(pa->irq, on);
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

static int qpnpint_irq_domain_activate(struct irq_domain *domain,
					struct irq_data *d, bool reserve)
{
	u8 irq = hwirq_to_irq(d->hwirq);
	u8 buf;

	buf = BIT(irq);
	qpnpint_spmi_write(d, QPNPINT_REG_EN_CLR, &buf, 1);
	qpnpint_spmi_write(d, QPNPINT_REG_LATCHED_CLR, &buf, 1);

	return 0;
}

static struct irq_chip pmic_arb_irqchip = {
	.name		= "pmic_arb",
	.irq_ack	= qpnpint_irq_ack,
	.irq_mask	= qpnpint_irq_mask,
	.irq_unmask	= qpnpint_irq_unmask,
	.irq_set_type	= qpnpint_irq_set_type,
	.irq_set_wake	= qpnpint_irq_set_wake,
	.irq_get_irqchip_state	= qpnpint_get_irqchip_state,
	.flags		= IRQCHIP_MASK_ON_SUSPEND,
};
static int qpnpint_irq_domain_translate(struct irq_domain *d,
					struct irq_fwspec *fwspec,
					unsigned long *out_hwirq,
					unsigned int *out_type)
{
	struct spmi_pmic_arb *pa = d->host_data;
	u32 *intspec = fwspec->param;
	u16 apid, ppid;
	int rc;

	dev_dbg(&pa->spmic->dev,
			"intspec[0] 0x%1x intspec[1] 0x%02x intspec[2] 0x%02x\n",
			intspec[0], intspec[1], intspec[2]);

	if (irq_domain_get_of_node(d) != (pa->vs->vdev->dev.parent)->of_node)
		return -EINVAL;
	if (fwspec->param_count != 4)
		return -EINVAL;
	if (intspec[0] > 0xF || intspec[1] > 0xFF || intspec[2] > 0x7)
		return -EINVAL;

	ppid = intspec[0] << 8 | intspec[1];
	rc = pa->ver_ops->ppid_to_apid(pa, ppid);
	if (rc < 0) {
		dev_err(&pa->spmic->dev,
				"failed to xlate sid = %#x, periph = %#x, irq = %u rc = %d\n",
				intspec[0], intspec[1], intspec[2], rc);
		return rc;
	}

	apid = rc;
	/* Keep track of {max,min}_apid for bounding search during interrupt */
	if (apid > pa->max_apid)
		pa->max_apid = apid;
	if (apid < pa->min_apid)
		pa->min_apid = apid;

	*out_hwirq = spec_to_hwirq(intspec[0], intspec[1], intspec[2], apid);
	*out_type  = intspec[3] & IRQ_TYPE_SENSE_MASK;

	dev_dbg(&pa->spmic->dev, "out_hwirq = %lu\n", *out_hwirq);

	return 0;
}

static struct lock_class_key qpnpint_irq_lock_class, qpnpint_irq_request_class;
static void qpnpint_irq_domain_map(struct spmi_pmic_arb *pa,
				struct irq_domain *domain, unsigned int virq,
				irq_hw_number_t hwirq, unsigned int type)
{
	irq_flow_handler_t handler;

	dev_dbg(&pa->spmic->dev, "virq = %u, hwirq = %lu, type = %u\n",
		virq, hwirq, type);

	if (type & IRQ_TYPE_EDGE_BOTH)
		handler = handle_edge_irq;
	else
		handler = handle_level_irq;

	irq_set_lockdep_class(virq, &qpnpint_irq_lock_class,
				&qpnpint_irq_request_class);
	irq_domain_set_info(domain, virq, hwirq, &pmic_arb_irqchip, pa,
				handler, NULL, NULL);
}

static int qpnpint_irq_domain_alloc(struct irq_domain *domain,
				unsigned int virq, unsigned int nr_irqs,
				void *data)
{
	struct spmi_pmic_arb *pa = domain->host_data;
	struct irq_fwspec *fwspec = data;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret, i;

	ret = qpnpint_irq_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++)
		qpnpint_irq_domain_map(pa, domain, virq + i, hwirq + i, type);

	return 0;
}

static int pmic_arb_read_apid_map_v5(struct spmi_pmic_arb *pa)
{
	struct virtio_spmi *vs = pa->vs;
	struct apid_data *apidd = pa->apid_data;
	u16 apid, ppid;
	u16 i;

	for (i = 0; i < VM_MAX_PERIPHS; i++) {
		ppid = vs->config.ppid_allowed[i];
		if (!ppid)
			break;

		apid = i;
		pa->ppid_to_apid[ppid] = apid | PMIC_ARB_APID_VALID;
		pa->apid_data[apid].ppid = ppid;
		pa->apid_data[apid].desc = NULL;
	}

	/* Dump the mapping table for debug purposes. */
	dev_dbg(&pa->spmic->dev, "PPID APID IRQ-DESC\n");
	for (ppid = 0; ppid < PMIC_ARB_MAX_PPID; ppid++) {
		apid = pa->ppid_to_apid[ppid];
		if (apid & PMIC_ARB_APID_VALID) {
			apid &= ~PMIC_ARB_APID_VALID;
			dev_dbg(&pa->spmic->dev, "%#03X %3u %llx\n",
			      ppid, apid, apidd[apid].desc);
		}
	}

	return 0;
}

static int pmic_arb_ppid_to_apid_v5(struct spmi_pmic_arb *pa, u16 ppid)
{
	if (!(pa->ppid_to_apid[ppid] & PMIC_ARB_APID_VALID))
		return -ENODEV;

	return pa->ppid_to_apid[ppid] & ~PMIC_ARB_APID_VALID;
}

static u32 pmic_arb_fmt_cmd_v1(u8 opc, u8 sid, u16 addr, u8 bc)
{
	return (opc << 27) | ((sid & 0xf) << 20) | (addr << 4) | (bc & 0x7);
}

static const struct pmic_arb_ver_ops pmic_arb_v5 = {
	.ver_str		= "v5",
	.ppid_to_apid	= pmic_arb_ppid_to_apid_v5,
	.fmt_cmd		= pmic_arb_fmt_cmd_v1,
};

static const struct irq_domain_ops pmic_arb_irq_domain_ops = {
	.activate = qpnpint_irq_domain_activate,
	.alloc = qpnpint_irq_domain_alloc,
	.free = irq_domain_free_irqs_common,
	.translate = qpnpint_irq_domain_translate,
};

static void viospmi_rx_isr(struct virtqueue *vq)
{
	struct virtio_spmi *vs = vq->vdev->priv;
	struct virtio_spmi_msg *msg;
	unsigned long flags;
	unsigned int len;

	raw_spin_lock_irqsave(&vs->rxlock, flags);
	while ((msg = virtqueue_get_buf(vs->rxq, &len)) != NULL) {
		raw_spin_unlock_irqrestore(&vs->rxlock, flags);
		pmic_arb_chained_irq(vs, msg);
		raw_spin_lock_irqsave(&vs->rxlock, flags);
		vspmi_queue_rxmsg(vs, msg);
	}

	virtqueue_kick(vs->rxq);
	raw_spin_unlock_irqrestore(&vs->rxlock, flags);
}

static int virtio_spmi_init_vqs(struct virtio_spmi *vspmi)
{
	struct virtqueue *vqs[2];
	vq_callback_t *cbs[] = { NULL, viospmi_rx_isr };
	static const char * const names[] = { "vs.tx", "vs.rx" };
	int rc;

	rc = virtio_find_vqs(vspmi->vdev, 2, vqs, cbs, names, NULL);
	if (rc)
		return rc;

	vspmi->txq = vqs[0];
	vspmi->rxq = vqs[1];

	return 0;
}

static void virtio_spmi_del_vqs(struct virtio_spmi *vspmi)
{
	vspmi->vdev->config->del_vqs(vspmi->vdev);
}

static int virtio_spmi_probe(struct virtio_device *vdev)
{
	struct virtio_spmi *vs;
	int i;
	int ret = 0;
	u32 val;

	struct spmi_pmic_arb *pa;
	struct spmi_controller *ctrl;
	int err;

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1))
		return -ENODEV;

	vs = devm_kzalloc(&vdev->dev, sizeof(*vs), GFP_KERNEL);
	if (!vs)
		return -ENOMEM;

	vdev->priv = vs;
	vs->vdev = vdev;
	raw_spin_lock_init(&vs->txlock);
	raw_spin_lock_init(&vs->rxlock);

	ret = virtio_spmi_init_vqs(vs);
	if (ret)
		goto err_init_vq;

	ctrl = spmi_controller_alloc(&vdev->dev, sizeof(*pa));
	if (!ctrl)
		return -ENOMEM;

	pa = vs->pa = spmi_controller_get_drvdata(ctrl);
	pa->spmic = ctrl;
	pa->vs = vs;

	pa->ver_ops = &pmic_arb_v5;
	dev_info(&ctrl->dev, "Virtio PMIC arbiter\n");

	pa->ppid_to_apid = devm_kcalloc(&ctrl->dev, PMIC_ARB_MAX_PPID,
					      sizeof(*pa->ppid_to_apid),
					      GFP_KERNEL);
	if (!pa->ppid_to_apid) {
		err = -ENOMEM;
		goto err_put_ctrl;
	}

	/* Initialize max_apid/min_apid to the opposite bounds, during
	 * the irq domain translation, we are sure to update these
	 */
	pa->max_apid = 0;
	pa->min_apid = PMIC_ARB_MAX_PERIPHS - 1;

	ctrl->read_cmd = pmic_arb_read_cmd;
	ctrl->write_cmd = pmic_arb_write_cmd;
	ctrl->dev.of_node = (vdev->dev.parent)->of_node->child;

	pa->irq = of_irq_get_byname((vdev->dev.parent)->of_node, "periph_irq");
	if (pa->irq < 0) {
		err = pa->irq;
		goto err_put_ctrl;
	}

	virtio_device_ready(vdev);
	vspmi_fill_rxmsgs(vs);

	memset(&vs->config, 0x0, sizeof(vs->config));

	for (i = 0; i < VM_MAX_PERIPHS; i += 2) {
		val = virtio_cread32(vdev,
			offsetof(struct virtio_spmi_config, ppid_allowed[i]));
		vs->config.ppid_allowed[i] = val & PMIC_ARB_PPID_MASK;
		vs->config.ppid_allowed[i + 1] =
					(val >> 16) & PMIC_ARB_PPID_MASK;
		if ((!vs->config.ppid_allowed[i]) ||
				!(vs->config.ppid_allowed[i + 1]))
			break;
	}

	err = pmic_arb_read_apid_map_v5(pa);
	if (err) {
		dev_err(&vdev->dev, "could not read APID->PPID mapping table, rc= %d\n",
			err);
		goto err_put_ctrl;
	}

	dev_dbg(&vdev->dev, "adding irq domain\n");
	pa->domain = irq_domain_add_tree((vdev->dev.parent)->of_node,
					 &pmic_arb_irq_domain_ops, pa);
	if (!pa->domain) {
		dev_err(&vdev->dev, "unable to create irq_domain\n");
		err = -ENOMEM;
		goto err_put_ctrl;
	}

	err = spmi_controller_add(ctrl);
	if (err)
		goto err_domain_remove;

	return 0;

err_domain_remove:
	irq_domain_remove(pa->domain);
err_put_ctrl:
	spmi_controller_put(ctrl);
	return err;

err_init_vq:
	virtio_spmi_del_vqs(vs);
	return ret;
}

static void virtio_spmi_remove(struct virtio_device *vdev)
{
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
}

static unsigned int features[] = {
	VIRTIO_SPMI_F_INT,
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_SPMI, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virtio_spmi_driver = {
	.feature_table		= features,
	.feature_table_size	= ARRAY_SIZE(features),
	.driver.name		= KBUILD_MODNAME,
	.driver.owner		= THIS_MODULE,
	.id_table		= id_table,
	.probe		= virtio_spmi_probe,
	.remove		= virtio_spmi_remove,
};

static int __init virtio_spmi_init(void)
{
	return register_virtio_driver(&virtio_spmi_driver);
}

static void __exit virtio_spmi_exit(void)
{
	unregister_virtio_driver(&virtio_spmi_driver);
}

subsys_initcall(virtio_spmi_init);
module_exit(virtio_spmi_exit);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("virtio spmi_pmic_arb frontend driver");
MODULE_LICENSE("GPL");
