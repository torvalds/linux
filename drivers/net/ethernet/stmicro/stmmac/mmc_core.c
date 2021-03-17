// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  DWMAC Management Counters

  Copyright (C) 2011  STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/kernel.h>
#include <linux/io.h>
#include "hwif.h"
#include "mmc.h"

/* MAC Management Counters register offset */

#define MMC_CNTRL		0x00	/* MMC Control */
#define MMC_RX_INTR		0x04	/* MMC RX Interrupt */
#define MMC_TX_INTR		0x08	/* MMC TX Interrupt */
#define MMC_RX_INTR_MASK	0x0c	/* MMC Interrupt Mask */
#define MMC_TX_INTR_MASK	0x10	/* MMC Interrupt Mask */
#define MMC_DEFAULT_MASK	0xffffffff

/* MMC TX counter registers */

/* Note:
 * _GB register stands for good and bad frames
 * _G is for good only.
 */
#define MMC_TX_OCTETCOUNT_GB		0x14
#define MMC_TX_FRAMECOUNT_GB		0x18
#define MMC_TX_BROADCASTFRAME_G		0x1c
#define MMC_TX_MULTICASTFRAME_G		0x20
#define MMC_TX_64_OCTETS_GB		0x24
#define MMC_TX_65_TO_127_OCTETS_GB	0x28
#define MMC_TX_128_TO_255_OCTETS_GB	0x2c
#define MMC_TX_256_TO_511_OCTETS_GB	0x30
#define MMC_TX_512_TO_1023_OCTETS_GB	0x34
#define MMC_TX_1024_TO_MAX_OCTETS_GB	0x38
#define MMC_TX_UNICAST_GB		0x3c
#define MMC_TX_MULTICAST_GB		0x40
#define MMC_TX_BROADCAST_GB		0x44
#define MMC_TX_UNDERFLOW_ERROR		0x48
#define MMC_TX_SINGLECOL_G		0x4c
#define MMC_TX_MULTICOL_G		0x50
#define MMC_TX_DEFERRED			0x54
#define MMC_TX_LATECOL			0x58
#define MMC_TX_EXESSCOL			0x5c
#define MMC_TX_CARRIER_ERROR		0x60
#define MMC_TX_OCTETCOUNT_G		0x64
#define MMC_TX_FRAMECOUNT_G		0x68
#define MMC_TX_EXCESSDEF		0x6c
#define MMC_TX_PAUSE_FRAME		0x70
#define MMC_TX_VLAN_FRAME_G		0x74

/* MMC RX counter registers */
#define MMC_RX_FRAMECOUNT_GB		0x80
#define MMC_RX_OCTETCOUNT_GB		0x84
#define MMC_RX_OCTETCOUNT_G		0x88
#define MMC_RX_BROADCASTFRAME_G		0x8c
#define MMC_RX_MULTICASTFRAME_G		0x90
#define MMC_RX_CRC_ERROR		0x94
#define MMC_RX_ALIGN_ERROR		0x98
#define MMC_RX_RUN_ERROR		0x9C
#define MMC_RX_JABBER_ERROR		0xA0
#define MMC_RX_UNDERSIZE_G		0xA4
#define MMC_RX_OVERSIZE_G		0xA8
#define MMC_RX_64_OCTETS_GB		0xAC
#define MMC_RX_65_TO_127_OCTETS_GB	0xb0
#define MMC_RX_128_TO_255_OCTETS_GB	0xb4
#define MMC_RX_256_TO_511_OCTETS_GB	0xb8
#define MMC_RX_512_TO_1023_OCTETS_GB	0xbc
#define MMC_RX_1024_TO_MAX_OCTETS_GB	0xc0
#define MMC_RX_UNICAST_G		0xc4
#define MMC_RX_LENGTH_ERROR		0xc8
#define MMC_RX_AUTOFRANGETYPE		0xcc
#define MMC_RX_PAUSE_FRAMES		0xd0
#define MMC_RX_FIFO_OVERFLOW		0xd4
#define MMC_RX_VLAN_FRAMES_GB		0xd8
#define MMC_RX_WATCHDOG_ERROR		0xdc
/* IPC*/
#define MMC_RX_IPC_INTR_MASK		0x100
#define MMC_RX_IPC_INTR			0x108
/* IPv4*/
#define MMC_RX_IPV4_GD			0x110
#define MMC_RX_IPV4_HDERR		0x114
#define MMC_RX_IPV4_NOPAY		0x118
#define MMC_RX_IPV4_FRAG		0x11C
#define MMC_RX_IPV4_UDSBL		0x120

