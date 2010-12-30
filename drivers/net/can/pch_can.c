/*
 * Copyright (C) 1999 - 2010 Intel Corporation.
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
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

#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

#define MAX_MSG_OBJ		32
#define MSG_OBJ_RX		0 /* The receive message object flag. */
#define MSG_OBJ_TX		1 /* The transmit message object flag. */

#define ENABLE			1 /* The enable flag */
#define DISABLE			0 /* The disable flag */
#define CAN_CTRL_INIT		0x0001 /* The INIT bit of CANCONT register. */
#define CAN_CTRL_IE		0x0002 /* The IE bit of CAN control register */
#define CAN_CTRL_IE_SIE_EIE	0x000e
#define CAN_CTRL_CCE		0x0040
#define CAN_CTRL_OPT		0x0080 /* The OPT bit of CANCONT register. */
#define CAN_OPT_SILENT		0x0008 /* The Silent bit of CANOPT reg. */
#define CAN_OPT_LBACK		0x0010 /* The LoopBack bit of CANOPT reg. */
#define CAN_CMASK_RX_TX_SET	0x00f3
#define CAN_CMASK_RX_TX_GET	0x0073
#define CAN_CMASK_ALL		0xff
#define CAN_CMASK_RDWR		0x80
#define CAN_CMASK_ARB		0x20
#define CAN_CMASK_CTRL		0x10
#define CAN_CMASK_MASK		0x40
#define CAN_CMASK_NEWDAT	0x04
#define CAN_CMASK_CLRINTPND	0x08

#define CAN_IF_MCONT_NEWDAT	0x8000
#define CAN_IF_MCONT_INTPND	0x2000
#define CAN_IF_MCONT_UMASK	0x1000
#define CAN_IF_MCONT_TXIE	0x0800
#define CAN_IF_MCONT_RXIE	0x0400
#define CAN_IF_MCONT_RMTEN	0x0200
#define CAN_IF_MCONT_TXRQXT	0x0100
#define CAN_IF_MCONT_EOB	0x0080
#define CAN_IF_MCONT_DLC	0x000f
#define CAN_IF_MCONT_MSGLOST	0x4000
#define CAN_MASK2_MDIR_MXTD	0xc000
#define CAN_ID2_DIR		0x2000
#define CAN_ID_MSGVAL		0x8000

#define CAN_STATUS_INT		0x8000
#define CAN_IF_CREQ_BUSY	0x8000
#define CAN_ID2_XTD		0x4000

#define CAN_REC			0x00007f00
#define CAN_TEC			0x000000ff

#define PCH_RX_OK		0x00000010
#define PCH_TX_OK		0x00000008
#define PCH_BUS_OFF		0x00000080
#define PCH_EWARN		0x00000040
#define PCH_EPASSIV		0x00000020
#define PCH_LEC0		0x00000001
#define PCH_LEC1		0x00000002
#define PCH_LEC2		0x00000004
#define PCH_LEC_ALL		(PCH_LEC0 | PCH_LEC1 | PCH_LEC2)
#define PCH_STUF_ERR		PCH_LEC0
#define PCH_FORM_ERR		PCH_LEC1
#define PCH_ACK_ERR		(PCH_LEC0 | PCH_LEC1)
#define PCH_BIT1_ERR		PCH_LEC2
#define PCH_BIT0_ERR		(PCH_LEC0 | PCH_LEC2)
#define PCH_CRC_ERR		(PCH_LEC1 | PCH_LEC2)

/* bit position of certain controller bits. */
#define BIT_BITT_BRP		0
#define BIT_BITT_SJW		6
#define BIT_BITT_TSEG1		8
#define BIT_BITT_TSEG2		12
#define BIT_IF1_MCONT_RXIE	10
#define BIT_IF2_MCONT_TXIE	11
#define BIT_BRPE_BRPE		6
#define BIT_ES_TXERRCNT		0
#define BIT_ES_RXERRCNT		8
#define MSK_BITT_BRP		0x3f
#define MSK_BITT_SJW		0xc0
#define MSK_BITT_TSEG1		0xf00
#define MSK_BITT_TSEG2		0x7000
#define MSK_BRPE_BRPE		0x3c0
#define MSK_BRPE_GET		0x0f
#define MSK_CTRL_IE_SIE_EIE	0x07
#define MSK_MCONT_TXIE		0x08
#define MSK_MCONT_RXIE		0x10
#define PCH_CAN_NO_TX_BUFF	1
#define COUNTER_LIMIT		10

#define PCH_CAN_CLK		50000000	/* 50MHz */

/* Define the number of message object.
 * PCH CAN communications are done via Message RAM.
 * The Message RAM consists of 32 message objects. */
#define PCH_RX_OBJ_NUM		26  /* 1~ PCH_RX_OBJ_NUM is Rx*/
#define PCH_TX_OBJ_NUM		6  /* PCH_RX_OBJ_NUM is RX ~ Tx*/
#define PCH_OBJ_NUM		(PCH_TX_OBJ_NUM + PCH_RX_OBJ_NUM)

#define PCH_FIFO_THRESH		16

enum pch_can_mode {
	PCH_CAN_ENABLE,
	PCH_CAN_DISABLE,
	PCH_CAN_ALL,
	PCH_CAN_NONE,
	PCH_CAN_STOP,
	PCH_CAN_RUN
};

struct pch_can_regs {
	u32 cont;
	u32 stat;
	u32 errc;
	u32 bitt;
	u32 intr;
	u32 opt;
	u32 brpe;
	u32 reserve1;
	u32 if1_creq;
	u32 if1_cmask;
	u32 if1_mask1;
	u32 if1_mask2;
	u32 if1_id1;
	u32 if1_id2;
	u32 if1_mcont;
	u32 if1_dataa1;
	u32 if1_dataa2;
	u32 if1_datab1;
	u32 if1_datab2;
	u32 reserve2;
	u32 reserve3[12];
	u32 if2_creq;
	u32 if2_cmask;
	u32 if2_mask1;
	u32 if2_mask2;
	u32 if2_id1;
	u32 if2_id2;
	u32 if2_mcont;
	u32 if2_dataa1;
	u32 if2_dataa2;
	u32 if2_datab1;
	u32 if2_datab2;
	u32 reserve4;
	u32 reserve5[20];
	u32 treq1;
	u32 treq2;
	u32 reserve6[2];
	u32 reserve7[56];
	u32 reserve8[3];
	u32 srst;
};

