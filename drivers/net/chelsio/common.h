/*****************************************************************************
 *                                                                           *
 * File: common.h                                                            *
 * $Revision: 1.5 $                                                          *
 * $Date: 2005/03/23 07:41:27 $                                              *
 * Description:                                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#ifndef CHELSIO_COMMON_H
#define CHELSIO_COMMON_H

#define DIMOF(x) (sizeof(x)/sizeof(x[0]))

#define NMTUS      8
#define MAX_NPORTS 4
#define TCB_SIZE   128

enum {
	CHBT_BOARD_7500,
	CHBT_BOARD_8000,
	CHBT_BOARD_CHT101,
	CHBT_BOARD_CHT110,
	CHBT_BOARD_CHT210,
	CHBT_BOARD_CHT204,
	CHBT_BOARD_N110,
	CHBT_BOARD_N210,
	CHBT_BOARD_COUGAR,
	CHBT_BOARD_6800,
	CHBT_BOARD_SIMUL
};

enum {
	CHBT_TERM_FPGA,
	CHBT_TERM_T1,
	CHBT_TERM_T2,
	CHBT_TERM_T3
};

enum {
	CHBT_MAC_CHELSIO_A,
	CHBT_MAC_IXF1010,
	CHBT_MAC_PM3393,
	CHBT_MAC_VSC7321,
	CHBT_MAC_DUMMY
};

enum {
	CHBT_PHY_88E1041,
	CHBT_PHY_88E1111,
	CHBT_PHY_88X2010,
	CHBT_PHY_XPAK,
	CHBT_PHY_MY3126,
	CHBT_PHY_DUMMY
};

enum {
	PAUSE_RX = 1,
	PAUSE_TX = 2,
	PAUSE_AUTONEG = 4
};

/* Revisions of T1 chip */
#define TERM_T1A     0
#define TERM_T1B     1
#define TERM_T2      3

struct tp_params {
	unsigned int pm_size;
	unsigned int cm_size;
	unsigned int pm_rx_base;
	unsigned int pm_tx_base;
	unsigned int pm_rx_pg_size;
	unsigned int pm_tx_pg_size;
	unsigned int pm_rx_num_pgs;
	unsigned int pm_tx_num_pgs;
	unsigned int use_5tuple_mode;
};

struct sge_params {
	unsigned int cmdQ_size[2];
	unsigned int freelQ_size[2];
	unsigned int large_buf_capacity;
	unsigned int rx_coalesce_usecs;
	unsigned int last_rx_coalesce_raw;
	unsigned int default_rx_coalesce_usecs;
	unsigned int sample_interval_usecs;
	unsigned int coalesce_enable;
	unsigned int polling;
};

struct mc5_params {
	unsigned int mode;	/* selects MC5 width */
	unsigned int nservers;	/* size of server region */
	unsigned int nroutes;	/* size of routing region */
};

/* Default MC5 region sizes */
#define DEFAULT_SERVER_REGION_LEN 256
#define DEFAULT_RT_REGION_LEN 1024

struct pci_params {
	unsigned short speed;
	unsigned char  width;
	unsigned char  is_pcix;
};

struct adapter_params {
	struct sge_params sge;
	struct mc5_params mc5;
	struct tp_params  tp;
	struct pci_params pci;

	const struct board_info *brd_info;

	unsigned short mtus[NMTUS];
	unsigned int   nports;         /* # of ethernet ports */
	unsigned int   stats_update_period;
	unsigned short chip_revision;
	unsigned char  chip_version;
	unsigned char  is_asic;
};

struct pci_err_cnt {
	unsigned int master_parity_err;
	unsigned int sig_target_abort;
	unsigned int rcv_target_abort;
	unsigned int rcv_master_abort;
	unsigned int sig_sys_err;
	unsigned int det_parity_err;
	unsigned int pio_parity_err;
	unsigned int wf_parity_err;
	unsigned int rf_parity_err;
	unsigned int cf_parity_err;
};

