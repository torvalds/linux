#include <linux/delay.h>
#include <linux/etherdevice.h>
#include "phy.h"
#include "sta.h"
#include "debug.h"

void hash_read(struct agnx_priv *priv, u32 reghi, u32 reglo, u8 sta_id)
{
	void __iomem *ctl = priv->ctl;

	reglo &= 0xFFFF;
	reglo |= 0x30000000;
	reglo |= 0x40000000;	/* Set status busy */
	reglo |= sta_id << 16;

	iowrite32(0, ctl + AGNX_RXM_HASH_CMD_FLAG);
	iowrite32(reghi, ctl + AGNX_RXM_HASH_CMD_HIGH);
	iowrite32(reglo, ctl + AGNX_RXM_HASH_CMD_LOW);

	reghi = ioread32(ctl + AGNX_RXM_HASH_CMD_HIGH);
	reglo = ioread32(ctl + AGNX_RXM_HASH_CMD_LOW);
	printk(PFX "RX hash cmd are : %.8x%.8x\n", reghi, reglo);
}

void hash_write(struct agnx_priv *priv, u8 *mac_addr, u8 sta_id)
{
	void __iomem *ctl = priv->ctl;
	u32 reghi, reglo;

	if (!is_valid_ether_addr(mac_addr))
		printk(KERN_WARNING PFX "Update hash table: Invalid hwaddr!\n");

	reghi = mac_addr[0] << 24 | mac_addr[1] << 16 | mac_addr[2] << 8 | mac_addr[3];
	reglo = mac_addr[4] << 8 | mac_addr[5];
	reglo |= 0x10000000;	/* Set hash commmand */
	reglo |= 0x40000000;	/* Set status busy */
	reglo |= sta_id << 16;

	iowrite32(0, ctl + AGNX_RXM_HASH_CMD_FLAG);
	iowrite32(reghi, ctl + AGNX_RXM_HASH_CMD_HIGH);
	iowrite32(reglo, ctl + AGNX_RXM_HASH_CMD_LOW);

	reglo = ioread32(ctl + AGNX_RXM_HASH_CMD_LOW);
	if (!(reglo & 0x80000000))
		printk(KERN_WARNING PFX "Update hash table failed\n");
}

void hash_delete(struct agnx_priv *priv, u32 reghi, u32 reglo, u8 sta_id)
{
	void __iomem *ctl = priv->ctl;

	reglo &= 0xFFFF;
	reglo |= 0x20000000;
	reglo |= 0x40000000;	/* Set status busy */
	reglo |= sta_id << 16;

	iowrite32(0, ctl + AGNX_RXM_HASH_CMD_FLAG);
	iowrite32(reghi, ctl + AGNX_RXM_HASH_CMD_HIGH);
	iowrite32(reglo, ctl + AGNX_RXM_HASH_CMD_LOW);
	reghi = ioread32(ctl + AGNX_RXM_HASH_CMD_HIGH);

	reglo = ioread32(ctl + AGNX_RXM_HASH_CMD_LOW);
	printk(PFX "RX hash cmd are : %.8x%.8x\n", reghi, reglo);

}

void hash_dump(struct agnx_priv *priv, u8 sta_id)
{
	void __iomem *ctl = priv->ctl;
	u32 reghi, reglo;

	reglo = 0x40000000;  	/* status bit */
	iowrite32(reglo, ctl + AGNX_RXM_HASH_CMD_LOW);
	iowrite32(sta_id << 16, ctl + AGNX_RXM_HASH_DUMP_DATA);

	udelay(80);

	reghi = ioread32(ctl + AGNX_RXM_HASH_CMD_HIGH);
	reglo = ioread32(ctl + AGNX_RXM_HASH_CMD_LOW);
	printk(PFX "hash cmd are : %.8x%.8x\n", reghi, reglo);
	reghi = ioread32(ctl + AGNX_RXM_HASH_CMD_FLAG);
	printk(PFX "hash flag is : %.8x\n", reghi);
	reghi = ioread32(ctl + AGNX_RXM_HASH_DUMP_MST);
	reglo = ioread32(ctl + AGNX_RXM_HASH_DUMP_LST);
	printk(PFX "hash dump mst lst: %.8x%.8x\n", reghi, reglo);
	reghi = ioread32(ctl + AGNX_RXM_HASH_DUMP_DATA);
	printk(PFX "hash dump data: %.8x\n", reghi);
}

void get_sta_power(struct agnx_priv *priv, struct agnx_sta_power *power, unsigned int sta_idx)
{
	void __iomem *ctl = priv->ctl;
	memcpy_fromio(power, ctl + AGNX_TXM_STAPOWTEMP + sizeof(*power) * sta_idx,
		      sizeof(*power));
}

inline void
set_sta_power(struct agnx_priv *priv, struct agnx_sta_power *power, unsigned int sta_idx)
{
	void __iomem *ctl = priv->ctl;
	/* FIXME   2. Write Template to offset + station number  */
	memcpy_toio(ctl + AGNX_TXM_STAPOWTEMP + sizeof(*power) * sta_idx,
		    power, sizeof(*power));
}