#define MMC_RX_IPV4_GD_OCTETS		0x150
#define MMC_RX_IPV4_HDERR_OCTETS	0x154
#define MMC_RX_IPV4_NOPAY_OCTETS	0x158
#define MMC_RX_IPV4_FRAG_OCTETS		0x15c
#define MMC_RX_IPV4_UDSBL_OCTETS	0x160

/* IPV6*/
#define MMC_RX_IPV6_GD_OCTETS		0x164
#define MMC_RX_IPV6_HDERR_OCTETS	0x168
#define MMC_RX_IPV6_NOPAY_OCTETS	0x16c

#define MMC_RX_IPV6_GD			0x124
#define MMC_RX_IPV6_HDERR		0x128
#define MMC_RX_IPV6_NOPAY		0x12c

/* Protocols*/
#define MMC_RX_UDP_GD			0x130
#define MMC_RX_UDP_ERR			0x134
#define MMC_RX_TCP_GD			0x138
#define MMC_RX_TCP_ERR			0x13c
#define MMC_RX_ICMP_GD			0x140
#define MMC_RX_ICMP_ERR			0x144

#define MMC_RX_UDP_GD_OCTETS		0x170
#define MMC_RX_UDP_ERR_OCTETS		0x174
#define MMC_RX_TCP_GD_OCTETS		0x178
#define MMC_RX_TCP_ERR_OCTETS		0x17c
#define MMC_RX_ICMP_GD_OCTETS		0x180
#define MMC_RX_ICMP_ERR_OCTETS		0x184

#define MMC_TX_FPE_FRAG			0x1a8
#define MMC_TX_HOLD_REQ			0x1ac
#define MMC_RX_PKT_ASSEMBLY_ERR		0x1c8
#define MMC_RX_PKT_SMD_ERR		0x1cc
#define MMC_RX_PKT_ASSEMBLY_OK		0x1d0
#define MMC_RX_FPE_FRAG			0x1d4

/* XGMAC MMC Registers */
#define MMC_XGMAC_TX_OCTET_GB		0x14
#define MMC_XGMAC_TX_PKT_GB		0x1c
#define MMC_XGMAC_TX_BROAD_PKT_G	0x24
#define MMC_XGMAC_TX_MULTI_PKT_G	0x2c
#define MMC_XGMAC_TX_64OCT_GB		0x34
#define MMC_XGMAC_TX_65OCT_GB		0x3c
#define MMC_XGMAC_TX_128OCT_GB		0x44
#define MMC_XGMAC_TX_256OCT_GB		0x4c
#define MMC_XGMAC_TX_512OCT_GB		0x54
#define MMC_XGMAC_TX_1024OCT_GB		0x5c
#define MMC_XGMAC_TX_UNI_PKT_GB		0x64
#define MMC_XGMAC_TX_MULTI_PKT_GB	0x6c
#define MMC_XGMAC_TX_BROAD_PKT_GB	0x74
#define MMC_XGMAC_TX_UNDER		0x7c
#define MMC_XGMAC_TX_OCTET_G		0x84
#define MMC_XGMAC_TX_PKT_G		0x8c
#define MMC_XGMAC_TX_PAUSE		0x94
#define MMC_XGMAC_TX_VLAN_PKT_G		0x9c
#define MMC_XGMAC_TX_LPI_USEC		0xa4
#define MMC_XGMAC_TX_LPI_TRAN		0xa8