struct pch_can_priv {
	struct can_priv can;
	unsigned int can_num;
	struct pci_dev *dev;
	unsigned int tx_enable[MAX_MSG_OBJ];
	unsigned int rx_enable[MAX_MSG_OBJ];
	unsigned int rx_link[MAX_MSG_OBJ];
	unsigned int int_enables;
	unsigned int int_stat;
	struct net_device *ndev;
	spinlock_t msgif_reg_lock; /* Message Interface Registers Access Lock*/
	unsigned int msg_obj[MAX_MSG_OBJ];
	struct pch_can_regs __iomem *regs;
	struct napi_struct napi;
	unsigned int tx_obj;	/* Point next Tx Obj index */
	unsigned int use_msi;
};

static struct can_bittiming_const pch_can_bittiming_const = {
	.name = KBUILD_MODNAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 1024, /* 6bit + extended 4bit */
	.brp_inc = 1,
};

static DEFINE_PCI_DEVICE_TABLE(pch_pci_tbl) = {
	{PCI_VENDOR_ID_INTEL, 0x8818, PCI_ANY_ID, PCI_ANY_ID,},
	{0,}
};
MODULE_DEVICE_TABLE(pci, pch_pci_tbl);

static inline void pch_can_bit_set(void __iomem *addr, u32 mask)
{
	iowrite32(ioread32(addr) | mask, addr);
}

static inline void pch_can_bit_clear(void __iomem *addr, u32 mask)
{
	iowrite32(ioread32(addr) & ~mask, addr);
}

static void pch_can_set_run_mode(struct pch_can_priv *priv,
				 enum pch_can_mode mode)
{
	switch (mode) {
	case PCH_CAN_RUN:
		pch_can_bit_clear(&priv->regs->cont, CAN_CTRL_INIT);
		break;

	case PCH_CAN_STOP:
		pch_can_bit_set(&priv->regs->cont, CAN_CTRL_INIT);
		break;

	default:
		dev_err(&priv->ndev->dev, "%s -> Invalid Mode.\n", __func__);
		break;
	}
}

static void pch_can_set_optmode(struct pch_can_priv *priv)
{
	u32 reg_val = ioread32(&priv->regs->opt);

	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		reg_val |= CAN_OPT_SILENT;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		reg_val |= CAN_OPT_LBACK;

	pch_can_bit_set(&priv->regs->cont, CAN_CTRL_OPT);
	iowrite32(reg_val, &priv->regs->opt);
}

static void pch_can_set_int_custom(struct pch_can_priv *priv)
{
	/* Clearing the IE, SIE and EIE bits of Can control register. */
	pch_can_bit_clear(&priv->regs->cont, CAN_CTRL_IE_SIE_EIE);

	/* Appropriately setting them. */
	pch_can_bit_set(&priv->regs->cont,
			((priv->int_enables & MSK_CTRL_IE_SIE_EIE) << 1));
}

/* This function retrieves interrupt enabled for the CAN device. */
static void pch_can_get_int_enables(struct pch_can_priv *priv, u32 *enables)
{
	/* Obtaining the status of IE, SIE and EIE interrupt bits. */
	*enables = ((ioread32(&priv->regs->cont) & CAN_CTRL_IE_SIE_EIE) >> 1);
}

static void pch_can_set_int_enables(struct pch_can_priv *priv,
				    enum pch_can_mode interrupt_no)
{
	switch (interrupt_no) {
	case PCH_CAN_ENABLE:
		pch_can_bit_set(&priv->regs->cont, CAN_CTRL_IE);
		break;

	case PCH_CAN_DISABLE:
		pch_can_bit_clear(&priv->regs->cont, CAN_CTRL_IE);
		break;

	case PCH_CAN_ALL:
		pch_can_bit_set(&priv->regs->cont, CAN_CTRL_IE_SIE_EIE);
		break;

	case PCH_CAN_NONE:
		pch_can_bit_clear(&priv->regs->cont, CAN_CTRL_IE_SIE_EIE);
		break;

	default:
		dev_err(&priv->ndev->dev, "Invalid interrupt number.\n");
		break;
	}
}

static void pch_can_check_if_busy(u32 __iomem *creq_addr, u32 num)
{
	u32 counter = COUNTER_LIMIT;
	u32 ifx_creq;

	iowrite32(num, creq_addr);
	while (counter) {
		ifx_creq = ioread32(creq_addr) & CAN_IF_CREQ_BUSY;
		if (!ifx_creq)
			break;
		counter--;
		udelay(1);
	}
	if (!counter)
		pr_err("%s:IF1 BUSY Flag is set forever.\n", __func__);
}

static void pch_can_set_rx_enable(struct pch_can_priv *priv, u32 buff_num,
				  u32 set)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->msgif_reg_lock, flags);
	/* Reading the receive buffer data from RAM to Interface1 registers */
	iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if1_cmask);
	pch_can_check_if_busy(&priv->regs->if1_creq, buff_num);

	/* Setting the IF1MASK1 register to access MsgVal and RxIE bits */
	iowrite32(CAN_CMASK_RDWR | CAN_CMASK_ARB | CAN_CMASK_CTRL,
		  &priv->regs->if1_cmask);

	if (set == ENABLE) {
		/* Setting the MsgVal and RxIE bits */
		pch_can_bit_set(&priv->regs->if1_mcont, CAN_IF_MCONT_RXIE);
		pch_can_bit_set(&priv->regs->if1_id2, CAN_ID_MSGVAL);

	} else if (set == DISABLE) {
		/* Resetting the MsgVal and RxIE bits */
		pch_can_bit_clear(&priv->regs->if1_mcont, CAN_IF_MCONT_RXIE);
		pch_can_bit_clear(&priv->regs->if1_id2, CAN_ID_MSGVAL);
	}

	pch_can_check_if_busy(&priv->regs->if1_creq, buff_num);
	spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
}

static void pch_can_rx_enable_all(struct pch_can_priv *priv)
{
	int i;

	/* Traversing to obtain the object configured as receivers. */
	for (i = 0; i < PCH_OBJ_NUM; i++) {
		if (priv->msg_obj[i] == MSG_OBJ_RX)
			pch_can_set_rx_enable(priv, i + 1, ENABLE);
	}
}

static void pch_can_rx_disable_all(struct pch_can_priv *priv)
{
	int i;

	/* Traversing to obtain the object configured as receivers. */
	for (i = 0; i < PCH_OBJ_NUM; i++) {
		if (priv->msg_obj[i] == MSG_OBJ_RX)
			pch_can_set_rx_enable(priv, i + 1, DISABLE);
	}
}

