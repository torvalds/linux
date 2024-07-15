/* SPDX-License-Identifier: GPL-2.0 */
/* NXP C45 PHY driver header file
 * Copyright 2023 NXP
 * Author: Radu Pirea <radu-nicolae.pirea@oss.nxp.com>
 */

#include <linux/ptp_clock_kernel.h>

#define VEND1_PORT_FUNC_ENABLES		0x8048

struct nxp_c45_macsec;

struct nxp_c45_phy {
	const struct nxp_c45_phy_data *phy_data;
	struct phy_device *phydev;
	struct mii_timestamper mii_ts;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info caps;
	struct sk_buff_head tx_queue;
	struct sk_buff_head rx_queue;
	/* used to access the PTP registers atomic */
	struct mutex ptp_lock;
	int hwts_tx;
	int hwts_rx;
	u32 tx_delay;
	u32 rx_delay;
	struct timespec64 extts_ts;
	int extts_index;
	bool extts;
	struct nxp_c45_macsec *macsec;
};

#if IS_ENABLED(CONFIG_MACSEC)
int nxp_c45_macsec_config_init(struct phy_device *phydev);
int nxp_c45_macsec_probe(struct phy_device *phydev);
void nxp_c45_macsec_remove(struct phy_device *phydev);
void nxp_c45_handle_macsec_interrupt(struct phy_device *phydev,
				     irqreturn_t *ret);
#else
static inline
int nxp_c45_macsec_config_init(struct phy_device *phydev)
{
	return 0;
}

static inline
int nxp_c45_macsec_probe(struct phy_device *phydev)
{
	return 0;
}

static inline
void nxp_c45_macsec_remove(struct phy_device *phydev)
{
}

static inline
void nxp_c45_handle_macsec_interrupt(struct phy_device *phydev,
				     irqreturn_t *ret)
{
}
#endif