#define MMC_XGMAC_RX_PKT_GB		0x100
#define MMC_XGMAC_RX_OCTET_GB		0x108
#define MMC_XGMAC_RX_OCTET_G		0x110
#define MMC_XGMAC_RX_BROAD_PKT_G	0x118
#define MMC_XGMAC_RX_MULTI_PKT_G	0x120
#define MMC_XGMAC_RX_CRC_ERR		0x128
#define MMC_XGMAC_RX_RUNT_ERR		0x130
#define MMC_XGMAC_RX_JABBER_ERR		0x134
#define MMC_XGMAC_RX_UNDER		0x138
#define MMC_XGMAC_RX_OVER		0x13c
#define MMC_XGMAC_RX_64OCT_GB		0x140
#define MMC_XGMAC_RX_65OCT_GB		0x148
#define MMC_XGMAC_RX_128OCT_GB		0x150
#define MMC_XGMAC_RX_256OCT_GB		0x158
#define MMC_XGMAC_RX_512OCT_GB		0x160
#define MMC_XGMAC_RX_1024OCT_GB		0x168
#define MMC_XGMAC_RX_UNI_PKT_G		0x170
#define MMC_XGMAC_RX_LENGTH_ERR		0x178
#define MMC_XGMAC_RX_RANGE		0x180
#define MMC_XGMAC_RX_PAUSE		0x188
#define MMC_XGMAC_RX_FIFOOVER_PKT	0x190
#define MMC_XGMAC_RX_VLAN_PKT_GB	0x198
#define MMC_XGMAC_RX_WATCHDOG_ERR	0x1a0
#define MMC_XGMAC_RX_LPI_USEC		0x1a4
#define MMC_XGMAC_RX_LPI_TRAN		0x1a8
#define MMC_XGMAC_RX_DISCARD_PKT_GB	0x1ac
#define MMC_XGMAC_RX_DISCARD_OCT_GB	0x1b4
#define MMC_XGMAC_RX_ALIGN_ERR_PKT	0x1bc

#define MMC_XGMAC_TX_FPE_FRAG		0x208
#define MMC_XGMAC_TX_HOLD_REQ		0x20c
#define MMC_XGMAC_RX_PKT_ASSEMBLY_ERR	0x228
#define MMC_XGMAC_RX_PKT_SMD_ERR	0x22c
#define MMC_XGMAC_RX_PKT_ASSEMBLY_OK	0x230
#define MMC_XGMAC_RX_FPE_FRAG		0x234
#define MMC_XGMAC_RX_IPC_INTR_MASK	0x25c

static void dwmac_mmc_ctrl(void __iomem *mmcaddr, unsigned int mode)
{
	u32 value = readl(mmcaddr + MMC_CNTRL);

	value |= (mode & 0x3F);

	writel(value, mmcaddr + MMC_CNTRL);

	pr_debug("stmmac: MMC ctrl register (offset 0x%x): 0x%08x\n",
		 MMC_CNTRL, value);
}

/* To mask all all interrupts.*/
static void dwmac_mmc_intr_all_mask(void __iomem *mmcaddr)
{
	writel(MMC_DEFAULT_MASK, mmcaddr + MMC_RX_INTR_MASK);
	writel(MMC_DEFAULT_MASK, mmcaddr + MMC_TX_INTR_MASK);
	writel(MMC_DEFAULT_MASK, mmcaddr + MMC_RX_IPC_INTR_MASK);
}

/* This reads the MAC core counters (if actaully supported).
 * by default the MMC core is programmed to reset each
 * counter after a read. So all the field of the mmc struct
 * have to be incremented.
 */