static void pch_can_set_tx_enable(struct pch_can_priv *priv, u32 buff_num,
				 u32 set)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->msgif_reg_lock, flags);
	/* Reading the Msg buffer from Message RAM to Interface2 registers. */
	iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if2_cmask);
	pch_can_check_if_busy(&priv->regs->if2_creq, buff_num);

	/* Setting the IF2CMASK register for accessing the
		MsgVal and TxIE bits */
	iowrite32(CAN_CMASK_RDWR | CAN_CMASK_ARB | CAN_CMASK_CTRL,
		 &priv->regs->if2_cmask);

	if (set == ENABLE) {
		/* Setting the MsgVal and TxIE bits */
		pch_can_bit_set(&priv->regs->if2_mcont, CAN_IF_MCONT_TXIE);
		pch_can_bit_set(&priv->regs->if2_id2, CAN_ID_MSGVAL);
	} else if (set == DISABLE) {
		/* Resetting the MsgVal and TxIE bits. */
		pch_can_bit_clear(&priv->regs->if2_mcont, CAN_IF_MCONT_TXIE);
		pch_can_bit_clear(&priv->regs->if2_id2, CAN_ID_MSGVAL);
	}

	pch_can_check_if_busy(&priv->regs->if2_creq, buff_num);
	spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
}

static void pch_can_tx_enable_all(struct pch_can_priv *priv)
{
	int i;

	/* Traversing to obtain the object configured as transmit object. */
	for (i = 0; i < PCH_OBJ_NUM; i++) {
		if (priv->msg_obj[i] == MSG_OBJ_TX)
			pch_can_set_tx_enable(priv, i + 1, ENABLE);
	}
}

static void pch_can_tx_disable_all(struct pch_can_priv *priv)
{
	int i;

	/* Traversing to obtain the object configured as transmit object. */
	for (i = 0; i < PCH_OBJ_NUM; i++) {
		if (priv->msg_obj[i] == MSG_OBJ_TX)
			pch_can_set_tx_enable(priv, i + 1, DISABLE);
	}
}

static void pch_can_get_rx_enable(struct pch_can_priv *priv, u32 buff_num,
				 u32 *enable)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->msgif_reg_lock, flags);
	iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if1_cmask);
	pch_can_check_if_busy(&priv->regs->if1_creq, buff_num);

	if (((ioread32(&priv->regs->if1_id2)) & CAN_ID_MSGVAL) &&
			((ioread32(&priv->regs->if1_mcont)) &
			CAN_IF_MCONT_RXIE))
		*enable = ENABLE;
	else
		*enable = DISABLE;
	spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
}

static void pch_can_get_tx_enable(struct pch_can_priv *priv, u32 buff_num,
				 u32 *enable)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->msgif_reg_lock, flags);
	iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if2_cmask);
	pch_can_check_if_busy(&priv->regs->if2_creq, buff_num);

	if (((ioread32(&priv->regs->if2_id2)) & CAN_ID_MSGVAL) &&
			((ioread32(&priv->regs->if2_mcont)) &
			CAN_IF_MCONT_TXIE)) {
		*enable = ENABLE;
	} else {
		*enable = DISABLE;
	}
	spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
}

static int pch_can_int_pending(struct pch_can_priv *priv)
{
	return ioread32(&priv->regs->intr) & 0xffff;
}

static void pch_can_set_rx_buffer_link(struct pch_can_priv *priv,
				       u32 buffer_num, u32 set)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->msgif_reg_lock, flags);
	iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if1_cmask);
	pch_can_check_if_busy(&priv->regs->if1_creq, buffer_num);
	iowrite32(CAN_CMASK_RDWR | CAN_CMASK_CTRL, &priv->regs->if1_cmask);
	if (set == ENABLE)
		pch_can_bit_clear(&priv->regs->if1_mcont, CAN_IF_MCONT_EOB);
	else
		pch_can_bit_set(&priv->regs->if1_mcont, CAN_IF_MCONT_EOB);

	pch_can_check_if_busy(&priv->regs->if1_creq, buffer_num);
	spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
}

static void pch_can_get_rx_buffer_link(struct pch_can_priv *priv,
				       u32 buffer_num, u32 *link)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->msgif_reg_lock, flags);
	iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if1_cmask);
	pch_can_check_if_busy(&priv->regs->if1_creq, buffer_num);

	if (ioread32(&priv->regs->if1_mcont) & CAN_IF_MCONT_EOB)
		*link = DISABLE;
	else
		*link = ENABLE;
	spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
}

static void pch_can_clear_buffers(struct pch_can_priv *priv)
{
	int i;

	for (i = 0; i < PCH_RX_OBJ_NUM; i++) {
		iowrite32(CAN_CMASK_RX_TX_SET, &priv->regs->if1_cmask);
		iowrite32(0xffff, &priv->regs->if1_mask1);
		iowrite32(0xffff, &priv->regs->if1_mask2);
		iowrite32(0x0, &priv->regs->if1_id1);
		iowrite32(0x0, &priv->regs->if1_id2);
		iowrite32(0x0, &priv->regs->if1_mcont);
		iowrite32(0x0, &priv->regs->if1_dataa1);
		iowrite32(0x0, &priv->regs->if1_dataa2);
		iowrite32(0x0, &priv->regs->if1_datab1);
		iowrite32(0x0, &priv->regs->if1_datab2);
		iowrite32(CAN_CMASK_RDWR | CAN_CMASK_MASK |
			  CAN_CMASK_ARB | CAN_CMASK_CTRL,
			  &priv->regs->if1_cmask);
		pch_can_check_if_busy(&priv->regs->if1_creq, i+1);
	}

	for (i = i;  i < PCH_OBJ_NUM; i++) {
		iowrite32(CAN_CMASK_RX_TX_SET, &priv->regs->if2_cmask);
		iowrite32(0xffff, &priv->regs->if2_mask1);
		iowrite32(0xffff, &priv->regs->if2_mask2);
		iowrite32(0x0, &priv->regs->if2_id1);
		iowrite32(0x0, &priv->regs->if2_id2);
		iowrite32(0x0, &priv->regs->if2_mcont);
		iowrite32(0x0, &priv->regs->if2_dataa1);
		iowrite32(0x0, &priv->regs->if2_dataa2);
		iowrite32(0x0, &priv->regs->if2_datab1);
		iowrite32(0x0, &priv->regs->if2_datab2);
		iowrite32(CAN_CMASK_RDWR | CAN_CMASK_MASK |
			  CAN_CMASK_ARB | CAN_CMASK_CTRL,
			  &priv->regs->if2_cmask);
		pch_can_check_if_busy(&priv->regs->if2_creq, i+1);
	}
}

