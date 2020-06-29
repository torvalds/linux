// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include "sja1105.h"

#define SJA1105_SIZE_MAC_AREA		(0x02 * 4)
#define SJA1105_SIZE_HL1_AREA		(0x10 * 4)
#define SJA1105_SIZE_HL2_AREA		(0x4 * 4)
#define SJA1105_SIZE_QLEVEL_AREA	(0x8 * 4) /* 0x4 to 0xB */
#define SJA1105_SIZE_ETHER_AREA		(0x17 * 4)

struct sja1105_port_status_mac {
	u64 n_runt;
	u64 n_soferr;
	u64 n_alignerr;
	u64 n_miierr;
	u64 typeerr;
	u64 sizeerr;
	u64 tctimeout;
	u64 priorerr;
	u64 nomaster;
	u64 memov;
	u64 memerr;
	u64 invtyp;
	u64 intcyov;
	u64 domerr;
	u64 pcfbagdrop;
	u64 spcprior;
	u64 ageprior;
	u64 portdrop;
	u64 lendrop;
	u64 bagdrop;
	u64 policeerr;
	u64 drpnona664err;
	u64 spcerr;
	u64 agedrp;
};

struct sja1105_port_status_hl1 {
	u64 n_n664err;
	u64 n_vlanerr;
	u64 n_unreleased;
	u64 n_sizeerr;
	u64 n_crcerr;
	u64 n_vlnotfound;
	u64 n_ctpolerr;
	u64 n_polerr;
	u64 n_rxfrmsh;
	u64 n_rxfrm;
	u64 n_rxbytesh;
	u64 n_rxbyte;
	u64 n_txfrmsh;
	u64 n_txfrm;
	u64 n_txbytesh;
	u64 n_txbyte;
};

struct sja1105_port_status_hl2 {
	u64 n_qfull;
	u64 n_part_drop;
	u64 n_egr_disabled;
	u64 n_not_reach;
	u64 qlevel_hwm[8]; /* Only for P/Q/R/S */
	u64 qlevel[8];     /* Only for P/Q/R/S */
};

struct sja1105_port_status_ether {
	u64 n_drops_nolearn;
	u64 n_drops_noroute;
	u64 n_drops_ill_dtag;
	u64 n_drops_dtag;
	u64 n_drops_sotag;
	u64 n_drops_sitag;
	u64 n_drops_utag;
	u64 n_tx_bytes_1024_2047;
	u64 n_tx_bytes_512_1023;
	u64 n_tx_bytes_256_511;
	u64 n_tx_bytes_128_255;
	u64 n_tx_bytes_65_127;
	u64 n_tx_bytes_64;
	u64 n_tx_mcast;
	u64 n_tx_bcast;
	u64 n_rx_bytes_1024_2047;
	u64 n_rx_bytes_512_1023;
	u64 n_rx_bytes_256_511;
	u64 n_rx_bytes_128_255;
	u64 n_rx_bytes_65_127;
	u64 n_rx_bytes_64;
	u64 n_rx_mcast;
	u64 n_rx_bcast;
};

struct sja1105_port_status {
	struct sja1105_port_status_mac mac;
	struct sja1105_port_status_hl1 hl1;
	struct sja1105_port_status_hl2 hl2;
	struct sja1105_port_status_ether ether;
};

static void
sja1105_port_status_mac_unpack(void *buf,
			       struct sja1105_port_status_mac *status)
{
	/* Make pointer arithmetic work on 4 bytes */
	u32 *p = buf;

	sja1105_unpack(p + 0x0, &status->n_runt,       31, 24, 4);
	sja1105_unpack(p + 0x0, &status->n_soferr,     23, 16, 4);
	sja1105_unpack(p + 0x0, &status->n_alignerr,   15,  8, 4);
	sja1105_unpack(p + 0x0, &status->n_miierr,      7,  0, 4);
	sja1105_unpack(p + 0x1, &status->typeerr,      27, 27, 4);
	sja1105_unpack(p + 0x1, &status->sizeerr,      26, 26, 4);
	sja1105_unpack(p + 0x1, &status->tctimeout,    25, 25, 4);
	sja1105_unpack(p + 0x1, &status->priorerr,     24, 24, 4);
	sja1105_unpack(p + 0x1, &status->nomaster,     23, 23, 4);
	sja1105_unpack(p + 0x1, &status->memov,        22, 22, 4);
	sja1105_unpack(p + 0x1, &status->memerr,       21, 21, 4);
	sja1105_unpack(p + 0x1, &status->invtyp,       19, 19, 4);
	sja1105_unpack(p + 0x1, &status->intcyov,      18, 18, 4);
	sja1105_unpack(p + 0x1, &status->domerr,       17, 17, 4);
	sja1105_unpack(p + 0x1, &status->pcfbagdrop,   16, 16, 4);
	sja1105_unpack(p + 0x1, &status->spcprior,     15, 12, 4);
	sja1105_unpack(p + 0x1, &status->ageprior,     11,  8, 4);
	sja1105_unpack(p + 0x1, &status->portdrop,      6,  6, 4);
	sja1105_unpack(p + 0x1, &status->lendrop,       5,  5, 4);
	sja1105_unpack(p + 0x1, &status->bagdrop,       4,  4, 4);
	sja1105_unpack(p + 0x1, &status->policeerr,     3,  3, 4);
	sja1105_unpack(p + 0x1, &status->drpnona664err, 2,  2, 4);
	sja1105_unpack(p + 0x1, &status->spcerr,        1,  1, 4);
	sja1105_unpack(p + 0x1, &status->agedrp,        0,  0, 4);
}