static void dwmac_mmc_read(void __iomem *mmcaddr, struct stmmac_counters *mmc)
{
	mmc->mmc_tx_octetcount_gb += readl(mmcaddr + MMC_TX_OCTETCOUNT_GB);
	mmc->mmc_tx_framecount_gb += readl(mmcaddr + MMC_TX_FRAMECOUNT_GB);
	mmc->mmc_tx_broadcastframe_g += readl(mmcaddr +
					      MMC_TX_BROADCASTFRAME_G);
	mmc->mmc_tx_multicastframe_g += readl(mmcaddr +
					      MMC_TX_MULTICASTFRAME_G);
	mmc->mmc_tx_64_octets_gb += readl(mmcaddr + MMC_TX_64_OCTETS_GB);
	mmc->mmc_tx_65_to_127_octets_gb +=
	    readl(mmcaddr + MMC_TX_65_TO_127_OCTETS_GB);
	mmc->mmc_tx_128_to_255_octets_gb +=
	    readl(mmcaddr + MMC_TX_128_TO_255_OCTETS_GB);
	mmc->mmc_tx_256_to_511_octets_gb +=
	    readl(mmcaddr + MMC_TX_256_TO_511_OCTETS_GB);
	mmc->mmc_tx_512_to_1023_octets_gb +=
	    readl(mmcaddr + MMC_TX_512_TO_1023_OCTETS_GB);
	mmc->mmc_tx_1024_to_max_octets_gb +=
	    readl(mmcaddr + MMC_TX_1024_TO_MAX_OCTETS_GB);
	mmc->mmc_tx_unicast_gb += readl(mmcaddr + MMC_TX_UNICAST_GB);
	mmc->mmc_tx_multicast_gb += readl(mmcaddr + MMC_TX_MULTICAST_GB);
	mmc->mmc_tx_broadcast_gb += readl(mmcaddr + MMC_TX_BROADCAST_GB);
	mmc->mmc_tx_underflow_error += readl(mmcaddr + MMC_TX_UNDERFLOW_ERROR);
	mmc->mmc_tx_singlecol_g += readl(mmcaddr + MMC_TX_SINGLECOL_G);
	mmc->mmc_tx_multicol_g += readl(mmcaddr + MMC_TX_MULTICOL_G);
	mmc->mmc_tx_deferred += readl(mmcaddr + MMC_TX_DEFERRED);
	mmc->mmc_tx_latecol += readl(mmcaddr + MMC_TX_LATECOL);
	mmc->mmc_tx_exesscol += readl(mmcaddr + MMC_TX_EXESSCOL);
	mmc->mmc_tx_carrier_error += readl(mmcaddr + MMC_TX_CARRIER_ERROR);
	mmc->mmc_tx_octetcount_g += readl(mmcaddr + MMC_TX_OCTETCOUNT_G);
	mmc->mmc_tx_framecount_g += readl(mmcaddr + MMC_TX_FRAMECOUNT_G);
	mmc->mmc_tx_excessdef += readl(mmcaddr + MMC_TX_EXCESSDEF);
	mmc->mmc_tx_pause_frame += readl(mmcaddr + MMC_TX_PAUSE_FRAME);
	mmc->mmc_tx_vlan_frame_g += readl(mmcaddr + MMC_TX_VLAN_FRAME_G);

	/* MMC RX counter registers */
	mmc->mmc_rx_framecount_gb += readl(mmcaddr + MMC_RX_FRAMECOUNT_GB);
	mmc->mmc_rx_octetcount_gb += readl(mmcaddr + MMC_RX_OCTETCOUNT_GB);
	mmc->mmc_rx_octetcount_g += readl(mmcaddr + MMC_RX_OCTETCOUNT_G);
	mmc->mmc_rx_broadcastframe_g += readl(mmcaddr +
					      MMC_RX_BROADCASTFRAME_G);
	mmc->mmc_rx_multicastframe_g += readl(mmcaddr +
					      MMC_RX_MULTICASTFRAME_G);
	mmc->mmc_rx_crc_error += readl(mmcaddr + MMC_RX_CRC_ERROR);
	mmc->mmc_rx_align_error += readl(mmcaddr + MMC_RX_ALIGN_ERROR);
	mmc->mmc_rx_run_error += readl(mmcaddr + MMC_RX_RUN_ERROR);
	mmc->mmc_rx_jabber_error += readl(mmcaddr + MMC_RX_JABBER_ERROR);
	mmc->mmc_rx_undersize_g += readl(mmcaddr + MMC_RX_UNDERSIZE_G);
	mmc->mmc_rx_oversize_g += readl(mmcaddr + MMC_RX_OVERSIZE_G);
	mmc->mmc_rx_64_octets_gb += readl(mmcaddr + MMC_RX_64_OCTETS_GB);
	mmc->mmc_rx_65_to_127_octets_gb +=
	    readl(mmcaddr + MMC_RX_65_TO_127_OCTETS_GB);
	mmc->mmc_rx_128_to_255_octets_gb +=
	    readl(mmcaddr + MMC_RX_128_TO_255_OCTETS_GB);
	mmc->mmc_rx_256_to_511_octets_gb +=
	    readl(mmcaddr + MMC_RX_256_TO_511_OCTETS_GB);
	mmc->mmc_rx_512_to_1023_octets_gb +=
	    readl(mmcaddr + MMC_RX_512_TO_1023_OCTETS_GB);
	mmc->mmc_rx_1024_to_max_octets_gb +=
	    readl(mmcaddr + MMC_RX_1024_TO_MAX_OCTETS_GB);
	mmc->mmc_rx_unicast_g += readl(mmcaddr + MMC_RX_UNICAST_G);
	mmc->mmc_rx_length_error += readl(mmcaddr + MMC_RX_LENGTH_ERROR);
	mmc->mmc_rx_autofrangetype += readl(mmcaddr + MMC_RX_AUTOFRANGETYPE);
	mmc->mmc_rx_pause_frames += readl(mmcaddr + MMC_RX_PAUSE_FRAMES);
	mmc->mmc_rx_fifo_overflow += readl(mmcaddr + MMC_RX_FIFO_OVERFLOW);
	mmc->mmc_rx_vlan_frames_gb += readl(mmcaddr + MMC_RX_VLAN_FRAMES_GB);
	mmc->mmc_rx_watchdog_error += readl(mmcaddr + MMC_RX_WATCHDOG_ERROR);
	/* IPC */
	mmc->mmc_rx_ipc_intr_mask += readl(mmcaddr + MMC_RX_IPC_INTR_MASK);
	mmc->mmc_rx_ipc_intr += readl(mmcaddr + MMC_RX_IPC_INTR);
	/* IPv4 */
	mmc->mmc_rx_ipv4_gd += readl(mmcaddr + MMC_RX_IPV4_GD);
	mmc->mmc_rx_ipv4_hderr += readl(mmcaddr + MMC_RX_IPV4_HDERR);
	mmc->mmc_rx_ipv4_nopay += readl(mmcaddr + MMC_RX_IPV4_NOPAY);
	mmc->mmc_rx_ipv4_frag += readl(mmcaddr + MMC_RX_IPV4_FRAG);
	mmc->mmc_rx_ipv4_udsbl += readl(mmcaddr + MMC_RX_IPV4_UDSBL);

	mmc->mmc_rx_ipv4_gd_octets += readl(mmcaddr + MMC_RX_IPV4_GD_OCTETS);
	mmc->mmc_rx_ipv4_hderr_octets +=
	    readl(mmcaddr + MMC_RX_IPV4_HDERR_OCTETS);
	mmc->mmc_rx_ipv4_nopay_octets +=
	    readl(mmcaddr + MMC_RX_IPV4_NOPAY_OCTETS);
	mmc->mmc_rx_ipv4_frag_octets += readl(mmcaddr +
					      MMC_RX_IPV4_FRAG_OCTETS);
	mmc->mmc_rx_ipv4_udsbl_octets +=
	    readl(mmcaddr + MMC_RX_IPV4_UDSBL_OCTETS);

	/* IPV6 */
	mmc->mmc_rx_ipv6_gd_octets += readl(mmcaddr + MMC_RX_IPV6_GD_OCTETS);
	mmc->mmc_rx_ipv6_hderr_octets +=
	    readl(mmcaddr + MMC_RX_IPV6_HDERR_OCTETS);
	mmc->mmc_rx_ipv6_nopay_octets +=
	    readl(mmcaddr + MMC_RX_IPV6_NOPAY_OCTETS);

	mmc->mmc_rx_ipv6_gd += readl(mmcaddr + MMC_RX_IPV6_GD);
	mmc->mmc_rx_ipv6_hderr += readl(mmcaddr + MMC_RX_IPV6_HDERR);
	mmc->mmc_rx_ipv6_nopay += readl(mmcaddr + MMC_RX_IPV6_NOPAY);

	/* Protocols */
	mmc->mmc_rx_udp_gd += readl(mmcaddr + MMC_RX_UDP_GD);
	mmc->mmc_rx_udp_err += readl(mmcaddr + MMC_RX_UDP_ERR);
	mmc->mmc_rx_tcp_gd += readl(mmcaddr + MMC_RX_TCP_GD);
	mmc->mmc_rx_tcp_err += readl(mmcaddr + MMC_RX_TCP_ERR);
	mmc->mmc_rx_icmp_gd += readl(mmcaddr + MMC_RX_ICMP_GD);
	mmc->mmc_rx_icmp_err += readl(mmcaddr + MMC_RX_ICMP_ERR);

	mmc->mmc_rx_udp_gd_octets += readl(mmcaddr + MMC_RX_UDP_GD_OCTETS);
	mmc->mmc_rx_udp_err_octets += readl(mmcaddr + MMC_RX_UDP_ERR_OCTETS);
	mmc->mmc_rx_tcp_gd_octets += readl(mmcaddr + MMC_RX_TCP_GD_OCTETS);
	mmc->mmc_rx_tcp_err_octets += readl(mmcaddr + MMC_RX_TCP_ERR_OCTETS);
	mmc->mmc_rx_icmp_gd_octets += readl(mmcaddr + MMC_RX_ICMP_GD_OCTETS);
	mmc->mmc_rx_icmp_err_octets += readl(mmcaddr + MMC_RX_ICMP_ERR_OCTETS);

	mmc->mmc_tx_fpe_fragment_cntr += readl(mmcaddr + MMC_TX_FPE_FRAG);
	mmc->mmc_tx_hold_req_cntr += readl(mmcaddr + MMC_TX_HOLD_REQ);
	mmc->mmc_rx_packet_assembly_err_cntr +=
		readl(mmcaddr + MMC_RX_PKT_ASSEMBLY_ERR);
	mmc->mmc_rx_packet_smd_err_cntr += readl(mmcaddr + MMC_RX_PKT_SMD_ERR);
	mmc->mmc_rx_packet_assembly_ok_cntr +=
		readl(mmcaddr + MMC_RX_PKT_ASSEMBLY_OK);
	mmc->mmc_rx_fpe_fragment_cntr += readl(mmcaddr + MMC_RX_FPE_FRAG);
}