static void pch_can_config_rx_tx_buffers(struct pch_can_priv *priv)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&priv->msgif_reg_lock, flags);

	for (i = 0; i < PCH_OBJ_NUM; i++) {
		if (priv->msg_obj[i] == MSG_OBJ_RX) {
			iowrite32(CAN_CMASK_RX_TX_GET,
				&priv->regs->if1_cmask);
			pch_can_check_if_busy(&priv->regs->if1_creq, i+1);

			iowrite32(0x0, &priv->regs->if1_id1);
			iowrite32(0x0, &priv->regs->if1_id2);

			pch_can_bit_set(&priv->regs->if1_mcont,
					CAN_IF_MCONT_UMASK);

			/* Set FIFO mode set to 0 except last Rx Obj*/
			pch_can_bit_clear(&priv->regs->if1_mcont,
					  CAN_IF_MCONT_EOB);
			/* In case FIFO mode, Last EoB of Rx Obj must be 1 */
			if (i == (PCH_RX_OBJ_NUM - 1))
				pch_can_bit_set(&priv->regs->if1_mcont,
						  CAN_IF_MCONT_EOB);

			iowrite32(0, &priv->regs->if1_mask1);
			pch_can_bit_clear(&priv->regs->if1_mask2,
					  0x1fff | CAN_MASK2_MDIR_MXTD);

			/* Setting CMASK for writing */
			iowrite32(CAN_CMASK_RDWR | CAN_CMASK_MASK |
				  CAN_CMASK_ARB | CAN_CMASK_CTRL,
				  &priv->regs->if1_cmask);

			pch_can_check_if_busy(&priv->regs->if1_creq, i+1);
		} else if (priv->msg_obj[i] == MSG_OBJ_TX) {
			iowrite32(CAN_CMASK_RX_TX_GET,
				&priv->regs->if2_cmask);
			pch_can_check_if_busy(&priv->regs->if2_creq, i+1);

			/* Resetting DIR bit for reception */
			iowrite32(0x0, &priv->regs->if2_id1);
			iowrite32(0x0, &priv->regs->if2_id2);
			pch_can_bit_set(&priv->regs->if2_id2, CAN_ID2_DIR);

			/* Setting EOB bit for transmitter */
			iowrite32(CAN_IF_MCONT_EOB, &priv->regs->if2_mcont);

			pch_can_bit_set(&priv->regs->if2_mcont,
					CAN_IF_MCONT_UMASK);

			iowrite32(0, &priv->regs->if2_mask1);
			pch_can_bit_clear(&priv->regs->if2_mask2, 0x1fff);

			/* Setting CMASK for writing */
			iowrite32(CAN_CMASK_RDWR | CAN_CMASK_MASK |
				  CAN_CMASK_ARB | CAN_CMASK_CTRL,
				  &priv->regs->if2_cmask);

			pch_can_check_if_busy(&priv->regs->if2_creq, i+1);
		}
	}
	spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
}

static void pch_can_init(struct pch_can_priv *priv)
{
	/* Stopping the Can device. */
	pch_can_set_run_mode(priv, PCH_CAN_STOP);

	/* Clearing all the message object buffers. */
	pch_can_clear_buffers(priv);

	/* Configuring the respective message object as either rx/tx object. */
	pch_can_config_rx_tx_buffers(priv);

	/* Enabling the interrupts. */
	pch_can_set_int_enables(priv, PCH_CAN_ALL);
}

static void pch_can_release(struct pch_can_priv *priv)
{
	/* Stooping the CAN device. */
	pch_can_set_run_mode(priv, PCH_CAN_STOP);

	/* Disabling the interrupts. */
	pch_can_set_int_enables(priv, PCH_CAN_NONE);

	/* Disabling all the receive object. */
	pch_can_rx_disable_all(priv);

	/* Disabling all the transmit object. */
	pch_can_tx_disable_all(priv);
}

/* This function clears interrupt(s) from the CAN device. */
static void pch_can_int_clr(struct pch_can_priv *priv, u32 mask)
{
	if (mask == CAN_STATUS_INT) {
		ioread32(&priv->regs->stat);
		return;
	}

	/* Clear interrupt for transmit object */
	if (priv->msg_obj[mask - 1] == MSG_OBJ_TX) {
		/* Setting CMASK for clearing interrupts for
					 frame transmission. */
		iowrite32(CAN_CMASK_RDWR | CAN_CMASK_CTRL | CAN_CMASK_ARB,
			  &priv->regs->if2_cmask);

		/* Resetting the ID registers. */
		pch_can_bit_set(&priv->regs->if2_id2,
			       CAN_ID2_DIR | (0x7ff << 2));
		iowrite32(0x0, &priv->regs->if2_id1);

		/* Claring NewDat, TxRqst & IntPnd */
		pch_can_bit_clear(&priv->regs->if2_mcont,
				  CAN_IF_MCONT_NEWDAT | CAN_IF_MCONT_INTPND |
				  CAN_IF_MCONT_TXRQXT);
		pch_can_check_if_busy(&priv->regs->if2_creq, mask);
	} else if (priv->msg_obj[mask - 1] == MSG_OBJ_RX) {
		/* Setting CMASK for clearing the reception interrupts. */
		iowrite32(CAN_CMASK_RDWR | CAN_CMASK_CTRL | CAN_CMASK_ARB,
			  &priv->regs->if1_cmask);

		/* Clearing the Dir bit. */
		pch_can_bit_clear(&priv->regs->if1_id2, CAN_ID2_DIR);

		/* Clearing NewDat & IntPnd */
		pch_can_bit_clear(&priv->regs->if1_mcont,
				  CAN_IF_MCONT_NEWDAT | CAN_IF_MCONT_INTPND);

		pch_can_check_if_busy(&priv->regs->if1_creq, mask);
	}
}

static int pch_can_get_buffer_status(struct pch_can_priv *priv)
{
	return (ioread32(&priv->regs->treq1) & 0xffff) |
	       ((ioread32(&priv->regs->treq2) & 0xffff) << 16);
}

static void pch_can_reset(struct pch_can_priv *priv)
{
	/* write to sw reset register */
	iowrite32(1, &priv->regs->srst);
	iowrite32(0, &priv->regs->srst);
}

