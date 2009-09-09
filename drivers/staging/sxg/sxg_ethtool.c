/**************************************************************************
 *
 * Copyright (C) 2000-2008 Alacritech, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of Alacritech, Inc.
 *
 **************************************************************************/

/*
 * FILENAME: sxg_ethtool.c
 *
 * The ethtool support for SXG driver for Alacritech's 10Gbe products.
 *
 * NOTE: This is the standard, non-accelerated version of Alacritech's
 *       IS-NIC driver.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/pci.h>

#include "sxg_os.h"
#include "sxghw.h"
#include "sxghif.h"
#include "sxg.h"

struct sxg_nic_stats {
        char stat_string[ETH_GSTRING_LEN];
        int sizeof_stat;
        int stat_offset;
};

#define SXG_NIC_STATS(m) sizeof(((struct adapter_t *)0)->m), \
				offsetof(struct adapter_t, m)

#define USER_VIEWABLE_EEPROM_SIZE	28

static struct sxg_nic_stats sxg_nic_gstrings_stats[] = {
	{"xmit_ring_0_full", SXG_NIC_STATS(Stats.XmtZeroFull)},

	/* May be will need in future */
/*	{"dumb_xmit_broadcast_packets", SXG_NIC_STATS(Stats.DumbXmtBcastPkts)},
	{"dumb_xmit_broadcast_bytes", SXG_NIC_STATS(Stats.DumbXmtBcastBytes)},
	{"dumb_xmit_unicast_packets", SXG_NIC_STATS(Stats.DumbXmtUcastPkts)},
	{"dumb_xmit_unicast_bytes", SXG_NIC_STATS(Stats.DumbXmtUcastBytes)},
*/
	{"xmit_queue_length", SXG_NIC_STATS(Stats.XmtQLen)},
	{"memory_allocation_failure", SXG_NIC_STATS(Stats.NoMem)},
	{"Interrupts", SXG_NIC_STATS(Stats.NumInts)},
	{"false_interrupts", SXG_NIC_STATS(Stats.FalseInts)},
	{"processed_data_queue_full", SXG_NIC_STATS(Stats.PdqFull)},
	{"event_ring_full", SXG_NIC_STATS(Stats.EventRingFull)},
	{"transport_checksum_error", SXG_NIC_STATS(Stats.TransportCsum)},
	{"transport_underflow_error", SXG_NIC_STATS(Stats.TransportUflow)},
	{"transport_header_length_error", SXG_NIC_STATS(Stats.TransportHdrLen)},
	{"network_checksum_error", SXG_NIC_STATS(Stats.NetworkCsum)},
	{"network_underflow_error", SXG_NIC_STATS(Stats.NetworkUflow)},
	{"network_header_length_error", SXG_NIC_STATS(Stats.NetworkHdrLen)},
	{"receive_parity_error", SXG_NIC_STATS(Stats.Parity)},
	{"link_parity_error", SXG_NIC_STATS(Stats.LinkParity)},
	{"link/data early_error", SXG_NIC_STATS(Stats.LinkEarly)},
	{"buffer_overflow_error", SXG_NIC_STATS(Stats.LinkBufOflow)},
	{"link_code_error", SXG_NIC_STATS(Stats.LinkCode)},
	{"dribble nibble", SXG_NIC_STATS(Stats.LinkDribble)},
	{"CRC_error", SXG_NIC_STATS(Stats.LinkCrc)},
	{"link_overflow_error", SXG_NIC_STATS(Stats.LinkOflow)},
	{"link_underflow_error", SXG_NIC_STATS(Stats.LinkUflow)},

	/* May be need in future */
/*	{"dumb_rcv_broadcast_packets", SXG_NIC_STATS(Stats.DumbRcvBcastPkts)},
	{"dumb_rcv_broadcast_bytes", SXG_NIC_STATS(Stats.DumbRcvBcastBytes)},
*/	{"dumb_rcv_multicast_packets", SXG_NIC_STATS(Stats.DumbRcvMcastPkts)},
	{"dumb_rcv_multicast_bytes", SXG_NIC_STATS(Stats.DumbRcvMcastBytes)},
/*	{"dumb_rcv_unicast_packets", SXG_NIC_STATS(Stats.DumbRcvUcastPkts)},
	{"dumb_rcv_unicast_bytes", SXG_NIC_STATS(Stats.DumbRcvUcastBytes)},
*/
	{"no_sgl_buffer", SXG_NIC_STATS(Stats.NoSglBuf)},
};