const struct stmmac_mmc_ops dwmac_mmc_ops = {
	.ctrl = dwmac_mmc_ctrl,
	.intr_all_mask = dwmac_mmc_intr_all_mask,
	.read = dwmac_mmc_read,
};

static void dwxgmac_mmc_ctrl(void __iomem *mmcaddr, unsigned int mode)
{
	u32 value = readl(mmcaddr + MMC_CNTRL);

	value |= (mode & 0x3F);

	writel(value, mmcaddr + MMC_CNTRL);
}

static void dwxgmac_mmc_intr_all_mask(void __iomem *mmcaddr)
{
	writel(0x0, mmcaddr + MMC_RX_INTR_MASK);
	writel(0x0, mmcaddr + MMC_TX_INTR_MASK);
	writel(MMC_DEFAULT_MASK, mmcaddr + MMC_XGMAC_RX_IPC_INTR_MASK);
}

static void dwxgmac_read_mmc_reg(void __iomem *addr, u32 reg, u32 *dest)
{
	u64 tmp = 0;

	tmp += readl(addr + reg);
	tmp += ((u64 )readl(addr + reg + 0x4)) << 32;
	if (tmp > GENMASK(31, 0))
		*dest = ~0x0;
	else
		*dest = *dest + tmp;
}

/* This reads the MAC core counters (if actaully supported).
 * by default the MMC core is programmed to reset each
 * counter after a read. So all the field of the mmc struct
 * have to be incremented.
 */