static void pch_can_error(struct net_device *ndev, u32 status)
{
	struct sk_buff *skb;
	struct pch_can_priv *priv = netdev_priv(ndev);
	struct can_frame *cf;
	u32 errc;
	struct net_device_stats *stats = &(priv->ndev->stats);
	enum can_state state = priv->can.state;

	skb = alloc_can_err_skb(ndev, &cf);
	if (!skb)
		return;

	if (status & PCH_BUS_OFF) {
		pch_can_tx_disable_all(priv);
		pch_can_rx_disable_all(priv);
		state = CAN_STATE_BUS_OFF;
		cf->can_id |= CAN_ERR_BUSOFF;
		can_bus_off(ndev);
		pch_can_set_run_mode(priv, PCH_CAN_RUN);
		dev_err(&ndev->dev, "%s -> Bus Off occurres.\n", __func__);
	}

	/* Warning interrupt. */
	if (status & PCH_EWARN) {
		state = CAN_STATE_ERROR_WARNING;
		priv->can.can_stats.error_warning++;
		cf->can_id |= CAN_ERR_CRTL;
		errc = ioread32(&priv->regs->errc);
		if (((errc & CAN_REC) >> 8) > 96)
			cf->data[1] |= CAN_ERR_CRTL_RX_WARNING;
		if ((errc & CAN_TEC) > 96)
			cf->data[1] |= CAN_ERR_CRTL_TX_WARNING;
		dev_warn(&ndev->dev,
			"%s -> Error Counter is more than 96.\n", __func__);
	}
	/* Error passive interrupt. */
	if (status & PCH_EPASSIV) {
		priv->can.can_stats.error_passive++;
		state = CAN_STATE_ERROR_PASSIVE;
		cf->can_id |= CAN_ERR_CRTL;
		errc = ioread32(&priv->regs->errc);
		if (((errc & CAN_REC) >> 8) > 127)
			cf->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
		if ((errc & CAN_TEC) > 127)
			cf->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
		dev_err(&ndev->dev,
			"%s -> CAN controller is ERROR PASSIVE .\n", __func__);
	}

	if (status & PCH_LEC_ALL) {
		priv->can.can_stats.bus_error++;
		stats->rx_errors++;
		switch (status & PCH_LEC_ALL) {
		case PCH_STUF_ERR:
			cf->data[2] |= CAN_ERR_PROT_STUFF;
			break;
		case PCH_FORM_ERR:
			cf->data[2] |= CAN_ERR_PROT_FORM;
			break;
		case PCH_ACK_ERR:
			cf->data[2] |= CAN_ERR_PROT_LOC_ACK |
				       CAN_ERR_PROT_LOC_ACK_DEL;
			break;
		case PCH_BIT1_ERR:
		case PCH_BIT0_ERR:
			cf->data[2] |= CAN_ERR_PROT_BIT;
			break;
		case PCH_CRC_ERR:
			cf->data[2] |= CAN_ERR_PROT_LOC_CRC_SEQ |
				       CAN_ERR_PROT_LOC_CRC_DEL;
			break;
		default:
			iowrite32(status | PCH_LEC_ALL, &priv->regs->stat);
			break;
		}

	}

	priv->can.state = state;
	netif_rx(skb);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
}

static irqreturn_t pch_can_interrupt(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device *)dev_id;
	struct pch_can_priv *priv = netdev_priv(ndev);

	pch_can_set_int_enables(priv, PCH_CAN_NONE);

	napi_schedule(&priv->napi);

	return IRQ_HANDLED;
}

static int pch_can_rx_normal(struct net_device *ndev, u32 int_stat)
{
	u32 reg;
	canid_t id;
	u32 ide;
	u32 rtr;
	int i, j, k;
	int rcv_pkts = 0;
	struct sk_buff *skb;
	struct can_frame *cf;
	struct pch_can_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &(priv->ndev->stats);

	/* Reading the messsage object from the Message RAM */
	iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if1_cmask);
	pch_can_check_if_busy(&priv->regs->if1_creq, int_stat);

	/* Reading the MCONT register. */
	reg = ioread32(&priv->regs->if1_mcont);
	reg &= 0xffff;

	for (k = int_stat; !(reg & CAN_IF_MCONT_EOB); k++) {
		/* If MsgLost bit set. */
		if (reg & CAN_IF_MCONT_MSGLOST) {
			dev_err(&priv->ndev->dev, "Msg Obj is overwritten.\n");
			pch_can_bit_clear(&priv->regs->if1_mcont,
					  CAN_IF_MCONT_MSGLOST);
			iowrite32(CAN_CMASK_RDWR | CAN_CMASK_CTRL,
				  &priv->regs->if1_cmask);
			pch_can_check_if_busy(&priv->regs->if1_creq, k);

			skb = alloc_can_err_skb(ndev, &cf);
			if (!skb)
				return -ENOMEM;

			priv->can.can_stats.error_passive++;
			priv->can.state = CAN_STATE_ERROR_PASSIVE;
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
			cf->data[2] |= CAN_ERR_PROT_OVERLOAD;
			stats->rx_packets++;
			stats->rx_bytes += cf->can_dlc;

			netif_receive_skb(skb);
			rcv_pkts++;
			goto RX_NEXT;
		}
		if (!(reg & CAN_IF_MCONT_NEWDAT))
			goto RX_NEXT;

		skb = alloc_can_skb(priv->ndev, &cf);
		if (!skb)
			return -ENOMEM;

		/* Get Received data */
		ide = ((ioread32(&priv->regs->if1_id2)) & CAN_ID2_XTD) >> 14;
		if (ide) {
			id = (ioread32(&priv->regs->if1_id1) & 0xffff);
			id |= (((ioread32(&priv->regs->if1_id2)) &
					    0x1fff) << 16);
			cf->can_id = (id & CAN_EFF_MASK) | CAN_EFF_FLAG;
		} else {
			id = (((ioread32(&priv->regs->if1_id2)) &
					  (CAN_SFF_MASK << 2)) >> 2);
			cf->can_id = (id & CAN_SFF_MASK);
		}

		rtr = (ioread32(&priv->regs->if1_id2) &  CAN_ID2_DIR);
		if (rtr) {
			cf->can_dlc = 0;
			cf->can_id |= CAN_RTR_FLAG;
		} else {
			cf->can_dlc = ((ioread32(&priv->regs->if1_mcont)) &
						   0x0f);
		}

		for (i = 0, j = 0; i < cf->can_dlc; j++) {
			reg = ioread32(&priv->regs->if1_dataa1 + j*4);
			cf->data[i++] = cpu_to_le32(reg & 0xff);
			if (i == cf->can_dlc)
				break;
			cf->data[i++] = cpu_to_le32((reg >> 8) & 0xff);
		}

		netif_receive_skb(skb);
		rcv_pkts++;
		stats->rx_packets++;
		stats->rx_bytes += cf->can_dlc;

		if (k < PCH_FIFO_THRESH) {
			iowrite32(CAN_CMASK_RDWR | CAN_CMASK_CTRL |
				  CAN_CMASK_ARB, &priv->regs->if1_cmask);

			/* Clearing the Dir bit. */
			pch_can_bit_clear(&priv->regs->if1_id2, CAN_ID2_DIR);

			/* Clearing NewDat & IntPnd */
			pch_can_bit_clear(&priv->regs->if1_mcont,
					  CAN_IF_MCONT_INTPND);
			pch_can_check_if_busy(&priv->regs->if1_creq, k);
		} else if (k > PCH_FIFO_THRESH) {
			pch_can_int_clr(priv, k);
		} else if (k == PCH_FIFO_THRESH) {
			int cnt;
			for (cnt = 0; cnt < PCH_FIFO_THRESH; cnt++)
				pch_can_int_clr(priv, cnt+1);
		}
RX_NEXT:
		/* Reading the messsage object from the Message RAM */
		iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if1_cmask);
		pch_can_check_if_busy(&priv->regs->if1_creq, k + 1);
		reg = ioread32(&priv->regs->if1_mcont);
	}

	return rcv_pkts;
}
static int pch_can_rx_poll(struct napi_struct *napi, int quota)
{
	struct net_device *ndev = napi->dev;
	struct pch_can_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &(priv->ndev->stats);
	u32 dlc;
	u32 int_stat;
	int rcv_pkts = 0;
	u32 reg_stat;
	unsigned long flags;

	int_stat = pch_can_int_pending(priv);
	if (!int_stat)
		return 0;

INT_STAT:
	if (int_stat == CAN_STATUS_INT) {
		reg_stat = ioread32(&priv->regs->stat);
		if (reg_stat & (PCH_BUS_OFF | PCH_LEC_ALL)) {
			if ((reg_stat & PCH_LEC_ALL) != PCH_LEC_ALL)
				pch_can_error(ndev, reg_stat);
		}

		if (reg_stat & PCH_TX_OK) {
			spin_lock_irqsave(&priv->msgif_reg_lock, flags);
			iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if2_cmask);
			pch_can_check_if_busy(&priv->regs->if2_creq,
					       ioread32(&priv->regs->intr));
			spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
			pch_can_bit_clear(&priv->regs->stat, PCH_TX_OK);
		}

		if (reg_stat & PCH_RX_OK)
			pch_can_bit_clear(&priv->regs->stat, PCH_RX_OK);

		int_stat = pch_can_int_pending(priv);
		if (int_stat == CAN_STATUS_INT)
			goto INT_STAT;
	}

