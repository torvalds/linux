#include <linux/pci.h>
#include <linux/delay.h>
#include "agnx.h"
#include "debug.h"
#include "phy.h"

static const u32
tx_fir_table[] = { 0x19, 0x5d, 0xce, 0x151, 0x1c3, 0x1ff, 0x1ea, 0x17c, 0xcf,
		   0x19, 0x38e, 0x350, 0x362, 0x3ad, 0x5, 0x44, 0x59, 0x49,
		   0x21, 0x3f7, 0x3e0, 0x3e3, 0x3f3, 0x0 };

void tx_fir_table_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	int i;

	for (i = 0; i < ARRAY_SIZE(tx_fir_table); i++)
		iowrite32(tx_fir_table[i], ctl + AGNX_FIR_BASE + i*4);
} /* fir_table_setup */


static const u32
gain_table[] = { 0x8, 0x8, 0xf, 0x13, 0x17, 0x1b, 0x1f, 0x23, 0x27, 0x2b,
		 0x2f, 0x33, 0x37, 0x3b, 0x3f, 0x43, 0x47, 0x4b, 0x4f,
		 0x53, 0x57, 0x5b, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f, 0x5f,
		 0x5f, 0x5f, 0x5f, 0x5f };

void gain_table_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	int i;

	for (i = 0; i < ARRAY_SIZE(gain_table); i++) {
		iowrite32(gain_table[i], ctl + AGNX_GAIN_TABLE + i*4);
		iowrite32(gain_table[i], ctl + AGNX_GAIN_TABLE + i*4 + 0x80);
	}
} /* gain_table_init */

void monitor_gain_table_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	unsigned int i;

	for (i = 0; i < 0x44; i += 4) {
		iowrite32(0x61, ctl + AGNX_MONGCR_BASE + i);
		iowrite32(0x61, ctl + AGNX_MONGCR_BASE + 0x200 + i);
	}
	for (i = 0x44; i < 0x64; i += 4) {
		iowrite32(0x6e, ctl + AGNX_MONGCR_BASE + i);
		iowrite32(0x6e, ctl + AGNX_MONGCR_BASE + 0x200 + i);
	}
	for (i = 0x64; i < 0x94; i += 4) {
		iowrite32(0x7a, ctl + AGNX_MONGCR_BASE + i);
		iowrite32(0x7a, ctl + AGNX_MONGCR_BASE + 0x200 + i);
	}
	for (i = 0x94; i < 0xdc; i += 4) {
		iowrite32(0x87, ctl + AGNX_MONGCR_BASE + i);
		iowrite32(0x87, ctl + AGNX_MONGCR_BASE + 0x200 + i);
	}
	for (i = 0xdc; i < 0x148; i += 4) {
		iowrite32(0x95, ctl + AGNX_MONGCR_BASE + i);
		iowrite32(0x95, ctl + AGNX_MONGCR_BASE + 0x200 + i);
	}
	for (i = 0x148; i < 0x1e8; i += 4) {
		iowrite32(0xa2, ctl + AGNX_MONGCR_BASE + i);
		iowrite32(0xa2, ctl + AGNX_MONGCR_BASE + 0x200 + i);
	}
	for (i = 0x1e8; i <= 0x1fc; i += 4) {
		iowrite32(0xb0, ctl + AGNX_MONGCR_BASE + i);
		iowrite32(0xb0, ctl + AGNX_MONGCR_BASE + 0x200 + i);
	}
} /* monitor_gain_table_init */