static void dwxgmac_mmc_read(void __iomem *mmcaddr, struct stmmac_counters *mmc)
{
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_OCTET_GB,
			     &mmc->mmc_tx_octetcount_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_PKT_GB,
			     &mmc->mmc_tx_framecount_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_BROAD_PKT_G,
			     &mmc->mmc_tx_broadcastframe_g);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_MULTI_PKT_G,
			     &mmc->mmc_tx_multicastframe_g);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_64OCT_GB,
			     &mmc->mmc_tx_64_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_65OCT_GB,
			     &mmc->mmc_tx_65_to_127_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_128OCT_GB,
			     &mmc->mmc_tx_128_to_255_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_256OCT_GB,
			     &mmc->mmc_tx_256_to_511_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_512OCT_GB,
			     &mmc->mmc_tx_512_to_1023_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_1024OCT_GB,
			     &mmc->mmc_tx_1024_to_max_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_UNI_PKT_GB,
			     &mmc->mmc_tx_unicast_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_MULTI_PKT_GB,
			     &mmc->mmc_tx_multicast_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_BROAD_PKT_GB,
			     &mmc->mmc_tx_broadcast_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_UNDER,
			     &mmc->mmc_tx_underflow_error);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_OCTET_G,
			     &mmc->mmc_tx_octetcount_g);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_PKT_G,
			     &mmc->mmc_tx_framecount_g);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_PAUSE,
			     &mmc->mmc_tx_pause_frame);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_TX_VLAN_PKT_G,
			     &mmc->mmc_tx_vlan_frame_g);

	/* MMC RX counter registers */
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_PKT_GB,
			     &mmc->mmc_rx_framecount_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_OCTET_GB,
			     &mmc->mmc_rx_octetcount_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_OCTET_G,
			     &mmc->mmc_rx_octetcount_g);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_BROAD_PKT_G,
			     &mmc->mmc_rx_broadcastframe_g);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_MULTI_PKT_G,
			     &mmc->mmc_rx_multicastframe_g);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_CRC_ERR,
			     &mmc->mmc_rx_crc_error);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_CRC_ERR,
			     &mmc->mmc_rx_crc_error);
	mmc->mmc_rx_run_error += readl(mmcaddr + MMC_XGMAC_RX_RUNT_ERR);
	mmc->mmc_rx_jabber_error += readl(mmcaddr + MMC_XGMAC_RX_JABBER_ERR);
	mmc->mmc_rx_undersize_g += readl(mmcaddr + MMC_XGMAC_RX_UNDER);
	mmc->mmc_rx_oversize_g += readl(mmcaddr + MMC_XGMAC_RX_OVER);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_64OCT_GB,
			     &mmc->mmc_rx_64_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_65OCT_GB,
			     &mmc->mmc_rx_65_to_127_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_128OCT_GB,
			     &mmc->mmc_rx_128_to_255_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_256OCT_GB,
			     &mmc->mmc_rx_256_to_511_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_512OCT_GB,
			     &mmc->mmc_rx_512_to_1023_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_1024OCT_GB,
			     &mmc->mmc_rx_1024_to_max_octets_gb);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_UNI_PKT_G,
			     &mmc->mmc_rx_unicast_g);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_LENGTH_ERR,
			     &mmc->mmc_rx_length_error);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_RANGE,
			     &mmc->mmc_rx_autofrangetype);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_PAUSE,
			     &mmc->mmc_rx_pause_frames);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_FIFOOVER_PKT,
			     &mmc->mmc_rx_fifo_overflow);
	dwxgmac_read_mmc_reg(mmcaddr, MMC_XGMAC_RX_VLAN_PKT_GB,
			     &mmc->mmc_rx_vlan_frames_gb);
	mmc->mmc_rx_watchdog_error += readl(mmcaddr + MMC_XGMAC_RX_WATCHDOG_ERR);

	mmc->mmc_tx_fpe_fragment_cntr += readl(mmcaddr + MMC_XGMAC_TX_FPE_FRAG);
	mmc->mmc_tx_hold_req_cntr += readl(mmcaddr + MMC_XGMAC_TX_HOLD_REQ);
	mmc->mmc_rx_packet_assembly_err_cntr +=
		readl(mmcaddr + MMC_XGMAC_RX_PKT_ASSEMBLY_ERR);
	mmc->mmc_rx_packet_smd_err_cntr +=
		readl(mmcaddr + MMC_XGMAC_RX_PKT_SMD_ERR);
	mmc->mmc_rx_packet_assembly_ok_cntr +=
		readl(mmcaddr + MMC_XGMAC_RX_PKT_ASSEMBLY_OK);
	mmc->mmc_rx_fpe_fragment_cntr +=
		readl(mmcaddr + MMC_XGMAC_RX_FPE_FRAG);
}

const struct stmmac_mmc_ops dwxgmac_mmc_ops = {
	.ctrl = dwxgmac_mmc_ctrl,
	.intr_all_mask = dwxgmac_mmc_intr_all_mask,
	.read = dwxgmac_mmc_read,
};