MSG_OBJ:
	if ((int_stat >= 1) && (int_stat <= PCH_RX_OBJ_NUM)) {
		spin_lock_irqsave(&priv->msgif_reg_lock, flags);
		rcv_pkts = pch_can_rx_normal(ndev, int_stat);
		spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
		if (rcv_pkts < 0)
			return 0;
	} else if ((int_stat > PCH_RX_OBJ_NUM) && (int_stat <= PCH_OBJ_NUM)) {
		if (priv->msg_obj[int_stat - 1] == MSG_OBJ_TX) {
			/* Handle transmission interrupt */
			can_get_echo_skb(ndev, int_stat - PCH_RX_OBJ_NUM - 1);
			spin_lock_irqsave(&priv->msgif_reg_lock, flags);
			iowrite32(CAN_CMASK_RX_TX_GET | CAN_CMASK_CLRINTPND,
				  &priv->regs->if2_cmask);
			dlc = ioread32(&priv->regs->if2_mcont) &
				       CAN_IF_MCONT_DLC;
			pch_can_check_if_busy(&priv->regs->if2_creq, int_stat);
			spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);
			if (dlc > 8)
				dlc = 8;
			stats->tx_bytes += dlc;
			stats->tx_packets++;
		}
	}

	int_stat = pch_can_int_pending(priv);
	if (int_stat == CAN_STATUS_INT)
		goto INT_STAT;
	else if (int_stat >= 1 && int_stat <= 32)
		goto MSG_OBJ;

	napi_complete(napi);
	pch_can_set_int_enables(priv, PCH_CAN_ALL);

	return rcv_pkts;
}

static int pch_set_bittiming(struct net_device *ndev)
{
	struct pch_can_priv *priv = netdev_priv(ndev);
	const struct can_bittiming *bt = &priv->can.bittiming;
	u32 canbit;
	u32 bepe;
	u32 brp;

	/* Setting the CCE bit for accessing the Can Timing register. */
	pch_can_bit_set(&priv->regs->cont, CAN_CTRL_CCE);

	brp = (bt->tq) / (1000000000/PCH_CAN_CLK) - 1;
	canbit = brp & MSK_BITT_BRP;
	canbit |= (bt->sjw - 1) << BIT_BITT_SJW;
	canbit |= (bt->phase_seg1 + bt->prop_seg - 1) << BIT_BITT_TSEG1;
	canbit |= (bt->phase_seg2 - 1) << BIT_BITT_TSEG2;
	bepe = (brp & MSK_BRPE_BRPE) >> BIT_BRPE_BRPE;
	iowrite32(canbit, &priv->regs->bitt);
	iowrite32(bepe, &priv->regs->brpe);
	pch_can_bit_clear(&priv->regs->cont, CAN_CTRL_CCE);

	return 0;
}

static void pch_can_start(struct net_device *ndev)
{
	struct pch_can_priv *priv = netdev_priv(ndev);

	if (priv->can.state != CAN_STATE_STOPPED)
		pch_can_reset(priv);

	pch_set_bittiming(ndev);
	pch_can_set_optmode(priv);

	pch_can_tx_enable_all(priv);
	pch_can_rx_enable_all(priv);

	/* Setting the CAN to run mode. */
	pch_can_set_run_mode(priv, PCH_CAN_RUN);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return;
}