#define SXG_NIC_STATS_LEN	ARRAY_SIZE(sxg_nic_gstrings_stats)

static inline void sxg_reg32_write(void __iomem *reg, u32 value, bool flush)
{
        writel(value, reg);
        if (flush)
                mb();
}

static inline void sxg_reg64_write(struct adapter_t *adapter, void __iomem *reg,
                                   u64 value, u32 cpu)
{
        u32 value_high = (u32) (value >> 32);
        u32 value_low = (u32) (value & 0x00000000FFFFFFFF);
        unsigned long flags;

        spin_lock_irqsave(&adapter->Bit64RegLock, flags);
        writel(value_high, (void __iomem *)(&adapter->UcodeRegs[cpu].Upper));
        writel(value_low, reg);
        spin_unlock_irqrestore(&adapter->Bit64RegLock, flags);
}

static void
sxg_nic_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *drvinfo)
{
	struct adapter_t *adapter = netdev_priv(dev);
	strncpy(drvinfo->driver, sxg_driver_name, 32);
	strncpy(drvinfo->version, SXG_DRV_VERSION, 32);
//	strncpy(drvinfo->fw_version, SAHARA_UCODE_VERS_STRING, 32);
	strncpy(drvinfo->bus_info, pci_name(adapter->pcidev), 32);
	/* TODO : Read the major and minor number of firmware. Is this
 	 * from the FLASH/EEPROM or download file ?
 	 */
	/* LINSYS : Check if this is correct or if not find the right value
 	 * Also check what is the right EEPROM length : EEPROM_SIZE_XFMR or EEPROM_SIZE_NO_XFMR
 	 */
}

static int sxg_nic_set_settings(struct net_device *netdev,
                              struct ethtool_cmd *ecmd)
{
	/* No settings are applicable as we support only 10Gb/FIBRE_media */
	return -EOPNOTSUPP;
}

static void
sxg_nic_get_strings(struct net_device *netdev, u32 stringset, u8 * data)
{
	int index;

	switch(stringset) {
	case ETH_SS_TEST:
		break;
	case ETH_SS_STATS:
		for (index = 0; index < SXG_NIC_STATS_LEN; index++) {
                	memcpy(data + index * ETH_GSTRING_LEN,
                        	sxg_nic_gstrings_stats[index].stat_string,
                               	ETH_GSTRING_LEN);
                }
                break;
	}
}

static void
sxg_nic_get_ethtool_stats(struct net_device *netdev,
			struct ethtool_stats *stats, u64 * data)
{
        struct adapter_t *adapter = netdev_priv(netdev);
        int index;
        for (index = 0; index < SXG_NIC_STATS_LEN; index++) {
                char *p = (char *)adapter +
				sxg_nic_gstrings_stats[index].stat_offset;
                data[index] = (sxg_nic_gstrings_stats[index].sizeof_stat ==
		                     sizeof(u64)) ? *(u64 *) p : *(u32 *) p;
        }
}

static int sxg_nic_get_sset_count(struct net_device *netdev, int sset)
{
        switch (sset) {
        case ETH_SS_STATS:
       		return SXG_NIC_STATS_LEN;
	default:
                return -EOPNOTSUPP;
        }
}

static int sxg_nic_get_settings(struct net_device *netdev,
				struct ethtool_cmd *ecmd)
{
	struct adapter_t *adapter = netdev_priv(netdev);

        ecmd->supported = SUPPORTED_10000baseT_Full;
        ecmd->autoneg = AUTONEG_ENABLE;		//VSS check This
        ecmd->transceiver = XCVR_EXTERNAL;	//VSS check This

	/* For Fibre Channel */
	ecmd->supported |= SUPPORTED_FIBRE;
        ecmd->advertising = (ADVERTISED_10000baseT_Full |
                                ADVERTISED_FIBRE);
	ecmd->port = PORT_FIBRE;


	/* Link Speed */
	if(adapter->LinkState & SXG_LINK_UP) {
		ecmd->speed = SPEED_10000;	//adapter->LinkSpeed;
		ecmd->duplex = DUPLEX_FULL;
	}
	return 0;
}

static u32 sxg_nic_get_rx_csum(struct net_device *netdev)
{
	struct adapter_t *adapter = netdev_priv(netdev);
	return ((adapter->flags & SXG_RCV_IP_CSUM_ENABLED) &&
		 (adapter->flags & SXG_RCV_TCP_CSUM_ENABLED));
}