static void
sja1105_port_status_hl1_unpack(void *buf,
			       struct sja1105_port_status_hl1 *status)
{
	/* Make pointer arithmetic work on 4 bytes */
	u32 *p = buf;

	sja1105_unpack(p + 0xF, &status->n_n664err,    31,  0, 4);
	sja1105_unpack(p + 0xE, &status->n_vlanerr,    31,  0, 4);
	sja1105_unpack(p + 0xD, &status->n_unreleased, 31,  0, 4);
	sja1105_unpack(p + 0xC, &status->n_sizeerr,    31,  0, 4);
	sja1105_unpack(p + 0xB, &status->n_crcerr,     31,  0, 4);
	sja1105_unpack(p + 0xA, &status->n_vlnotfound, 31,  0, 4);
	sja1105_unpack(p + 0x9, &status->n_ctpolerr,   31,  0, 4);
	sja1105_unpack(p + 0x8, &status->n_polerr,     31,  0, 4);
	sja1105_unpack(p + 0x7, &status->n_rxfrmsh,    31,  0, 4);
	sja1105_unpack(p + 0x6, &status->n_rxfrm,      31,  0, 4);
	sja1105_unpack(p + 0x5, &status->n_rxbytesh,   31,  0, 4);
	sja1105_unpack(p + 0x4, &status->n_rxbyte,     31,  0, 4);
	sja1105_unpack(p + 0x3, &status->n_txfrmsh,    31,  0, 4);
	sja1105_unpack(p + 0x2, &status->n_txfrm,      31,  0, 4);
	sja1105_unpack(p + 0x1, &status->n_txbytesh,   31,  0, 4);
	sja1105_unpack(p + 0x0, &status->n_txbyte,     31,  0, 4);
	status->n_rxfrm  += status->n_rxfrmsh  << 32;
	status->n_rxbyte += status->n_rxbytesh << 32;
	status->n_txfrm  += status->n_txfrmsh  << 32;
	status->n_txbyte += status->n_txbytesh << 32;
}

static void
sja1105_port_status_hl2_unpack(void *buf,
			       struct sja1105_port_status_hl2 *status)
{
	/* Make pointer arithmetic work on 4 bytes */
	u32 *p = buf;

	sja1105_unpack(p + 0x3, &status->n_qfull,        31,  0, 4);
	sja1105_unpack(p + 0x2, &status->n_part_drop,    31,  0, 4);
	sja1105_unpack(p + 0x1, &status->n_egr_disabled, 31,  0, 4);
	sja1105_unpack(p + 0x0, &status->n_not_reach,    31,  0, 4);
}

static void
sja1105pqrs_port_status_qlevel_unpack(void *buf,
				      struct sja1105_port_status_hl2 *status)
{
	/* Make pointer arithmetic work on 4 bytes */
	u32 *p = buf;
	int i;

	for (i = 0; i < 8; i++) {
		sja1105_unpack(p + i, &status->qlevel_hwm[i], 24, 16, 4);
		sja1105_unpack(p + i, &status->qlevel[i],      8,  0, 4);
	}
}

static void
sja1105pqrs_port_status_ether_unpack(void *buf,
				     struct sja1105_port_status_ether *status)
{
	/* Make pointer arithmetic work on 4 bytes */
	u32 *p = buf;