static int pch_can_do_set_mode(struct net_device *ndev, enum can_mode mode)
{
	int ret = 0;

	switch (mode) {
	case CAN_MODE_START:
		pch_can_start(ndev);
		netif_wake_queue(ndev);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int pch_can_open(struct net_device *ndev)
{
	struct pch_can_priv *priv = netdev_priv(ndev);
	int retval;

	retval = pci_enable_msi(priv->dev);
	if (retval) {
		dev_info(&ndev->dev, "PCH CAN opened without MSI\n");
		priv->use_msi = 0;
	} else {
		dev_info(&ndev->dev, "PCH CAN opened with MSI\n");
		priv->use_msi = 1;
	}

	/* Regsitering the interrupt. */
	retval = request_irq(priv->dev->irq, pch_can_interrupt, IRQF_SHARED,
			     ndev->name, ndev);
	if (retval) {
		dev_err(&ndev->dev, "request_irq failed.\n");
		goto req_irq_err;
	}

	/* Open common can device */
	retval = open_candev(ndev);
	if (retval) {
		dev_err(ndev->dev.parent, "open_candev() failed %d\n", retval);
		goto err_open_candev;
	}

	pch_can_init(priv);
	pch_can_start(ndev);
	napi_enable(&priv->napi);
	netif_start_queue(ndev);

	return 0;

err_open_candev:
	free_irq(priv->dev->irq, ndev);
req_irq_err:
	if (priv->use_msi)
		pci_disable_msi(priv->dev);

	pch_can_release(priv);

	return retval;
}

static int pch_close(struct net_device *ndev)
{
	struct pch_can_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	pch_can_release(priv);
	free_irq(priv->dev->irq, ndev);
	if (priv->use_msi)
		pci_disable_msi(priv->dev);
	close_candev(ndev);
	priv->can.state = CAN_STATE_STOPPED;
	return 0;
}

static int pch_get_msg_obj_sts(struct net_device *ndev, u32 obj_id)
{
	u32 buffer_status = 0;
	struct pch_can_priv *priv = netdev_priv(ndev);

	/* Getting the message object status. */
	buffer_status = (u32) pch_can_get_buffer_status(priv);

	return buffer_status & obj_id;
}


static netdev_tx_t pch_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	int i, j;
	unsigned long flags;
	struct pch_can_priv *priv = netdev_priv(ndev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	int tx_buffer_avail = 0;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (priv->tx_obj == (PCH_OBJ_NUM + 1)) { /* Point tail Obj */
		while (pch_get_msg_obj_sts(ndev, (((1 << PCH_TX_OBJ_NUM)-1) <<
					   PCH_RX_OBJ_NUM)))
			udelay(500);

		priv->tx_obj = PCH_RX_OBJ_NUM + 1; /* Point head of Tx Obj ID */
		tx_buffer_avail = priv->tx_obj; /* Point Tail of Tx Obj */
	} else {
		tx_buffer_avail = priv->tx_obj;
	}
	priv->tx_obj++;

	/* Attaining the lock. */
	spin_lock_irqsave(&priv->msgif_reg_lock, flags);

	/* Reading the Msg Obj from the Msg RAM to the Interface register. */
	iowrite32(CAN_CMASK_RX_TX_GET, &priv->regs->if2_cmask);
	pch_can_check_if_busy(&priv->regs->if2_creq, tx_buffer_avail);

	/* Setting the CMASK register. */
	pch_can_bit_set(&priv->regs->if2_cmask, CAN_CMASK_ALL);

	/* If ID extended is set. */
	pch_can_bit_clear(&priv->regs->if2_id1, 0xffff);
	pch_can_bit_clear(&priv->regs->if2_id2, 0x1fff | CAN_ID2_XTD);
	if (cf->can_id & CAN_EFF_FLAG) {
		pch_can_bit_set(&priv->regs->if2_id1, cf->can_id & 0xffff);
		pch_can_bit_set(&priv->regs->if2_id2,
				((cf->can_id >> 16) & 0x1fff) | CAN_ID2_XTD);
	} else {
		pch_can_bit_set(&priv->regs->if2_id1, 0);
		pch_can_bit_set(&priv->regs->if2_id2,
				(cf->can_id & CAN_SFF_MASK) << 2);
	}

	/* If remote frame has to be transmitted.. */
	if (cf->can_id & CAN_RTR_FLAG)
		pch_can_bit_clear(&priv->regs->if2_id2, CAN_ID2_DIR);

	for (i = 0, j = 0; i < cf->can_dlc; j++) {
		iowrite32(le32_to_cpu(cf->data[i++]),
			 (&priv->regs->if2_dataa1) + j*4);
		if (i == cf->can_dlc)
			break;
		iowrite32(le32_to_cpu(cf->data[i++] << 8),
			 (&priv->regs->if2_dataa1) + j*4);
	}

	can_put_echo_skb(skb, ndev, tx_buffer_avail - PCH_RX_OBJ_NUM - 1);

	/* Updating the size of the data. */
	pch_can_bit_clear(&priv->regs->if2_mcont, 0x0f);
	pch_can_bit_set(&priv->regs->if2_mcont, cf->can_dlc);

	/* Clearing IntPend, NewDat & TxRqst */
	pch_can_bit_clear(&priv->regs->if2_mcont,
			  CAN_IF_MCONT_NEWDAT | CAN_IF_MCONT_INTPND |
			  CAN_IF_MCONT_TXRQXT);

	/* Setting NewDat, TxRqst bits */
	pch_can_bit_set(&priv->regs->if2_mcont,
			CAN_IF_MCONT_NEWDAT | CAN_IF_MCONT_TXRQXT);

	pch_can_check_if_busy(&priv->regs->if2_creq, tx_buffer_avail);

	spin_unlock_irqrestore(&priv->msgif_reg_lock, flags);

	return NETDEV_TX_OK;
}

static const struct net_device_ops pch_can_netdev_ops = {
	.ndo_open		= pch_can_open,
	.ndo_stop		= pch_close,
	.ndo_start_xmit		= pch_xmit,
};

static void __devexit pch_can_remove(struct pci_dev *pdev)
{
	struct net_device *ndev = pci_get_drvdata(pdev);
	struct pch_can_priv *priv = netdev_priv(ndev);

	unregister_candev(priv->ndev);
	free_candev(priv->ndev);
	pci_iounmap(pdev, priv->regs);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	pch_can_reset(priv);
}

#ifdef CONFIG_PM
static int pch_can_suspend(struct pci_dev *pdev, pm_message_t state)
{
	int i;			/* Counter variable. */
	int retval;		/* Return value. */
	u32 buf_stat;	/* Variable for reading the transmit buffer status. */
	u32 counter = 0xFFFFFF;

	struct net_device *dev = pci_get_drvdata(pdev);
	struct pch_can_priv *priv = netdev_priv(dev);

	/* Stop the CAN controller */
	pch_can_set_run_mode(priv, PCH_CAN_STOP);

	/* Indicate that we are aboutto/in suspend */
	priv->can.state = CAN_STATE_SLEEPING;

	/* Waiting for all transmission to complete. */
	while (counter) {
		buf_stat = pch_can_get_buffer_status(priv);
		if (!buf_stat)
			break;
		counter--;
		udelay(1);
	}
	if (!counter)
		dev_err(&pdev->dev, "%s -> Transmission time out.\n", __func__);

	/* Save interrupt configuration and then disable them */
	pch_can_get_int_enables(priv, &(priv->int_enables));
	pch_can_set_int_enables(priv, PCH_CAN_DISABLE);

	/* Save Tx buffer enable state */
	for (i = 0; i < PCH_OBJ_NUM; i++) {
		if (priv->msg_obj[i] == MSG_OBJ_TX)
			pch_can_get_tx_enable(priv, i + 1,
					      &(priv->tx_enable[i]));
	}

	/* Disable all Transmit buffers */
	pch_can_tx_disable_all(priv);

	/* Save Rx buffer enable state */
	for (i = 0; i < PCH_OBJ_NUM; i++) {
		if (priv->msg_obj[i] == MSG_OBJ_RX) {
			pch_can_get_rx_enable(priv, i + 1,
						&(priv->rx_enable[i]));
			pch_can_get_rx_buffer_link(priv, i + 1,
						&(priv->rx_link[i]));
		}
	}

	/* Disable all Receive buffers */
	pch_can_rx_disable_all(priv);
	retval = pci_save_state(pdev);
	if (retval) {
		dev_err(&pdev->dev, "pci_save_state failed.\n");
	} else {
		pci_enable_wake(pdev, PCI_D3hot, 0);
		pci_disable_device(pdev);
		pci_set_power_state(pdev, pci_choose_state(pdev, state));
	}

	return retval;
}

static int pch_can_resume(struct pci_dev *pdev)
{
	int i;			/* Counter variable. */
	int retval;		/* Return variable. */
	struct net_device *dev = pci_get_drvdata(pdev);
	struct pch_can_priv *priv = netdev_priv(dev);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "pci_enable_device failed.\n");
		return retval;
	}

	pci_enable_wake(pdev, PCI_D3hot, 0);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	/* Disabling all interrupts. */
	pch_can_set_int_enables(priv, PCH_CAN_DISABLE);

	/* Setting the CAN device in Stop Mode. */
	pch_can_set_run_mode(priv, PCH_CAN_STOP);

	/* Configuring the transmit and receive buffers. */
	pch_can_config_rx_tx_buffers(priv);

	/* Restore the CAN state */
	pch_set_bittiming(dev);

	/* Listen/Active */
	pch_can_set_optmode(priv);

	/* Enabling the transmit buffer. */
	for (i = 0; i < PCH_OBJ_NUM; i++) {
		if (priv->msg_obj[i] == MSG_OBJ_TX) {
			pch_can_set_tx_enable(priv, i + 1,
					      priv->tx_enable[i]);
		}
	}

	/* Configuring the receive buffer and enabling them. */
	for (i = 0; i < PCH_OBJ_NUM; i++) {
		if (priv->msg_obj[i] == MSG_OBJ_RX) {
			/* Restore buffer link */
			pch_can_set_rx_buffer_link(priv, i + 1,
						   priv->rx_link[i]);

			/* Restore buffer enables */
			pch_can_set_rx_enable(priv, i + 1, priv->rx_enable[i]);
		}
	}

	/* Enable CAN Interrupts */
	pch_can_set_int_custom(priv);

	/* Restore Run Mode */
	pch_can_set_run_mode(priv, PCH_CAN_RUN);

	return retval;
}
#else
#define pch_can_suspend NULL
#define pch_can_resume NULL
#endif