void routing_table_init(struct agnx_priv *priv)
{
	void __iomem *ctl = priv->ctl;
	unsigned int type, subtype;
	u32 reg;

	disable_receiver(priv);

	for ( type = 0; type < 0x3; type++ ) {
		for (subtype = 0; subtype < 0x10; subtype++) {
			/* 1. Set Routing table to R/W and to Return status on Read */
			reg = (type << ROUTAB_TYPE_SHIFT) |
				(subtype << ROUTAB_SUBTYPE_SHIFT);
			reg |= (1 << ROUTAB_RW_SHIFT) | (1 << ROUTAB_STATUS_SHIFT);
			if (type == ROUTAB_TYPE_DATA) {
				/* NULL goes to RFP */
				if (subtype == ROUTAB_SUBTYPE_NULL)
//					reg |= ROUTAB_ROUTE_RFP;
					reg |= ROUTAB_ROUTE_CPU;
				/* QOS NULL goes to CPU */
				else if (subtype == ROUTAB_SUBTYPE_QOSNULL)
					reg |= ROUTAB_ROUTE_CPU;
				/* All Data and QOS data subtypes go to Encryption */
				else if ((subtype == ROUTAB_SUBTYPE_DATA) ||
					 (subtype == ROUTAB_SUBTYPE_DATAACK) ||
					 (subtype == ROUTAB_SUBTYPE_DATAPOLL) ||
					 (subtype == ROUTAB_SUBTYPE_DATAPOLLACK) ||
					 (subtype == ROUTAB_SUBTYPE_QOSDATA) ||
					 (subtype == ROUTAB_SUBTYPE_QOSDATAACK) ||
					 (subtype == ROUTAB_SUBTYPE_QOSDATAPOLL) ||
					 (subtype == ROUTAB_SUBTYPE_QOSDATAACKPOLL))
					reg |= ROUTAB_ROUTE_ENCRY;
//					reg |= ROUTAB_ROUTE_CPU;
				/*Drop NULL and QOS NULL ack, poll and poll ack*/
				else if ((subtype == ROUTAB_SUBTYPE_NULLACK) ||
					 (subtype == ROUTAB_SUBTYPE_QOSNULLACK) ||
					 (subtype == ROUTAB_SUBTYPE_NULLPOLL) ||
					 (subtype == ROUTAB_SUBTYPE_QOSNULLPOLL) ||
					 (subtype == ROUTAB_SUBTYPE_NULLPOLLACK) ||
					 (subtype == ROUTAB_SUBTYPE_QOSNULLPOLLACK))
//					reg |= ROUTAB_ROUTE_DROP;
					reg |= ROUTAB_ROUTE_CPU;
			}
			else
				reg |= (ROUTAB_ROUTE_CPU);
			iowrite32(reg, ctl + AGNX_RXM_ROUTAB);
			/* Check to verify that the status bit cleared */
			routing_table_delay();
		}
	}
	enable_receiver(priv);
} /* routing_table_init */

void tx_engine_lookup_tbl_init(struct agnx_priv *priv)
{
	void __iomem *data = priv->data;
	unsigned int i;

	for (i = 0; i <= 28; i += 4)
		iowrite32(0xb00c, data + AGNX_ENGINE_LOOKUP_TBL + i);
	for (i = 32; i <= 120; i += 8) {
		iowrite32(0x1e58, data + AGNX_ENGINE_LOOKUP_TBL + i);
		iowrite32(0xb00c, data + AGNX_ENGINE_LOOKUP_TBL + i + 4);
	}

	for (i = 128; i <= 156; i += 4)
		iowrite32(0x980c, data + AGNX_ENGINE_LOOKUP_TBL + i);
	for (i = 160; i <= 248; i += 8) {
		iowrite32(0x1858, data + AGNX_ENGINE_LOOKUP_TBL + i);
		iowrite32(0x980c, data + AGNX_ENGINE_LOOKUP_TBL + i + 4);
	}

	for (i = 256; i <= 284; i += 4)
		iowrite32(0x980c, data + AGNX_ENGINE_LOOKUP_TBL + i);
	for (i = 288; i <= 376; i += 8) {
		iowrite32(0x1a58, data + AGNX_ENGINE_LOOKUP_TBL + i);
		iowrite32(0x1858, data + AGNX_ENGINE_LOOKUP_TBL + i + 4);
	}

	for (i = 512; i <= 540; i += 4)
		iowrite32(0xc00c, data + AGNX_ENGINE_LOOKUP_TBL + i);
	for (i = 544; i <= 632; i += 8) {
		iowrite32(0x2058, data + AGNX_ENGINE_LOOKUP_TBL + i);
		iowrite32(0xc00c, data + AGNX_ENGINE_LOOKUP_TBL + i + 4);
	}

	for (i = 640; i <= 668; i += 4)
		iowrite32(0xc80c, data + AGNX_ENGINE_LOOKUP_TBL + i);
	for (i = 672; i <= 764; i += 8) {
		iowrite32(0x2258, data + AGNX_ENGINE_LOOKUP_TBL + i);
		iowrite32(0xc80c, data + AGNX_ENGINE_LOOKUP_TBL + i + 4);
	}
}