void get_sta_tx_wq(struct agnx_priv *priv, struct agnx_sta_tx_wq *tx_wq,
		   unsigned int sta_idx, unsigned int wq_idx)
{
	void __iomem *data = priv->data;
	memcpy_fromio(tx_wq, data + AGNX_PDU_TX_WQ + sizeof(*tx_wq) * STA_TX_WQ_NUM * sta_idx +
		      sizeof(*tx_wq) * wq_idx,  sizeof(*tx_wq));

}

inline void set_sta_tx_wq(struct agnx_priv *priv, struct agnx_sta_tx_wq *tx_wq,
		   unsigned int sta_idx, unsigned int wq_idx)
{
	void __iomem *data = priv->data;
	memcpy_toio(data + AGNX_PDU_TX_WQ + sizeof(*tx_wq) * STA_TX_WQ_NUM * sta_idx +
		    sizeof(*tx_wq) * wq_idx, tx_wq, sizeof(*tx_wq));
}


void get_sta(struct agnx_priv *priv, struct agnx_sta *sta, unsigned int sta_idx)
{
	void __iomem *data = priv->data;

	memcpy_fromio(sta, data + AGNX_PDUPOOL + sizeof(*sta) * sta_idx,
		      sizeof(*sta));
}

inline void set_sta(struct agnx_priv *priv, struct agnx_sta *sta, unsigned int sta_idx)
{
	void __iomem *data = priv->data;

	memcpy_toio(data + AGNX_PDUPOOL + sizeof(*sta) * sta_idx,
		    sta, sizeof(*sta));
}

/* FIXME */
void sta_power_init(struct agnx_priv *priv, unsigned int sta_idx)
{
	struct agnx_sta_power power;
	u32 reg;
	AGNX_TRACE;

	memset(&power, 0, sizeof(power));
	reg = agnx_set_bits(EDCF, EDCF_SHIFT, 0x1);
	power.reg = cpu_to_le32(reg);
	set_sta_power(priv, &power, sta_idx);
	udelay(40);
} /* add_power_template */


/* @num: The #number of station that is visible to the card */
static void sta_tx_workqueue_init(struct agnx_priv *priv, unsigned int sta_idx)
{
	struct agnx_sta_tx_wq tx_wq;
	u32 reg;
	unsigned int i;

	memset(&tx_wq, 0, sizeof(tx_wq));

	reg = agnx_set_bits(WORK_QUEUE_VALID, WORK_QUEUE_VALID_SHIFT, 1);
	reg |= agnx_set_bits(WORK_QUEUE_ACK_TYPE, WORK_QUEUE_ACK_TYPE_SHIFT, 1);
/*	reg |= agnx_set_bits(WORK_QUEUE_ACK_TYPE, WORK_QUEUE_ACK_TYPE_SHIFT, 0); */
	tx_wq.reg2 |= cpu_to_le32(reg);

	/* Suppose all 8 traffic class are used */
	for (i = 0; i < STA_TX_WQ_NUM; i++)
		set_sta_tx_wq(priv, &tx_wq, sta_idx, i);
} /* sta_tx_workqueue_init */


static void sta_traffic_init(struct agnx_sta_traffic *traffic)
{
	u32 reg;
	memset(traffic, 0, sizeof(*traffic));

	reg = agnx_set_bits(NEW_PACKET, NEW_PACKET_SHIFT, 1);
	reg |= agnx_set_bits(TRAFFIC_VALID, TRAFFIC_VALID_SHIFT, 1);
/*	reg |= agnx_set_bits(TRAFFIC_ACK_TYPE, TRAFFIC_ACK_TYPE_SHIFT, 1); */
	traffic->reg0 = cpu_to_le32(reg);

	/* 	3. setting RX Sequence Number to 4095 */
	reg = agnx_set_bits(RX_SEQUENCE_NUM, RX_SEQUENCE_NUM_SHIFT, 4095);
	traffic->reg1 = cpu_to_le32(reg);
}


/* @num: The #number of station that is visible to the card */
void sta_init(struct agnx_priv *priv, unsigned int sta_idx)
{
	/* FIXME the length of sta is 256 bytes Is that
	 * dangerous to stack overflow? */
	struct agnx_sta sta;
	u32 reg;
	int i;

	memset(&sta, 0, sizeof(sta));
	/* Set valid to 1 */
	reg = agnx_set_bits(STATION_VALID, STATION_VALID_SHIFT, 1);
	/* Set Enable Concatenation to 0 (?) */
	reg |= agnx_set_bits(ENABLE_CONCATENATION, ENABLE_CONCATENATION_SHIFT, 0);
	/* Set Enable Decompression to 0 (?) */
	reg |= agnx_set_bits(ENABLE_DECOMPRESSION, ENABLE_DECOMPRESSION_SHIFT, 0);
	sta.reg = cpu_to_le32(reg);

	/* Initialize each of the Traffic Class Structures by: */
	for (i = 0; i < 8; i++)
		sta_traffic_init(sta.traffic + i);

	set_sta(priv, &sta, sta_idx);
	sta_tx_workqueue_init(priv, sta_idx);
} /* sta_descriptor_init */