struct link_config {
	unsigned int   supported;        /* link capabilities */
	unsigned int   advertising;      /* advertised capabilities */
	unsigned short requested_speed;  /* speed user has requested */
	unsigned short speed;            /* actual link speed */
	unsigned char  requested_duplex; /* duplex user has requested */
	unsigned char  duplex;           /* actual link duplex */
	unsigned char  requested_fc;     /* flow control user has requested */
	unsigned char  fc;               /* actual link flow control */
	unsigned char  autoneg;          /* autonegotiating? */
};

#define SPEED_INVALID   0xffff
#define DUPLEX_INVALID  0xff

struct mdio_ops;
struct gmac;
struct gphy;

struct board_info {
	unsigned char           board;
	unsigned char           port_number;
	unsigned long           caps;
	unsigned char           chip_term;
	unsigned char           chip_mac;
	unsigned char           chip_phy;
	unsigned int            clock_core;
	unsigned int            clock_mc3;
	unsigned int            clock_mc4;
	unsigned int            espi_nports;
	unsigned int            clock_cspi;
	unsigned int            clock_elmer0;
	unsigned char           mdio_mdien;
	unsigned char           mdio_mdiinv;
	unsigned char           mdio_mdc;
	unsigned char           mdio_phybaseaddr;
	struct gmac            *gmac;
	struct gphy            *gphy;
	struct mdio_ops	       *mdio_ops;
	const char             *desc;
};

#include "osdep.h"

#ifndef PCI_VENDOR_ID_CHELSIO
#define PCI_VENDOR_ID_CHELSIO 0x1425
#endif

extern struct pci_device_id t1_pci_tbl[];

static inline int t1_is_asic(const adapter_t *adapter)
{
	return adapter->params.is_asic;
}

static inline int adapter_matches_type(const adapter_t *adapter,
				       int version, int revision)
{
	return adapter->params.chip_version == version &&
	       adapter->params.chip_revision == revision;
}

#define t1_is_T1B(adap) adapter_matches_type(adap, CHBT_TERM_T1, TERM_T1B)
#define is_T2(adap)     adapter_matches_type(adap, CHBT_TERM_T2, TERM_T2)

/* Returns true if an adapter supports VLAN acceleration and TSO */
static inline int vlan_tso_capable(const adapter_t *adapter)
{
	return !t1_is_T1B(adapter);
}

#define for_each_port(adapter, iter) \
	for (iter = 0; iter < (adapter)->params.nports; ++iter)

#define board_info(adapter) ((adapter)->params.brd_info)
#define is_10G(adapter) (board_info(adapter)->caps & SUPPORTED_10000baseT_Full)

static inline unsigned int core_ticks_per_usec(const adapter_t *adap)
{
	return board_info(adap)->clock_core / 1000000;
}

int t1_tpi_write(adapter_t *adapter, u32 addr, u32 value);
int t1_tpi_read(adapter_t *adapter, u32 addr, u32 *value);

void t1_interrupts_enable(adapter_t *adapter);
void t1_interrupts_disable(adapter_t *adapter);
void t1_interrupts_clear(adapter_t *adapter);
int elmer0_ext_intr_handler(adapter_t *adapter);
int t1_slow_intr_handler(adapter_t *adapter);

int t1_link_start(struct cphy *phy, struct cmac *mac, struct link_config *lc);
const struct board_info *t1_get_board_info(unsigned int board_id);
const struct board_info *t1_get_board_info_from_ids(unsigned int devid,
						    unsigned short ssid);
int t1_seeprom_read(adapter_t *adapter, u32 addr, u32 *data);
int t1_get_board_rev(adapter_t *adapter, const struct board_info *bi,
		     struct adapter_params *p);
int t1_init_hw_modules(adapter_t *adapter);
int t1_init_sw_modules(adapter_t *adapter, const struct board_info *bi);
void t1_free_sw_modules(adapter_t *adapter);
void t1_fatal_err(adapter_t *adapter);
#endif