static int sxg_nic_set_rx_csum(struct net_device *netdev, u32 data)
{
	struct adapter_t *adapter = netdev_priv(netdev);
	if (data)
		adapter->flags |= SXG_RCV_IP_CSUM_ENABLED;
	else
		adapter->flags &= ~SXG_RCV_IP_CSUM_ENABLED;
	/*
	 * We dont need to write to the card to do checksums.
	 * It does it anyways.
	 */
	return 0;
}

static int sxg_nic_get_regs_len(struct net_device *dev)
{
	return (SXG_HWREG_MEMSIZE + SXG_UCODEREG_MEMSIZE);
}

static void sxg_nic_get_regs(struct net_device *netdev,
			struct ethtool_regs *regs, void *p)
{
	struct adapter_t *adapter = netdev_priv(netdev);
	struct sxg_hw_regs *HwRegs = adapter->HwRegs;
	struct sxg_ucode_regs *UcodeRegs = adapter->UcodeRegs;
	u32 *buff = p;

	memset(p, 0, (sizeof(struct sxg_hw_regs)+sizeof(struct sxg_ucode_regs)));
	memcpy(buff, HwRegs, sizeof(struct sxg_hw_regs));
	memcpy((buff+sizeof(struct sxg_hw_regs)), UcodeRegs, sizeof(struct sxg_ucode_regs));
}

static int sxg_nic_get_eeprom_len(struct net_device *netdev)
{
	return (USER_VIEWABLE_EEPROM_SIZE);
}

static int sxg_nic_get_eeprom(struct net_device *netdev,
				struct ethtool_eeprom *eeprom, u8 *bytes)
{
	struct adapter_t *adapter = netdev_priv(netdev);
	struct sw_cfg_data *data;
	unsigned long           i, status;
	dma_addr_t p_addr;

	data = pci_alloc_consistent(adapter->pcidev, sizeof(struct sw_cfg_data),
					 &p_addr);
	if(!data) {
                /*
		 * We cant get even this much memory. Raise a hell
                 * Get out of here
                 */
                printk(KERN_ERR"%s : Could not allocate memory for reading \
                                EEPROM\n", __func__);
                return -ENOMEM;
        }

        WRITE_REG(adapter->UcodeRegs[0].ConfigStat, SXG_CFG_TIMEOUT, TRUE);
        WRITE_REG64(adapter, adapter->UcodeRegs[0].Config, p_addr, 0);
	for(i=0; i<1000; i++) {
                READ_REG(adapter->UcodeRegs[0].ConfigStat, status);
                if (status != SXG_CFG_TIMEOUT)
                        break;
                mdelay(1);      /* Do we really need this */
        }

	memset(bytes, 0, eeprom->len);
        memcpy(bytes, data->MacAddr[0].MacAddr, sizeof(struct sxg_config_mac));
        memcpy(bytes+6, data->AtkFru.PartNum, 6);
        memcpy(bytes+12, data->AtkFru.Revision, 2);
        memcpy(bytes+14, data->AtkFru.Serial, 14);

	return 0;
}

struct ethtool_ops sxg_nic_ethtool_ops = {
	.get_settings = sxg_nic_get_settings,
	.set_settings = sxg_nic_set_settings,
	.get_drvinfo = sxg_nic_get_drvinfo,
	.get_regs_len = sxg_nic_get_regs_len,
	.get_regs = sxg_nic_get_regs,
	.get_link = ethtool_op_get_link,
//	.get_wol = sxg_nic_get_wol,
	.get_eeprom_len = sxg_nic_get_eeprom_len,
	.get_eeprom = sxg_nic_get_eeprom,
//	.get_pauseparam = sxg_nic_get_pauseparam,
//	.set_pauseparam = sxg_nic_set_pauseparam,
	.set_tx_csum = ethtool_op_set_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
//	.get_tso = sxg_nic_get_tso,
//	.set_tso = sxg_nic_set_tso,
//	.self_test = sxg_nic_diag_test,
	.get_strings = sxg_nic_get_strings,
	.get_ethtool_stats = sxg_nic_get_ethtool_stats,
	.get_sset_count = sxg_nic_get_sset_count,
	.get_rx_csum = sxg_nic_get_rx_csum,
	.set_rx_csum = sxg_nic_set_rx_csum,
//	.get_coalesce = sxg_nic_get_intr_coalesce,
//	.set_coalesce = sxg_nic_set_intr_coalesce,
};