	sja1105_unpack(p + 0x16, &status->n_drops_nolearn,      31, 0, 4);
	sja1105_unpack(p + 0x15, &status->n_drops_noroute,      31, 0, 4);
	sja1105_unpack(p + 0x14, &status->n_drops_ill_dtag,     31, 0, 4);
	sja1105_unpack(p + 0x13, &status->n_drops_dtag,         31, 0, 4);
	sja1105_unpack(p + 0x12, &status->n_drops_sotag,        31, 0, 4);
	sja1105_unpack(p + 0x11, &status->n_drops_sitag,        31, 0, 4);
	sja1105_unpack(p + 0x10, &status->n_drops_utag,         31, 0, 4);
	sja1105_unpack(p + 0x0F, &status->n_tx_bytes_1024_2047, 31, 0, 4);
	sja1105_unpack(p + 0x0E, &status->n_tx_bytes_512_1023,  31, 0, 4);
	sja1105_unpack(p + 0x0D, &status->n_tx_bytes_256_511,   31, 0, 4);
	sja1105_unpack(p + 0x0C, &status->n_tx_bytes_128_255,   31, 0, 4);
	sja1105_unpack(p + 0x0B, &status->n_tx_bytes_65_127,    31, 0, 4);
	sja1105_unpack(p + 0x0A, &status->n_tx_bytes_64,        31, 0, 4);
	sja1105_unpack(p + 0x09, &status->n_tx_mcast,           31, 0, 4);
	sja1105_unpack(p + 0x08, &status->n_tx_bcast,           31, 0, 4);
	sja1105_unpack(p + 0x07, &status->n_rx_bytes_1024_2047, 31, 0, 4);
	sja1105_unpack(p + 0x06, &status->n_rx_bytes_512_1023,  31, 0, 4);
	sja1105_unpack(p + 0x05, &status->n_rx_bytes_256_511,   31, 0, 4);
	sja1105_unpack(p + 0x04, &status->n_rx_bytes_128_255,   31, 0, 4);
	sja1105_unpack(p + 0x03, &status->n_rx_bytes_65_127,    31, 0, 4);
	sja1105_unpack(p + 0x02, &status->n_rx_bytes_64,        31, 0, 4);
	sja1105_unpack(p + 0x01, &status->n_rx_mcast,           31, 0, 4);
	sja1105_unpack(p + 0x00, &status->n_rx_bcast,           31, 0, 4);
}

static int
sja1105pqrs_port_status_get_ether(struct sja1105_private *priv,
				  struct sja1105_port_status_ether *ether,
				  int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 packed_buf[SJA1105_SIZE_ETHER_AREA] = {0};
	int rc;

	/* Ethernet statistics area */
	rc = sja1105_xfer_buf(priv, SPI_READ, regs->ether_stats[port],
			      packed_buf, SJA1105_SIZE_ETHER_AREA);
	if (rc < 0)
		return rc;

	sja1105pqrs_port_status_ether_unpack(packed_buf, ether);

	return 0;
}

static int sja1105_port_status_get_mac(struct sja1105_private *priv,
				       struct sja1105_port_status_mac *status,
				       int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 packed_buf[SJA1105_SIZE_MAC_AREA] = {0};
	int rc;

	/* MAC area */
	rc = sja1105_xfer_buf(priv, SPI_READ, regs->mac[port], packed_buf,
			      SJA1105_SIZE_MAC_AREA);
	if (rc < 0)
		return rc;

	sja1105_port_status_mac_unpack(packed_buf, status);

	return 0;
}

static int sja1105_port_status_get_hl1(struct sja1105_private *priv,
				       struct sja1105_port_status_hl1 *status,
				       int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 packed_buf[SJA1105_SIZE_HL1_AREA] = {0};
	int rc;

	rc = sja1105_xfer_buf(priv, SPI_READ, regs->mac_hl1[port], packed_buf,
			      SJA1105_SIZE_HL1_AREA);
	if (rc < 0)
		return rc;

	sja1105_port_status_hl1_unpack(packed_buf, status);

	return 0;
}

static int sja1105_port_status_get_hl2(struct sja1105_private *priv,
				       struct sja1105_port_status_hl2 *status,
				       int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 packed_buf[SJA1105_SIZE_QLEVEL_AREA] = {0};
	int rc;

	rc = sja1105_xfer_buf(priv, SPI_READ, regs->mac_hl2[port], packed_buf,
			      SJA1105_SIZE_HL2_AREA);
	if (rc < 0)
		return rc;

	sja1105_port_status_hl2_unpack(packed_buf, status);

	/* Code below is strictly P/Q/R/S specific. */
	if (priv->info->device_id == SJA1105E_DEVICE_ID ||
	    priv->info->device_id == SJA1105T_DEVICE_ID)
		return 0;

	rc = sja1105_xfer_buf(priv, SPI_READ, regs->qlevel[port], packed_buf,
			      SJA1105_SIZE_QLEVEL_AREA);
	if (rc < 0)
		return rc;

	sja1105pqrs_port_status_qlevel_unpack(packed_buf, status);

	return 0;
}