static int pch_can_get_berr_counter(const struct net_device *dev,
				    struct can_berr_counter *bec)
{
	struct pch_can_priv *priv = netdev_priv(dev);

	bec->txerr = ioread32(&priv->regs->errc) & CAN_TEC;
	bec->rxerr = (ioread32(&priv->regs->errc) & CAN_REC) >> 8;

	return 0;
}

static int __devinit pch_can_probe(struct pci_dev *pdev,
				   const struct pci_device_id *id)
{
	struct net_device *ndev;
	struct pch_can_priv *priv;
	int rc;
	int index;
	void __iomem *addr;

	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "Failed pci_enable_device %d\n", rc);
		goto probe_exit_endev;
	}

	rc = pci_request_regions(pdev, KBUILD_MODNAME);
	if (rc) {
		dev_err(&pdev->dev, "Failed pci_request_regions %d\n", rc);
		goto probe_exit_pcireq;
	}

	addr = pci_iomap(pdev, 1, 0);
	if (!addr) {
		rc = -EIO;
		dev_err(&pdev->dev, "Failed pci_iomap\n");
		goto probe_exit_ipmap;
	}

	ndev = alloc_candev(sizeof(struct pch_can_priv), PCH_TX_OBJ_NUM);
	if (!ndev) {
		rc = -ENOMEM;
		dev_err(&pdev->dev, "Failed alloc_candev\n");
		goto probe_exit_alloc_candev;
	}

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	priv->regs = addr;
	priv->dev = pdev;
	priv->can.bittiming_const = &pch_can_bittiming_const;
	priv->can.do_set_mode = pch_can_do_set_mode;
	priv->can.do_get_berr_counter = pch_can_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY |
				       CAN_CTRLMODE_LOOPBACK;
	priv->tx_obj = PCH_RX_OBJ_NUM + 1; /* Point head of Tx Obj */

	ndev->irq = pdev->irq;
	ndev->flags |= IFF_ECHO;

	pci_set_drvdata(pdev, ndev);
	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &pch_can_netdev_ops;

	priv->can.clock.freq = PCH_CAN_CLK; /* Hz */
	for (index = 0; index < PCH_RX_OBJ_NUM;)
		priv->msg_obj[index++] = MSG_OBJ_RX;

	for (index = index;  index < PCH_OBJ_NUM;)
		priv->msg_obj[index++] = MSG_OBJ_TX;

	netif_napi_add(ndev, &priv->napi, pch_can_rx_poll, PCH_RX_OBJ_NUM);

	rc = register_candev(ndev);
	if (rc) {
		dev_err(&pdev->dev, "Failed register_candev %d\n", rc);
		goto probe_exit_reg_candev;
	}

	return 0;

probe_exit_reg_candev:
	free_candev(ndev);
probe_exit_alloc_candev:
	pci_iounmap(pdev, addr);
probe_exit_ipmap:
	pci_release_regions(pdev);
probe_exit_pcireq:
	pci_disable_device(pdev);
probe_exit_endev:
	return rc;
}

static struct pci_driver pch_can_pci_driver = {
	.name = "pch_can",
	.id_table = pch_pci_tbl,
	.probe = pch_can_probe,
	.remove = __devexit_p(pch_can_remove),
	.suspend = pch_can_suspend,
	.resume = pch_can_resume,
};

static int __init pch_can_pci_init(void)
{
	return pci_register_driver(&pch_can_pci_driver);
}
module_init(pch_can_pci_init);

static void __exit pch_can_pci_exit(void)
{
	pci_unregister_driver(&pch_can_pci_driver);
}
module_exit(pch_can_pci_exit);

MODULE_DESCRIPTION("Controller Area Network Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.94");