static int sja1105_port_status_get(struct sja1105_private *priv,
				   struct sja1105_port_status *status,
				   int port)
{
	int rc;

	rc = sja1105_port_status_get_mac(priv, &status->mac, port);
	if (rc < 0)
		return rc;
	rc = sja1105_port_status_get_hl1(priv, &status->hl1, port);
	if (rc < 0)
		return rc;
	rc = sja1105_port_status_get_hl2(priv, &status->hl2, port);
	if (rc < 0)
		return rc;

	if (priv->info->device_id == SJA1105E_DEVICE_ID ||
	    priv->info->device_id == SJA1105T_DEVICE_ID)
		return 0;

	return sja1105pqrs_port_status_get_ether(priv, &status->ether, port);
}

static char sja1105_port_stats[][ETH_GSTRING_LEN] = {
	/* MAC-Level Diagnostic Counters */
	"n_runt",
	"n_soferr",
	"n_alignerr",
	"n_miierr",
	/* MAC-Level Diagnostic Flags */
	"typeerr",
	"sizeerr",
	"tctimeout",
	"priorerr",
	"nomaster",
	"memov",
	"memerr",
	"invtyp",
	"intcyov",
	"domerr",
	"pcfbagdrop",
	"spcprior",
	"ageprior",
	"portdrop",
	"lendrop",
	"bagdrop",
	"policeerr",
	"drpnona664err",
	"spcerr",
	"agedrp",
	/* High-Level Diagnostic Counters */
	"n_n664err",
	"n_vlanerr",
	"n_unreleased",
	"n_sizeerr",
	"n_crcerr",
	"n_vlnotfound",
	"n_ctpolerr",
	"n_polerr",
	"n_rxfrm",
	"n_rxbyte",
	"n_txfrm",
	"n_txbyte",
	"n_qfull",
	"n_part_drop",
	"n_egr_disabled",
	"n_not_reach",
};

static char sja1105pqrs_extra_port_stats[][ETH_GSTRING_LEN] = {
	/* Queue Levels */
	"qlevel_hwm_0",
	"qlevel_hwm_1",
	"qlevel_hwm_2",
	"qlevel_hwm_3",
	"qlevel_hwm_4",
	"qlevel_hwm_5",
	"qlevel_hwm_6",
	"qlevel_hwm_7",
	"qlevel_0",
	"qlevel_1",
	"qlevel_2",
	"qlevel_3",
	"qlevel_4",
	"qlevel_5",
	"qlevel_6",
	"qlevel_7",
	/* Ether Stats */
	"n_drops_nolearn",
	"n_drops_noroute",
	"n_drops_ill_dtag",
	"n_drops_dtag",
	"n_drops_sotag",
	"n_drops_sitag",
	"n_drops_utag",
	"n_tx_bytes_1024_2047",
	"n_tx_bytes_512_1023",
	"n_tx_bytes_256_511",
	"n_tx_bytes_128_255",
	"n_tx_bytes_65_127",
	"n_tx_bytes_64",
	"n_tx_mcast",
	"n_tx_bcast",
	"n_rx_bytes_1024_2047",
	"n_rx_bytes_512_1023",
	"n_rx_bytes_256_511",
	"n_rx_bytes_128_255",
	"n_rx_bytes_65_127",
	"n_rx_bytes_64",
	"n_rx_mcast",
	"n_rx_bcast",
};

void sja1105_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data)
{
	struct sja1105_private *priv = ds->priv;
	struct sja1105_port_status *status;
	int rc, i, k = 0;

	status = kzalloc(sizeof(*status), GFP_KERNEL);
	if (!status)
		goto out;

	rc = sja1105_port_status_get(priv, status, port);
	if (rc < 0) {
		dev_err(ds->dev, "Failed to read port %d counters: %d\n",
			port, rc);
		goto out;
	}
	memset(data, 0, ARRAY_SIZE(sja1105_port_stats) * sizeof(u64));
	data[k++] = status->mac.n_runt;
	data[k++] = status->mac.n_soferr;
	data[k++] = status->mac.n_alignerr;
	data[k++] = status->mac.n_miierr;
	data[k++] = status->mac.typeerr;
	data[k++] = status->mac.sizeerr;
	data[k++] = status->mac.tctimeout;
	data[k++] = status->mac.priorerr;
	data[k++] = status->mac.nomaster;
	data[k++] = status->mac.memov;
	data[k++] = status->mac.memerr;
	data[k++] = status->mac.invtyp;
	data[k++] = status->mac.intcyov;
	data[k++] = status->mac.domerr;
	data[k++] = status->mac.pcfbagdrop;
	data[k++] = status->mac.spcprior;
	data[k++] = status->mac.ageprior;
	data[k++] = status->mac.portdrop;
	data[k++] = status->mac.lendrop;
	data[k++] = status->mac.bagdrop;
	data[k++] = status->mac.policeerr;
	data[k++] = status->mac.drpnona664err;
	data[k++] = status->mac.spcerr;
	data[k++] = status->mac.agedrp;
	data[k++] = status->hl1.n_n664err;
	data[k++] = status->hl1.n_vlanerr;
	data[k++] = status->hl1.n_unreleased;
	data[k++] = status->hl1.n_sizeerr;
	data[k++] = status->hl1.n_crcerr;
	data[k++] = status->hl1.n_vlnotfound;
	data[k++] = status->hl1.n_ctpolerr;
	data[k++] = status->hl1.n_polerr;
	data[k++] = status->hl1.n_rxfrm;
	data[k++] = status->hl1.n_rxbyte;
	data[k++] = status->hl1.n_txfrm;
	data[k++] = status->hl1.n_txbyte;
	data[k++] = status->hl2.n_qfull;
	data[k++] = status->hl2.n_part_drop;
	data[k++] = status->hl2.n_egr_disabled;
	data[k++] = status->hl2.n_not_reach;

	if (priv->info->device_id == SJA1105E_DEVICE_ID ||
	    priv->info->device_id == SJA1105T_DEVICE_ID)
		goto out;

	memset(data + k, 0, ARRAY_SIZE(sja1105pqrs_extra_port_stats) *
			sizeof(u64));
	for (i = 0; i < 8; i++) {
		data[k++] = status->hl2.qlevel_hwm[i];
		data[k++] = status->hl2.qlevel[i];
	}
	data[k++] = status->ether.n_drops_nolearn;
	data[k++] = status->ether.n_drops_noroute;
	data[k++] = status->ether.n_drops_ill_dtag;
	data[k++] = status->ether.n_drops_dtag;
	data[k++] = status->ether.n_drops_sotag;
	data[k++] = status->ether.n_drops_sitag;
	data[k++] = status->ether.n_drops_utag;
	data[k++] = status->ether.n_tx_bytes_1024_2047;
	data[k++] = status->ether.n_tx_bytes_512_1023;
	data[k++] = status->ether.n_tx_bytes_256_511;
	data[k++] = status->ether.n_tx_bytes_128_255;
	data[k++] = status->ether.n_tx_bytes_65_127;
	data[k++] = status->ether.n_tx_bytes_64;
	data[k++] = status->ether.n_tx_mcast;
	data[k++] = status->ether.n_tx_bcast;
	data[k++] = status->ether.n_rx_bytes_1024_2047;
	data[k++] = status->ether.n_rx_bytes_512_1023;
	data[k++] = status->ether.n_rx_bytes_256_511;
	data[k++] = status->ether.n_rx_bytes_128_255;
	data[k++] = status->ether.n_rx_bytes_65_127;
	data[k++] = status->ether.n_rx_bytes_64;
	data[k++] = status->ether.n_rx_mcast;
	data[k++] = status->ether.n_rx_bcast;
out:
	kfree(status);
}

void sja1105_get_strings(struct dsa_switch *ds, int port,
			 u32 stringset, u8 *data)
{
	struct sja1105_private *priv = ds->priv;
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(sja1105_port_stats); i++) {
			strlcpy(p, sja1105_port_stats[i], ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		if (priv->info->device_id == SJA1105E_DEVICE_ID ||
		    priv->info->device_id == SJA1105T_DEVICE_ID)
			return;
		for (i = 0; i < ARRAY_SIZE(sja1105pqrs_extra_port_stats); i++) {
			strlcpy(p, sja1105pqrs_extra_port_stats[i],
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

int sja1105_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	int count = ARRAY_SIZE(sja1105_port_stats);
	struct sja1105_private *priv = ds->priv;

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	if (priv->info->device_id == SJA1105PR_DEVICE_ID ||
	    priv->info->device_id == SJA1105QS_DEVICE_ID)
		count += ARRAY_SIZE(sja1105pqrs_extra_port_stats);

	return count;
}
