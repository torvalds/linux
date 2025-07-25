// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/* Copyright (C) 2018 KVASER AB, Sweden. All rights reserved.
 * Parts of this driver are based on the following:
 *  - Kvaser linux pciefd driver (version 5.42)
 *  - PEAK linux canfd driver
 */

#include "kvaser_pciefd.h"

#include <linux/bitfield.h>
#include <linux/can/dev.h>
#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <net/netdev_queues.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Kvaser AB <support@kvaser.com>");
MODULE_DESCRIPTION("CAN driver for Kvaser CAN/PCIe devices");

#define KVASER_PCIEFD_DRV_NAME "kvaser_pciefd"

#define KVASER_PCIEFD_WAIT_TIMEOUT msecs_to_jiffies(1000)
#define KVASER_PCIEFD_BEC_POLL_FREQ (jiffies + msecs_to_jiffies(200))
#define KVASER_PCIEFD_MAX_ERR_REP 256U

#define KVASER_PCIEFD_VENDOR 0x1a07

/* Altera based devices */
#define KVASER_PCIEFD_4HS_DEVICE_ID 0x000d
#define KVASER_PCIEFD_2HS_V2_DEVICE_ID 0x000e
#define KVASER_PCIEFD_HS_V2_DEVICE_ID 0x000f
#define KVASER_PCIEFD_MINIPCIE_HS_V2_DEVICE_ID 0x0010
#define KVASER_PCIEFD_MINIPCIE_2HS_V2_DEVICE_ID 0x0011

/* SmartFusion2 based devices */
#define KVASER_PCIEFD_2CAN_V3_DEVICE_ID 0x0012
#define KVASER_PCIEFD_1CAN_V3_DEVICE_ID 0x0013
#define KVASER_PCIEFD_4CAN_V2_DEVICE_ID 0x0014
#define KVASER_PCIEFD_MINIPCIE_2CAN_V3_DEVICE_ID 0x0015
#define KVASER_PCIEFD_MINIPCIE_1CAN_V3_DEVICE_ID 0x0016

/* Xilinx based devices */
#define KVASER_PCIEFD_M2_4CAN_DEVICE_ID 0x0017
#define KVASER_PCIEFD_8CAN_DEVICE_ID 0x0019

/* Altera SerDes Enable 64-bit DMA address translation */
#define KVASER_PCIEFD_ALTERA_DMA_64BIT BIT(0)

/* SmartFusion2 SerDes LSB address translation mask */
#define KVASER_PCIEFD_SF2_DMA_LSB_MASK GENMASK(31, 12)

/* Xilinx SerDes LSB address translation mask */
#define KVASER_PCIEFD_XILINX_DMA_LSB_MASK GENMASK(31, 12)

/* Kvaser KCAN CAN controller registers */
#define KVASER_PCIEFD_KCAN_FIFO_REG 0x100
#define KVASER_PCIEFD_KCAN_FIFO_LAST_REG 0x180
#define KVASER_PCIEFD_KCAN_CTRL_REG 0x2c0
#define KVASER_PCIEFD_KCAN_CMD_REG 0x400
#define KVASER_PCIEFD_KCAN_IOC_REG 0x404
#define KVASER_PCIEFD_KCAN_IEN_REG 0x408
#define KVASER_PCIEFD_KCAN_IRQ_REG 0x410
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG 0x414
#define KVASER_PCIEFD_KCAN_STAT_REG 0x418
#define KVASER_PCIEFD_KCAN_MODE_REG 0x41c
#define KVASER_PCIEFD_KCAN_BTRN_REG 0x420
#define KVASER_PCIEFD_KCAN_BUS_LOAD_REG 0x424
#define KVASER_PCIEFD_KCAN_BTRD_REG 0x428
#define KVASER_PCIEFD_KCAN_PWM_REG 0x430
/* System identification and information registers */
#define KVASER_PCIEFD_SYSID_VERSION_REG 0x8
#define KVASER_PCIEFD_SYSID_CANFREQ_REG 0xc
#define KVASER_PCIEFD_SYSID_BUSFREQ_REG 0x10
#define KVASER_PCIEFD_SYSID_BUILD_REG 0x14
/* Shared receive buffer FIFO registers */
#define KVASER_PCIEFD_SRB_FIFO_LAST_REG 0x1f4
/* Shared receive buffer registers */
#define KVASER_PCIEFD_SRB_CMD_REG 0x0
#define KVASER_PCIEFD_SRB_IEN_REG 0x04
#define KVASER_PCIEFD_SRB_IRQ_REG 0x0c
#define KVASER_PCIEFD_SRB_STAT_REG 0x10
#define KVASER_PCIEFD_SRB_RX_NR_PACKETS_REG 0x14
#define KVASER_PCIEFD_SRB_CTRL_REG 0x18

/* System build information fields */
#define KVASER_PCIEFD_SYSID_VERSION_NR_CHAN_MASK GENMASK(31, 24)
#define KVASER_PCIEFD_SYSID_VERSION_MAJOR_MASK GENMASK(23, 16)
#define KVASER_PCIEFD_SYSID_VERSION_MINOR_MASK GENMASK(7, 0)
#define KVASER_PCIEFD_SYSID_BUILD_SEQ_MASK GENMASK(15, 1)

/* Reset DMA buffer 0, 1 and FIFO offset */
#define KVASER_PCIEFD_SRB_CMD_RDB1 BIT(5)
#define KVASER_PCIEFD_SRB_CMD_RDB0 BIT(4)
#define KVASER_PCIEFD_SRB_CMD_FOR BIT(0)

/* DMA underflow, buffer 0 and 1 */
#define KVASER_PCIEFD_SRB_IRQ_DUF1 BIT(13)
#define KVASER_PCIEFD_SRB_IRQ_DUF0 BIT(12)
/* DMA overflow, buffer 0 and 1 */
#define KVASER_PCIEFD_SRB_IRQ_DOF1 BIT(11)
#define KVASER_PCIEFD_SRB_IRQ_DOF0 BIT(10)
/* DMA packet done, buffer 0 and 1 */
#define KVASER_PCIEFD_SRB_IRQ_DPD1 BIT(9)
#define KVASER_PCIEFD_SRB_IRQ_DPD0 BIT(8)

/* Got DMA support */
#define KVASER_PCIEFD_SRB_STAT_DMA BIT(24)
/* DMA idle */
#define KVASER_PCIEFD_SRB_STAT_DI BIT(15)

/* SRB current packet level */
#define KVASER_PCIEFD_SRB_RX_NR_PACKETS_MASK GENMASK(7, 0)

/* DMA Enable */
#define KVASER_PCIEFD_SRB_CTRL_DMA_ENABLE BIT(0)

/* KCAN CTRL packet types */
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_MASK GENMASK(31, 29)
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_EFLUSH 0x4
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_EFRAME 0x5

/* Command sequence number */
#define KVASER_PCIEFD_KCAN_CMD_SEQ_MASK GENMASK(23, 16)
/* Command bits */
#define KVASER_PCIEFD_KCAN_CMD_MASK GENMASK(5, 0)
/* Abort, flush and reset */
#define KVASER_PCIEFD_KCAN_CMD_AT BIT(1)
/* Request status packet */
#define KVASER_PCIEFD_KCAN_CMD_SRQ BIT(0)

/* Control CAN LED, active low */
#define KVASER_PCIEFD_KCAN_IOC_LED BIT(0)

/* Transmitter unaligned */
#define KVASER_PCIEFD_KCAN_IRQ_TAL BIT(17)
/* Tx FIFO empty */
#define KVASER_PCIEFD_KCAN_IRQ_TE BIT(16)
/* Tx FIFO overflow */
#define KVASER_PCIEFD_KCAN_IRQ_TOF BIT(15)
/* Tx buffer flush done */
#define KVASER_PCIEFD_KCAN_IRQ_TFD BIT(14)
/* Abort done */
#define KVASER_PCIEFD_KCAN_IRQ_ABD BIT(13)
/* Rx FIFO overflow */
#define KVASER_PCIEFD_KCAN_IRQ_ROF BIT(5)
/* FDF bit when controller is in classic CAN mode */
#define KVASER_PCIEFD_KCAN_IRQ_FDIC BIT(3)
/* Bus parameter protection error */
#define KVASER_PCIEFD_KCAN_IRQ_BPP BIT(2)
/* Tx FIFO unaligned end */
#define KVASER_PCIEFD_KCAN_IRQ_TAE BIT(1)
/* Tx FIFO unaligned read */
#define KVASER_PCIEFD_KCAN_IRQ_TAR BIT(0)

/* Tx FIFO size */
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_MAX_MASK GENMASK(23, 16)
/* Tx FIFO current packet level */
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_CURRENT_MASK GENMASK(7, 0)

/* Current status packet sequence number */
#define KVASER_PCIEFD_KCAN_STAT_SEQNO_MASK GENMASK(31, 24)
/* Controller got CAN FD capability */
#define KVASER_PCIEFD_KCAN_STAT_FD BIT(19)
/* Controller got one-shot capability */
#define KVASER_PCIEFD_KCAN_STAT_CAP BIT(16)
/* Controller in reset mode */
#define KVASER_PCIEFD_KCAN_STAT_IRM BIT(15)
/* Reset mode request */
#define KVASER_PCIEFD_KCAN_STAT_RMR BIT(14)
/* Bus off */
#define KVASER_PCIEFD_KCAN_STAT_BOFF BIT(11)
/* Idle state. Controller in reset mode and no abort or flush pending */
#define KVASER_PCIEFD_KCAN_STAT_IDLE BIT(10)
/* Abort request */
#define KVASER_PCIEFD_KCAN_STAT_AR BIT(7)
/* Controller is bus off */
#define KVASER_PCIEFD_KCAN_STAT_BUS_OFF_MASK \
	(KVASER_PCIEFD_KCAN_STAT_AR | KVASER_PCIEFD_KCAN_STAT_BOFF | \
	 KVASER_PCIEFD_KCAN_STAT_RMR | KVASER_PCIEFD_KCAN_STAT_IRM)

/* Classic CAN mode */
#define KVASER_PCIEFD_KCAN_MODE_CCM BIT(31)
/* Active error flag enable. Clear to force error passive */
#define KVASER_PCIEFD_KCAN_MODE_EEN BIT(23)
/* Acknowledgment packet type */
#define KVASER_PCIEFD_KCAN_MODE_APT BIT(20)
/* CAN FD non-ISO */
#define KVASER_PCIEFD_KCAN_MODE_NIFDEN BIT(15)
/* Error packet enable */
#define KVASER_PCIEFD_KCAN_MODE_EPEN BIT(12)
/* Listen only mode */
#define KVASER_PCIEFD_KCAN_MODE_LOM BIT(9)
/* Reset mode */
#define KVASER_PCIEFD_KCAN_MODE_RM BIT(8)

/* BTRN and BTRD fields */
#define KVASER_PCIEFD_KCAN_BTRN_TSEG2_MASK GENMASK(30, 26)
#define KVASER_PCIEFD_KCAN_BTRN_TSEG1_MASK GENMASK(25, 17)
#define KVASER_PCIEFD_KCAN_BTRN_SJW_MASK GENMASK(16, 13)
#define KVASER_PCIEFD_KCAN_BTRN_BRP_MASK GENMASK(12, 0)

/* PWM Control fields */
#define KVASER_PCIEFD_KCAN_PWM_TOP_MASK GENMASK(23, 16)
#define KVASER_PCIEFD_KCAN_PWM_TRIGGER_MASK GENMASK(7, 0)

/* KCAN packet type IDs */
#define KVASER_PCIEFD_PACK_TYPE_DATA 0x0
#define KVASER_PCIEFD_PACK_TYPE_ACK 0x1
#define KVASER_PCIEFD_PACK_TYPE_TXRQ 0x2
#define KVASER_PCIEFD_PACK_TYPE_ERROR 0x3
#define KVASER_PCIEFD_PACK_TYPE_EFLUSH_ACK 0x4
#define KVASER_PCIEFD_PACK_TYPE_EFRAME_ACK 0x5
#define KVASER_PCIEFD_PACK_TYPE_ACK_DATA 0x6
#define KVASER_PCIEFD_PACK_TYPE_STATUS 0x8
#define KVASER_PCIEFD_PACK_TYPE_BUS_LOAD 0x9

/* Common KCAN packet definitions, second word */
#define KVASER_PCIEFD_PACKET_TYPE_MASK GENMASK(31, 28)
#define KVASER_PCIEFD_PACKET_CHID_MASK GENMASK(27, 25)
#define KVASER_PCIEFD_PACKET_SEQ_MASK GENMASK(7, 0)

/* KCAN Transmit/Receive data packet, first word */
#define KVASER_PCIEFD_RPACKET_IDE BIT(30)
#define KVASER_PCIEFD_RPACKET_RTR BIT(29)
#define KVASER_PCIEFD_RPACKET_ID_MASK GENMASK(28, 0)
/* KCAN Transmit data packet, second word */
#define KVASER_PCIEFD_TPACKET_AREQ BIT(31)
#define KVASER_PCIEFD_TPACKET_SMS BIT(16)
/* KCAN Transmit/Receive data packet, second word */
#define KVASER_PCIEFD_RPACKET_FDF BIT(15)
#define KVASER_PCIEFD_RPACKET_BRS BIT(14)
#define KVASER_PCIEFD_RPACKET_ESI BIT(13)
#define KVASER_PCIEFD_RPACKET_DLC_MASK GENMASK(11, 8)

/* KCAN Transmit acknowledge packet, first word */
#define KVASER_PCIEFD_APACKET_NACK BIT(11)
#define KVASER_PCIEFD_APACKET_ABL BIT(10)
#define KVASER_PCIEFD_APACKET_CT BIT(9)
#define KVASER_PCIEFD_APACKET_FLU BIT(8)

/* KCAN Status packet, first word */
#define KVASER_PCIEFD_SPACK_RMCD BIT(22)
#define KVASER_PCIEFD_SPACK_IRM BIT(21)
#define KVASER_PCIEFD_SPACK_IDET BIT(20)
#define KVASER_PCIEFD_SPACK_BOFF BIT(16)
#define KVASER_PCIEFD_SPACK_RXERR_MASK GENMASK(15, 8)
#define KVASER_PCIEFD_SPACK_TXERR_MASK GENMASK(7, 0)
/* KCAN Status packet, second word */
#define KVASER_PCIEFD_SPACK_EPLR BIT(24)
#define KVASER_PCIEFD_SPACK_EWLR BIT(23)
#define KVASER_PCIEFD_SPACK_AUTO BIT(21)

/* KCAN Error detected packet, second word */
#define KVASER_PCIEFD_EPACK_DIR_TX BIT(0)

/* Macros for calculating addresses of registers */
#define KVASER_PCIEFD_GET_BLOCK_ADDR(pcie, block) \
	((pcie)->reg_base + (pcie)->driver_data->address_offset->block)
#define KVASER_PCIEFD_PCI_IEN_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), pci_ien))
#define KVASER_PCIEFD_PCI_IRQ_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), pci_irq))
#define KVASER_PCIEFD_SERDES_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), serdes))
#define KVASER_PCIEFD_SYSID_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), sysid))
#define KVASER_PCIEFD_LOOPBACK_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), loopback))
#define KVASER_PCIEFD_SRB_FIFO_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), kcan_srb_fifo))
#define KVASER_PCIEFD_SRB_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), kcan_srb))
#define KVASER_PCIEFD_KCAN_CH0_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), kcan_ch0))
#define KVASER_PCIEFD_KCAN_CH1_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), kcan_ch1))
#define KVASER_PCIEFD_KCAN_CHANNEL_SPAN(pcie) \
	(KVASER_PCIEFD_KCAN_CH1_ADDR((pcie)) - KVASER_PCIEFD_KCAN_CH0_ADDR((pcie)))
#define KVASER_PCIEFD_KCAN_CHX_ADDR(pcie, i) \
	(KVASER_PCIEFD_KCAN_CH0_ADDR((pcie)) + (i) * KVASER_PCIEFD_KCAN_CHANNEL_SPAN((pcie)))

struct kvaser_pciefd;
static void kvaser_pciefd_write_dma_map_altera(struct kvaser_pciefd *pcie,
					       dma_addr_t addr, int index);
static void kvaser_pciefd_write_dma_map_sf2(struct kvaser_pciefd *pcie,
					    dma_addr_t addr, int index);
static void kvaser_pciefd_write_dma_map_xilinx(struct kvaser_pciefd *pcie,
					       dma_addr_t addr, int index);

static const struct kvaser_pciefd_address_offset kvaser_pciefd_altera_address_offset = {
	.serdes = 0x1000,
	.pci_ien = 0x50,
	.pci_irq = 0x40,
	.sysid = 0x1f020,
	.loopback = 0x1f000,
	.kcan_srb_fifo = 0x1f200,
	.kcan_srb = 0x1f400,
	.kcan_ch0 = 0x10000,
	.kcan_ch1 = 0x11000,
};

static const struct kvaser_pciefd_address_offset kvaser_pciefd_sf2_address_offset = {
	.serdes = 0x280c8,
	.pci_ien = 0x102004,
	.pci_irq = 0x102008,
	.sysid = 0x100000,
	.loopback = 0x103000,
	.kcan_srb_fifo = 0x120000,
	.kcan_srb = 0x121000,
	.kcan_ch0 = 0x140000,
	.kcan_ch1 = 0x142000,
};

static const struct kvaser_pciefd_address_offset kvaser_pciefd_xilinx_address_offset = {
	.serdes = 0x00208,
	.pci_ien = 0x102004,
	.pci_irq = 0x102008,
	.sysid = 0x100000,
	.loopback = 0x103000,
	.kcan_srb_fifo = 0x120000,
	.kcan_srb = 0x121000,
	.kcan_ch0 = 0x140000,
	.kcan_ch1 = 0x142000,
};

static const struct kvaser_pciefd_irq_mask kvaser_pciefd_altera_irq_mask = {
	.kcan_rx0 = BIT(4),
	.kcan_tx = { BIT(0), BIT(1), BIT(2), BIT(3) },
	.all = GENMASK(4, 0),
};

static const struct kvaser_pciefd_irq_mask kvaser_pciefd_sf2_irq_mask = {
	.kcan_rx0 = BIT(4),
	.kcan_tx = { BIT(16), BIT(17), BIT(18), BIT(19) },
	.all = GENMASK(19, 16) | BIT(4),
};

static const struct kvaser_pciefd_irq_mask kvaser_pciefd_xilinx_irq_mask = {
	.kcan_rx0 = BIT(4),
	.kcan_tx = { BIT(16), BIT(17), BIT(18), BIT(19), BIT(20), BIT(21), BIT(22), BIT(23) },
	.all = GENMASK(23, 16) | BIT(4),
};

static const struct kvaser_pciefd_dev_ops kvaser_pciefd_altera_dev_ops = {
	.kvaser_pciefd_write_dma_map = kvaser_pciefd_write_dma_map_altera,
};

static const struct kvaser_pciefd_dev_ops kvaser_pciefd_sf2_dev_ops = {
	.kvaser_pciefd_write_dma_map = kvaser_pciefd_write_dma_map_sf2,
};

static const struct kvaser_pciefd_dev_ops kvaser_pciefd_xilinx_dev_ops = {
	.kvaser_pciefd_write_dma_map = kvaser_pciefd_write_dma_map_xilinx,
};

static const struct kvaser_pciefd_driver_data kvaser_pciefd_altera_driver_data = {
	.address_offset = &kvaser_pciefd_altera_address_offset,
	.irq_mask = &kvaser_pciefd_altera_irq_mask,
	.ops = &kvaser_pciefd_altera_dev_ops,
};

static const struct kvaser_pciefd_driver_data kvaser_pciefd_sf2_driver_data = {
	.address_offset = &kvaser_pciefd_sf2_address_offset,
	.irq_mask = &kvaser_pciefd_sf2_irq_mask,
	.ops = &kvaser_pciefd_sf2_dev_ops,
};

static const struct kvaser_pciefd_driver_data kvaser_pciefd_xilinx_driver_data = {
	.address_offset = &kvaser_pciefd_xilinx_address_offset,
	.irq_mask = &kvaser_pciefd_xilinx_irq_mask,
	.ops = &kvaser_pciefd_xilinx_dev_ops,
};

struct kvaser_pciefd_rx_packet {
	u32 header[2];
	u64 timestamp;
};

struct kvaser_pciefd_tx_packet {
	u32 header[2];
	u8 data[64];
};

static const struct can_bittiming_const kvaser_pciefd_bittiming_const = {
	.name = KVASER_PCIEFD_DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 512,
	.tseg2_min = 1,
	.tseg2_max = 32,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 8192,
	.brp_inc = 1,
};

static struct pci_device_id kvaser_pciefd_id_table[] = {
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_4HS_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_altera_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_2HS_V2_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_altera_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_HS_V2_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_altera_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_MINIPCIE_HS_V2_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_altera_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_MINIPCIE_2HS_V2_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_altera_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_2CAN_V3_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_sf2_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_1CAN_V3_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_sf2_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_4CAN_V2_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_sf2_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_MINIPCIE_2CAN_V3_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_sf2_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_MINIPCIE_1CAN_V3_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_sf2_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_M2_4CAN_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_xilinx_driver_data,
	},
	{
		PCI_DEVICE(KVASER_PCIEFD_VENDOR, KVASER_PCIEFD_8CAN_DEVICE_ID),
		.driver_data = (kernel_ulong_t)&kvaser_pciefd_xilinx_driver_data,
	},
	{
		0,
	},
};
MODULE_DEVICE_TABLE(pci, kvaser_pciefd_id_table);

static inline void kvaser_pciefd_send_kcan_cmd(struct kvaser_pciefd_can *can, u32 cmd)
{
	iowrite32(FIELD_PREP(KVASER_PCIEFD_KCAN_CMD_MASK, cmd) |
		  FIELD_PREP(KVASER_PCIEFD_KCAN_CMD_SEQ_MASK, ++can->cmd_seq),
		  can->reg_base + KVASER_PCIEFD_KCAN_CMD_REG);
}

static inline void kvaser_pciefd_request_status(struct kvaser_pciefd_can *can)
{
	kvaser_pciefd_send_kcan_cmd(can, KVASER_PCIEFD_KCAN_CMD_SRQ);
}

static inline void kvaser_pciefd_abort_flush_reset(struct kvaser_pciefd_can *can)
{
	kvaser_pciefd_send_kcan_cmd(can, KVASER_PCIEFD_KCAN_CMD_AT);
}

static inline void kvaser_pciefd_set_led(struct kvaser_pciefd_can *can, bool on)
{
	if (on)
		can->ioc &= ~KVASER_PCIEFD_KCAN_IOC_LED;
	else
		can->ioc |= KVASER_PCIEFD_KCAN_IOC_LED;

	iowrite32(can->ioc, can->reg_base + KVASER_PCIEFD_KCAN_IOC_REG);
}

static void kvaser_pciefd_enable_err_gen(struct kvaser_pciefd_can *can)
{
	u32 mode;
	unsigned long irq;

	spin_lock_irqsave(&can->lock, irq);
	mode = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	if (!(mode & KVASER_PCIEFD_KCAN_MODE_EPEN)) {
		mode |= KVASER_PCIEFD_KCAN_MODE_EPEN;
		iowrite32(mode, can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	}
	spin_unlock_irqrestore(&can->lock, irq);
}

static void kvaser_pciefd_disable_err_gen(struct kvaser_pciefd_can *can)
{
	u32 mode;
	unsigned long irq;

	spin_lock_irqsave(&can->lock, irq);
	mode = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	mode &= ~KVASER_PCIEFD_KCAN_MODE_EPEN;
	iowrite32(mode, can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	spin_unlock_irqrestore(&can->lock, irq);
}

static inline void kvaser_pciefd_set_tx_irq(struct kvaser_pciefd_can *can)
{
	u32 msk;

	msk = KVASER_PCIEFD_KCAN_IRQ_TE | KVASER_PCIEFD_KCAN_IRQ_ROF |
	      KVASER_PCIEFD_KCAN_IRQ_TOF | KVASER_PCIEFD_KCAN_IRQ_ABD |
	      KVASER_PCIEFD_KCAN_IRQ_TAE | KVASER_PCIEFD_KCAN_IRQ_TAL |
	      KVASER_PCIEFD_KCAN_IRQ_FDIC | KVASER_PCIEFD_KCAN_IRQ_BPP |
	      KVASER_PCIEFD_KCAN_IRQ_TAR;

	iowrite32(msk, can->reg_base + KVASER_PCIEFD_KCAN_IEN_REG);
}

static inline void kvaser_pciefd_set_skb_timestamp(const struct kvaser_pciefd *pcie,
						   struct sk_buff *skb, u64 timestamp)
{
	skb_hwtstamps(skb)->hwtstamp =
		ns_to_ktime(div_u64(timestamp * 1000, pcie->freq_to_ticks_div));
}

static void kvaser_pciefd_setup_controller(struct kvaser_pciefd_can *can)
{
	u32 mode;
	unsigned long irq;

	spin_lock_irqsave(&can->lock, irq);
	mode = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	if (can->can.ctrlmode & CAN_CTRLMODE_FD) {
		mode &= ~KVASER_PCIEFD_KCAN_MODE_CCM;
		if (can->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO)
			mode |= KVASER_PCIEFD_KCAN_MODE_NIFDEN;
		else
			mode &= ~KVASER_PCIEFD_KCAN_MODE_NIFDEN;
	} else {
		mode |= KVASER_PCIEFD_KCAN_MODE_CCM;
		mode &= ~KVASER_PCIEFD_KCAN_MODE_NIFDEN;
	}

	if (can->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		mode |= KVASER_PCIEFD_KCAN_MODE_LOM;
	else
		mode &= ~KVASER_PCIEFD_KCAN_MODE_LOM;
	mode |= KVASER_PCIEFD_KCAN_MODE_EEN;
	mode |= KVASER_PCIEFD_KCAN_MODE_EPEN;
	/* Use ACK packet type */
	mode &= ~KVASER_PCIEFD_KCAN_MODE_APT;
	mode &= ~KVASER_PCIEFD_KCAN_MODE_RM;
	iowrite32(mode, can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);

	spin_unlock_irqrestore(&can->lock, irq);
}

static void kvaser_pciefd_start_controller_flush(struct kvaser_pciefd_can *can)
{
	u32 status;
	unsigned long irq;

	spin_lock_irqsave(&can->lock, irq);
	iowrite32(GENMASK(31, 0), can->reg_base + KVASER_PCIEFD_KCAN_IRQ_REG);
	iowrite32(KVASER_PCIEFD_KCAN_IRQ_ABD,
		  can->reg_base + KVASER_PCIEFD_KCAN_IEN_REG);
	status = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_STAT_REG);
	if (status & KVASER_PCIEFD_KCAN_STAT_IDLE) {
		/* If controller is already idle, run abort, flush and reset */
		kvaser_pciefd_abort_flush_reset(can);
	} else if (!(status & KVASER_PCIEFD_KCAN_STAT_RMR)) {
		u32 mode;

		/* Put controller in reset mode */
		mode = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
		mode |= KVASER_PCIEFD_KCAN_MODE_RM;
		iowrite32(mode, can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	}
	spin_unlock_irqrestore(&can->lock, irq);
}

static int kvaser_pciefd_bus_on(struct kvaser_pciefd_can *can)
{
	u32 mode;
	unsigned long irq;

	timer_delete(&can->bec_poll_timer);
	if (!completion_done(&can->flush_comp))
		kvaser_pciefd_start_controller_flush(can);

	if (!wait_for_completion_timeout(&can->flush_comp,
					 KVASER_PCIEFD_WAIT_TIMEOUT)) {
		netdev_err(can->can.dev, "Timeout during bus on flush\n");
		return -ETIMEDOUT;
	}

	spin_lock_irqsave(&can->lock, irq);
	iowrite32(0, can->reg_base + KVASER_PCIEFD_KCAN_IEN_REG);
	iowrite32(GENMASK(31, 0), can->reg_base + KVASER_PCIEFD_KCAN_IRQ_REG);
	iowrite32(KVASER_PCIEFD_KCAN_IRQ_ABD,
		  can->reg_base + KVASER_PCIEFD_KCAN_IEN_REG);
	mode = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	mode &= ~KVASER_PCIEFD_KCAN_MODE_RM;
	iowrite32(mode, can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	spin_unlock_irqrestore(&can->lock, irq);

	if (!wait_for_completion_timeout(&can->start_comp,
					 KVASER_PCIEFD_WAIT_TIMEOUT)) {
		netdev_err(can->can.dev, "Timeout during bus on reset\n");
		return -ETIMEDOUT;
	}
	/* Reset interrupt handling */
	iowrite32(0, can->reg_base + KVASER_PCIEFD_KCAN_IEN_REG);
	iowrite32(GENMASK(31, 0), can->reg_base + KVASER_PCIEFD_KCAN_IRQ_REG);

	kvaser_pciefd_set_tx_irq(can);
	kvaser_pciefd_setup_controller(can);
	can->can.state = CAN_STATE_ERROR_ACTIVE;
	netif_wake_queue(can->can.dev);
	can->bec.txerr = 0;
	can->bec.rxerr = 0;
	can->err_rep_cnt = 0;

	return 0;
}

static void kvaser_pciefd_pwm_stop(struct kvaser_pciefd_can *can)
{
	u8 top;
	u32 pwm_ctrl;
	unsigned long irq;

	spin_lock_irqsave(&can->lock, irq);
	pwm_ctrl = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_PWM_REG);
	top = FIELD_GET(KVASER_PCIEFD_KCAN_PWM_TOP_MASK, pwm_ctrl);
	/* Set duty cycle to zero */
	pwm_ctrl |= FIELD_PREP(KVASER_PCIEFD_KCAN_PWM_TRIGGER_MASK, top);
	iowrite32(pwm_ctrl, can->reg_base + KVASER_PCIEFD_KCAN_PWM_REG);
	spin_unlock_irqrestore(&can->lock, irq);
}

static void kvaser_pciefd_pwm_start(struct kvaser_pciefd_can *can)
{
	int top, trigger;
	u32 pwm_ctrl;
	unsigned long irq;

	kvaser_pciefd_pwm_stop(can);
	spin_lock_irqsave(&can->lock, irq);
	/* Set frequency to 500 KHz */
	top = can->kv_pcie->bus_freq / (2 * 500000) - 1;

	pwm_ctrl = FIELD_PREP(KVASER_PCIEFD_KCAN_PWM_TRIGGER_MASK, top);
	pwm_ctrl |= FIELD_PREP(KVASER_PCIEFD_KCAN_PWM_TOP_MASK, top);
	iowrite32(pwm_ctrl, can->reg_base + KVASER_PCIEFD_KCAN_PWM_REG);

	/* Set duty cycle to 95 */
	trigger = (100 * top - 95 * (top + 1) + 50) / 100;
	pwm_ctrl = FIELD_PREP(KVASER_PCIEFD_KCAN_PWM_TRIGGER_MASK, trigger);
	pwm_ctrl |= FIELD_PREP(KVASER_PCIEFD_KCAN_PWM_TOP_MASK, top);
	iowrite32(pwm_ctrl, can->reg_base + KVASER_PCIEFD_KCAN_PWM_REG);
	spin_unlock_irqrestore(&can->lock, irq);
}

static int kvaser_pciefd_open(struct net_device *netdev)
{
	int ret;
	struct kvaser_pciefd_can *can = netdev_priv(netdev);

	can->tx_idx = 0;
	can->ack_idx = 0;

	ret = open_candev(netdev);
	if (ret)
		return ret;

	ret = kvaser_pciefd_bus_on(can);
	if (ret) {
		close_candev(netdev);
		return ret;
	}

	return 0;
}

static int kvaser_pciefd_stop(struct net_device *netdev)
{
	struct kvaser_pciefd_can *can = netdev_priv(netdev);
	int ret = 0;

	/* Don't interrupt ongoing flush */
	if (!completion_done(&can->flush_comp))
		kvaser_pciefd_start_controller_flush(can);

	if (!wait_for_completion_timeout(&can->flush_comp,
					 KVASER_PCIEFD_WAIT_TIMEOUT)) {
		netdev_err(can->can.dev, "Timeout during stop\n");
		ret = -ETIMEDOUT;
	} else {
		iowrite32(0, can->reg_base + KVASER_PCIEFD_KCAN_IEN_REG);
		timer_delete(&can->bec_poll_timer);
	}
	can->can.state = CAN_STATE_STOPPED;
	netdev_reset_queue(netdev);
	close_candev(netdev);

	return ret;
}

static unsigned int kvaser_pciefd_tx_avail(const struct kvaser_pciefd_can *can)
{
	return can->tx_max_count - (READ_ONCE(can->tx_idx) - READ_ONCE(can->ack_idx));
}

static int kvaser_pciefd_prepare_tx_packet(struct kvaser_pciefd_tx_packet *p,
					   struct can_priv *can, u8 seq,
					   struct sk_buff *skb)
{
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	int packet_size;

	memset(p, 0, sizeof(*p));
	if (can->ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		p->header[1] |= KVASER_PCIEFD_TPACKET_SMS;

	if (cf->can_id & CAN_RTR_FLAG)
		p->header[0] |= KVASER_PCIEFD_RPACKET_RTR;

	if (cf->can_id & CAN_EFF_FLAG)
		p->header[0] |= KVASER_PCIEFD_RPACKET_IDE;

	p->header[0] |= FIELD_PREP(KVASER_PCIEFD_RPACKET_ID_MASK, cf->can_id);
	p->header[1] |= KVASER_PCIEFD_TPACKET_AREQ;

	if (can_is_canfd_skb(skb)) {
		p->header[1] |= FIELD_PREP(KVASER_PCIEFD_RPACKET_DLC_MASK,
					   can_fd_len2dlc(cf->len));
		p->header[1] |= KVASER_PCIEFD_RPACKET_FDF;
		if (cf->flags & CANFD_BRS)
			p->header[1] |= KVASER_PCIEFD_RPACKET_BRS;
		if (cf->flags & CANFD_ESI)
			p->header[1] |= KVASER_PCIEFD_RPACKET_ESI;
	} else {
		p->header[1] |=
			FIELD_PREP(KVASER_PCIEFD_RPACKET_DLC_MASK,
				   can_get_cc_dlc((struct can_frame *)cf, can->ctrlmode));
	}

	p->header[1] |= FIELD_PREP(KVASER_PCIEFD_PACKET_SEQ_MASK, seq);

	packet_size = cf->len;
	memcpy(p->data, cf->data, packet_size);

	return DIV_ROUND_UP(packet_size, 4);
}

static netdev_tx_t kvaser_pciefd_start_xmit(struct sk_buff *skb,
					    struct net_device *netdev)
{
	struct kvaser_pciefd_can *can = netdev_priv(netdev);
	struct kvaser_pciefd_tx_packet packet;
	unsigned int seq = can->tx_idx & (can->can.echo_skb_max - 1);
	unsigned int frame_len;
	int nr_words;

	if (can_dev_dropped_skb(netdev, skb))
		return NETDEV_TX_OK;
	if (!netif_subqueue_maybe_stop(netdev, 0, kvaser_pciefd_tx_avail(can), 1, 1))
		return NETDEV_TX_BUSY;

	nr_words = kvaser_pciefd_prepare_tx_packet(&packet, &can->can, seq, skb);

	/* Prepare and save echo skb in internal slot */
	WRITE_ONCE(can->can.echo_skb[seq], NULL);
	frame_len = can_skb_get_frame_len(skb);
	can_put_echo_skb(skb, netdev, seq, frame_len);
	netdev_sent_queue(netdev, frame_len);
	WRITE_ONCE(can->tx_idx, can->tx_idx + 1);

	/* Write header to fifo */
	iowrite32(packet.header[0],
		  can->reg_base + KVASER_PCIEFD_KCAN_FIFO_REG);
	iowrite32(packet.header[1],
		  can->reg_base + KVASER_PCIEFD_KCAN_FIFO_REG);

	if (nr_words) {
		u32 data_last = ((u32 *)packet.data)[nr_words - 1];

		/* Write data to fifo, except last word */
		iowrite32_rep(can->reg_base +
			      KVASER_PCIEFD_KCAN_FIFO_REG, packet.data,
			      nr_words - 1);
		/* Write last word to end of fifo */
		__raw_writel(data_last, can->reg_base +
			     KVASER_PCIEFD_KCAN_FIFO_LAST_REG);
	} else {
		/* Complete write to fifo */
		__raw_writel(0, can->reg_base +
			     KVASER_PCIEFD_KCAN_FIFO_LAST_REG);
	}

	netif_subqueue_maybe_stop(netdev, 0, kvaser_pciefd_tx_avail(can), 1, 1);

	return NETDEV_TX_OK;
}

static int kvaser_pciefd_set_bittiming(struct kvaser_pciefd_can *can, bool data)
{
	u32 mode, test, btrn;
	unsigned long irq_flags;
	int ret;
	struct can_bittiming *bt;

	if (data)
		bt = &can->can.fd.data_bittiming;
	else
		bt = &can->can.bittiming;

	btrn = FIELD_PREP(KVASER_PCIEFD_KCAN_BTRN_TSEG2_MASK, bt->phase_seg2 - 1) |
	       FIELD_PREP(KVASER_PCIEFD_KCAN_BTRN_TSEG1_MASK, bt->prop_seg + bt->phase_seg1 - 1) |
	       FIELD_PREP(KVASER_PCIEFD_KCAN_BTRN_SJW_MASK, bt->sjw - 1) |
	       FIELD_PREP(KVASER_PCIEFD_KCAN_BTRN_BRP_MASK, bt->brp - 1);

	spin_lock_irqsave(&can->lock, irq_flags);
	mode = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	/* Put the circuit in reset mode */
	iowrite32(mode | KVASER_PCIEFD_KCAN_MODE_RM,
		  can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);

	/* Can only set bittiming if in reset mode */
	ret = readl_poll_timeout(can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG,
				 test, test & KVASER_PCIEFD_KCAN_MODE_RM, 0, 10);
	if (ret) {
		spin_unlock_irqrestore(&can->lock, irq_flags);
		return -EBUSY;
	}

	if (data)
		iowrite32(btrn, can->reg_base + KVASER_PCIEFD_KCAN_BTRD_REG);
	else
		iowrite32(btrn, can->reg_base + KVASER_PCIEFD_KCAN_BTRN_REG);
	/* Restore previous reset mode status */
	iowrite32(mode, can->reg_base + KVASER_PCIEFD_KCAN_MODE_REG);
	spin_unlock_irqrestore(&can->lock, irq_flags);

	return 0;
}

static int kvaser_pciefd_set_nominal_bittiming(struct net_device *ndev)
{
	return kvaser_pciefd_set_bittiming(netdev_priv(ndev), false);
}

static int kvaser_pciefd_set_data_bittiming(struct net_device *ndev)
{
	return kvaser_pciefd_set_bittiming(netdev_priv(ndev), true);
}

static int kvaser_pciefd_set_mode(struct net_device *ndev, enum can_mode mode)
{
	struct kvaser_pciefd_can *can = netdev_priv(ndev);
	int ret = 0;

	switch (mode) {
	case CAN_MODE_START:
		if (!can->can.restart_ms)
			ret = kvaser_pciefd_bus_on(can);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int kvaser_pciefd_get_berr_counter(const struct net_device *ndev,
					  struct can_berr_counter *bec)
{
	struct kvaser_pciefd_can *can = netdev_priv(ndev);

	bec->rxerr = can->bec.rxerr;
	bec->txerr = can->bec.txerr;

	return 0;
}

static void kvaser_pciefd_bec_poll_timer(struct timer_list *data)
{
	struct kvaser_pciefd_can *can = timer_container_of(can, data,
							   bec_poll_timer);

	kvaser_pciefd_enable_err_gen(can);
	kvaser_pciefd_request_status(can);
	can->err_rep_cnt = 0;
}

static const struct net_device_ops kvaser_pciefd_netdev_ops = {
	.ndo_open = kvaser_pciefd_open,
	.ndo_stop = kvaser_pciefd_stop,
	.ndo_eth_ioctl = can_eth_ioctl_hwts,
	.ndo_start_xmit = kvaser_pciefd_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static int kvaser_pciefd_set_phys_id(struct net_device *netdev,
				     enum ethtool_phys_id_state state)
{
	struct kvaser_pciefd_can *can = netdev_priv(netdev);

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		return 3; /* 3 On/Off cycles per second */

	case ETHTOOL_ID_ON:
		kvaser_pciefd_set_led(can, true);
		return 0;

	case ETHTOOL_ID_OFF:
	case ETHTOOL_ID_INACTIVE:
		kvaser_pciefd_set_led(can, false);
		return 0;

	default:
		return -EINVAL;
	}
}

static const struct ethtool_ops kvaser_pciefd_ethtool_ops = {
	.get_ts_info = can_ethtool_op_get_ts_info_hwts,
	.set_phys_id = kvaser_pciefd_set_phys_id,
};

static int kvaser_pciefd_setup_can_ctrls(struct kvaser_pciefd *pcie)
{
	int i;

	for (i = 0; i < pcie->nr_channels; i++) {
		struct net_device *netdev;
		struct kvaser_pciefd_can *can;
		u32 status, tx_nr_packets_max;
		int ret;

		netdev = alloc_candev(sizeof(struct kvaser_pciefd_can),
				      roundup_pow_of_two(KVASER_PCIEFD_CAN_TX_MAX_COUNT));
		if (!netdev)
			return -ENOMEM;

		can = netdev_priv(netdev);
		netdev->netdev_ops = &kvaser_pciefd_netdev_ops;
		netdev->ethtool_ops = &kvaser_pciefd_ethtool_ops;
		can->reg_base = KVASER_PCIEFD_KCAN_CHX_ADDR(pcie, i);
		can->kv_pcie = pcie;
		can->cmd_seq = 0;
		can->err_rep_cnt = 0;
		can->completed_tx_pkts = 0;
		can->completed_tx_bytes = 0;
		can->bec.txerr = 0;
		can->bec.rxerr = 0;
		can->can.dev->dev_port = i;

		init_completion(&can->start_comp);
		init_completion(&can->flush_comp);
		timer_setup(&can->bec_poll_timer, kvaser_pciefd_bec_poll_timer, 0);

		/* Disable Bus load reporting */
		iowrite32(0, can->reg_base + KVASER_PCIEFD_KCAN_BUS_LOAD_REG);

		can->ioc = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_IOC_REG);
		kvaser_pciefd_set_led(can, false);

		tx_nr_packets_max =
			FIELD_GET(KVASER_PCIEFD_KCAN_TX_NR_PACKETS_MAX_MASK,
				  ioread32(can->reg_base + KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG));
		can->tx_max_count = min(KVASER_PCIEFD_CAN_TX_MAX_COUNT, tx_nr_packets_max - 1);

		can->can.clock.freq = pcie->freq;
		spin_lock_init(&can->lock);

		can->can.bittiming_const = &kvaser_pciefd_bittiming_const;
		can->can.fd.data_bittiming_const = &kvaser_pciefd_bittiming_const;
		can->can.do_set_bittiming = kvaser_pciefd_set_nominal_bittiming;
		can->can.fd.do_set_data_bittiming = kvaser_pciefd_set_data_bittiming;
		can->can.do_set_mode = kvaser_pciefd_set_mode;
		can->can.do_get_berr_counter = kvaser_pciefd_get_berr_counter;
		can->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY |
					      CAN_CTRLMODE_FD |
					      CAN_CTRLMODE_FD_NON_ISO |
					      CAN_CTRLMODE_CC_LEN8_DLC |
					      CAN_CTRLMODE_BERR_REPORTING;

		status = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_STAT_REG);
		if (!(status & KVASER_PCIEFD_KCAN_STAT_FD)) {
			dev_err(&pcie->pci->dev,
				"CAN FD not supported as expected %d\n", i);

			free_candev(netdev);
			return -ENODEV;
		}

		if (status & KVASER_PCIEFD_KCAN_STAT_CAP)
			can->can.ctrlmode_supported |= CAN_CTRLMODE_ONE_SHOT;

		netdev->flags |= IFF_ECHO;
		SET_NETDEV_DEV(netdev, &pcie->pci->dev);

		iowrite32(GENMASK(31, 0), can->reg_base + KVASER_PCIEFD_KCAN_IRQ_REG);
		iowrite32(KVASER_PCIEFD_KCAN_IRQ_ABD,
			  can->reg_base + KVASER_PCIEFD_KCAN_IEN_REG);

		pcie->can[i] = can;
		kvaser_pciefd_pwm_start(can);
		ret = kvaser_pciefd_devlink_port_register(can);
		if (ret) {
			dev_err(&pcie->pci->dev, "Failed to register devlink port\n");
			return ret;
		}
	}

	return 0;
}

static int kvaser_pciefd_reg_candev(struct kvaser_pciefd *pcie)
{
	int i;

	for (i = 0; i < pcie->nr_channels; i++) {
		int ret = register_candev(pcie->can[i]->can.dev);

		if (ret) {
			int j;

			/* Unregister all successfully registered devices. */
			for (j = 0; j < i; j++)
				unregister_candev(pcie->can[j]->can.dev);
			return ret;
		}
	}

	return 0;
}

static void kvaser_pciefd_write_dma_map_altera(struct kvaser_pciefd *pcie,
					       dma_addr_t addr, int index)
{
	void __iomem *serdes_base;
	u32 word1, word2;

	if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT)) {
		word1 = lower_32_bits(addr) | KVASER_PCIEFD_ALTERA_DMA_64BIT;
		word2 = upper_32_bits(addr);
	} else {
		word1 = addr;
		word2 = 0;
	}
	serdes_base = KVASER_PCIEFD_SERDES_ADDR(pcie) + 0x8 * index;
	iowrite32(word1, serdes_base);
	iowrite32(word2, serdes_base + 0x4);
}

static void kvaser_pciefd_write_dma_map_sf2(struct kvaser_pciefd *pcie,
					    dma_addr_t addr, int index)
{
	void __iomem *serdes_base;
	u32 lsb = addr & KVASER_PCIEFD_SF2_DMA_LSB_MASK;
	u32 msb = 0x0;

	if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT))
		msb = upper_32_bits(addr);

	serdes_base = KVASER_PCIEFD_SERDES_ADDR(pcie) + 0x10 * index;
	iowrite32(lsb, serdes_base);
	iowrite32(msb, serdes_base + 0x4);
}

static void kvaser_pciefd_write_dma_map_xilinx(struct kvaser_pciefd *pcie,
					       dma_addr_t addr, int index)
{
	void __iomem *serdes_base;
	u32 lsb = addr & KVASER_PCIEFD_XILINX_DMA_LSB_MASK;
	u32 msb = 0x0;

	if (IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT))
		msb = upper_32_bits(addr);

	serdes_base = KVASER_PCIEFD_SERDES_ADDR(pcie) + 0x8 * index;
	iowrite32(msb, serdes_base);
	iowrite32(lsb, serdes_base + 0x4);
}

static int kvaser_pciefd_setup_dma(struct kvaser_pciefd *pcie)
{
	int i;
	u32 srb_status;
	u32 srb_packet_count;
	dma_addr_t dma_addr[KVASER_PCIEFD_DMA_COUNT];

	/* Disable the DMA */
	iowrite32(0, KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_CTRL_REG);

	dma_set_mask_and_coherent(&pcie->pci->dev, DMA_BIT_MASK(64));

	for (i = 0; i < KVASER_PCIEFD_DMA_COUNT; i++) {
		pcie->dma_data[i] = dmam_alloc_coherent(&pcie->pci->dev,
							KVASER_PCIEFD_DMA_SIZE,
							&dma_addr[i],
							GFP_KERNEL);

		if (!pcie->dma_data[i] || !dma_addr[i]) {
			dev_err(&pcie->pci->dev, "Rx dma_alloc(%u) failure\n",
				KVASER_PCIEFD_DMA_SIZE);
			return -ENOMEM;
		}
		pcie->driver_data->ops->kvaser_pciefd_write_dma_map(pcie, dma_addr[i], i);
	}

	/* Reset Rx FIFO, and both DMA buffers */
	iowrite32(KVASER_PCIEFD_SRB_CMD_FOR | KVASER_PCIEFD_SRB_CMD_RDB0 |
		  KVASER_PCIEFD_SRB_CMD_RDB1,
		  KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_CMD_REG);
	/* Empty Rx FIFO */
	srb_packet_count =
		FIELD_GET(KVASER_PCIEFD_SRB_RX_NR_PACKETS_MASK,
			  ioread32(KVASER_PCIEFD_SRB_ADDR(pcie) +
				   KVASER_PCIEFD_SRB_RX_NR_PACKETS_REG));
	while (srb_packet_count) {
		/* Drop current packet in FIFO */
		ioread32(KVASER_PCIEFD_SRB_FIFO_ADDR(pcie) + KVASER_PCIEFD_SRB_FIFO_LAST_REG);
		srb_packet_count--;
	}

	srb_status = ioread32(KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_STAT_REG);
	if (!(srb_status & KVASER_PCIEFD_SRB_STAT_DI)) {
		dev_err(&pcie->pci->dev, "DMA not idle before enabling\n");
		return -EIO;
	}

	/* Enable the DMA */
	iowrite32(KVASER_PCIEFD_SRB_CTRL_DMA_ENABLE,
		  KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_CTRL_REG);

	return 0;
}

static int kvaser_pciefd_setup_board(struct kvaser_pciefd *pcie)
{
	u32 version, srb_status, build;

	version = ioread32(KVASER_PCIEFD_SYSID_ADDR(pcie) + KVASER_PCIEFD_SYSID_VERSION_REG);
	build = ioread32(KVASER_PCIEFD_SYSID_ADDR(pcie) + KVASER_PCIEFD_SYSID_BUILD_REG);
	pcie->nr_channels = min(KVASER_PCIEFD_MAX_CAN_CHANNELS,
				FIELD_GET(KVASER_PCIEFD_SYSID_VERSION_NR_CHAN_MASK, version));
	pcie->fw_version.major = FIELD_GET(KVASER_PCIEFD_SYSID_VERSION_MAJOR_MASK, version);
	pcie->fw_version.minor = FIELD_GET(KVASER_PCIEFD_SYSID_VERSION_MINOR_MASK, version);
	pcie->fw_version.build = FIELD_GET(KVASER_PCIEFD_SYSID_BUILD_SEQ_MASK, build);

	srb_status = ioread32(KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_STAT_REG);
	if (!(srb_status & KVASER_PCIEFD_SRB_STAT_DMA)) {
		dev_err(&pcie->pci->dev, "Hardware without DMA is not supported\n");
		return -ENODEV;
	}

	pcie->bus_freq = ioread32(KVASER_PCIEFD_SYSID_ADDR(pcie) + KVASER_PCIEFD_SYSID_BUSFREQ_REG);
	pcie->freq = ioread32(KVASER_PCIEFD_SYSID_ADDR(pcie) + KVASER_PCIEFD_SYSID_CANFREQ_REG);
	pcie->freq_to_ticks_div = pcie->freq / 1000000;
	if (pcie->freq_to_ticks_div == 0)
		pcie->freq_to_ticks_div = 1;
	/* Turn off all loopback functionality */
	iowrite32(0, KVASER_PCIEFD_LOOPBACK_ADDR(pcie));

	return 0;
}

static int kvaser_pciefd_handle_data_packet(struct kvaser_pciefd *pcie,
					    struct kvaser_pciefd_rx_packet *p,
					    __le32 *data)
{
	struct sk_buff *skb;
	struct canfd_frame *cf;
	struct can_priv *priv;
	u8 ch_id = FIELD_GET(KVASER_PCIEFD_PACKET_CHID_MASK, p->header[1]);
	u8 dlc;

	if (ch_id >= pcie->nr_channels)
		return -EIO;

	priv = &pcie->can[ch_id]->can;
	dlc = FIELD_GET(KVASER_PCIEFD_RPACKET_DLC_MASK, p->header[1]);

	if (p->header[1] & KVASER_PCIEFD_RPACKET_FDF) {
		skb = alloc_canfd_skb(priv->dev, &cf);
		if (!skb) {
			priv->dev->stats.rx_dropped++;
			return 0;
		}

		cf->len = can_fd_dlc2len(dlc);
		if (p->header[1] & KVASER_PCIEFD_RPACKET_BRS)
			cf->flags |= CANFD_BRS;
		if (p->header[1] & KVASER_PCIEFD_RPACKET_ESI)
			cf->flags |= CANFD_ESI;
	} else {
		skb = alloc_can_skb(priv->dev, (struct can_frame **)&cf);
		if (!skb) {
			priv->dev->stats.rx_dropped++;
			return 0;
		}
		can_frame_set_cc_len((struct can_frame *)cf, dlc, priv->ctrlmode);
	}

	cf->can_id = FIELD_GET(KVASER_PCIEFD_RPACKET_ID_MASK, p->header[0]);
	if (p->header[0] & KVASER_PCIEFD_RPACKET_IDE)
		cf->can_id |= CAN_EFF_FLAG;

	if (p->header[0] & KVASER_PCIEFD_RPACKET_RTR) {
		cf->can_id |= CAN_RTR_FLAG;
	} else {
		memcpy(cf->data, data, cf->len);
		priv->dev->stats.rx_bytes += cf->len;
	}
	priv->dev->stats.rx_packets++;
	kvaser_pciefd_set_skb_timestamp(pcie, skb, p->timestamp);

	netif_rx(skb);

	return 0;
}

static void kvaser_pciefd_change_state(struct kvaser_pciefd_can *can,
				       const struct can_berr_counter *bec,
				       struct can_frame *cf,
				       enum can_state new_state,
				       enum can_state tx_state,
				       enum can_state rx_state)
{
	enum can_state old_state;

	old_state = can->can.state;
	can_change_state(can->can.dev, cf, tx_state, rx_state);

	if (new_state == CAN_STATE_BUS_OFF) {
		struct net_device *ndev = can->can.dev;
		unsigned long irq_flags;

		spin_lock_irqsave(&can->lock, irq_flags);
		netif_stop_queue(can->can.dev);
		spin_unlock_irqrestore(&can->lock, irq_flags);
		/* Prevent CAN controller from auto recover from bus off */
		if (!can->can.restart_ms) {
			kvaser_pciefd_start_controller_flush(can);
			can_bus_off(ndev);
		}
	}
	if (old_state == CAN_STATE_BUS_OFF &&
	    new_state == CAN_STATE_ERROR_ACTIVE &&
	    can->can.restart_ms) {
		can->can.can_stats.restarts++;
		if (cf)
			cf->can_id |= CAN_ERR_RESTARTED;
	}
	if (cf && new_state != CAN_STATE_BUS_OFF) {
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = bec->txerr;
		cf->data[7] = bec->rxerr;
	}
}

static void kvaser_pciefd_packet_to_state(struct kvaser_pciefd_rx_packet *p,
					  struct can_berr_counter *bec,
					  enum can_state *new_state,
					  enum can_state *tx_state,
					  enum can_state *rx_state)
{
	if (p->header[0] & KVASER_PCIEFD_SPACK_BOFF ||
	    p->header[0] & KVASER_PCIEFD_SPACK_IRM)
		*new_state = CAN_STATE_BUS_OFF;
	else if (bec->txerr >= 255 || bec->rxerr >= 255)
		*new_state = CAN_STATE_BUS_OFF;
	else if (p->header[1] & KVASER_PCIEFD_SPACK_EPLR)
		*new_state = CAN_STATE_ERROR_PASSIVE;
	else if (bec->txerr >= 128 || bec->rxerr >= 128)
		*new_state = CAN_STATE_ERROR_PASSIVE;
	else if (p->header[1] & KVASER_PCIEFD_SPACK_EWLR)
		*new_state = CAN_STATE_ERROR_WARNING;
	else if (bec->txerr >= 96 || bec->rxerr >= 96)
		*new_state = CAN_STATE_ERROR_WARNING;
	else
		*new_state = CAN_STATE_ERROR_ACTIVE;

	*tx_state = bec->txerr >= bec->rxerr ? *new_state : 0;
	*rx_state = bec->txerr <= bec->rxerr ? *new_state : 0;
}

static int kvaser_pciefd_rx_error_frame(struct kvaser_pciefd_can *can,
					struct kvaser_pciefd_rx_packet *p)
{
	struct can_berr_counter bec;
	enum can_state old_state, new_state, tx_state, rx_state;
	struct net_device *ndev = can->can.dev;
	struct sk_buff *skb = NULL;
	struct can_frame *cf = NULL;

	old_state = can->can.state;

	bec.txerr = FIELD_GET(KVASER_PCIEFD_SPACK_TXERR_MASK, p->header[0]);
	bec.rxerr = FIELD_GET(KVASER_PCIEFD_SPACK_RXERR_MASK, p->header[0]);

	kvaser_pciefd_packet_to_state(p, &bec, &new_state, &tx_state, &rx_state);
	if (can->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		skb = alloc_can_err_skb(ndev, &cf);
	if (new_state != old_state) {
		kvaser_pciefd_change_state(can, &bec, cf, new_state, tx_state, rx_state);
	}

	can->err_rep_cnt++;
	can->can.can_stats.bus_error++;
	if (p->header[1] & KVASER_PCIEFD_EPACK_DIR_TX)
		ndev->stats.tx_errors++;
	else
		ndev->stats.rx_errors++;

	can->bec.txerr = bec.txerr;
	can->bec.rxerr = bec.rxerr;

	if (can->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING) {
		if (!skb) {
			netdev_warn(ndev, "No memory left for err_skb\n");
			ndev->stats.rx_dropped++;
			return -ENOMEM;
		}
		kvaser_pciefd_set_skb_timestamp(can->kv_pcie, skb, p->timestamp);
		cf->can_id |= CAN_ERR_BUSERROR | CAN_ERR_CNT;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
		netif_rx(skb);
	}

	return 0;
}

static int kvaser_pciefd_handle_error_packet(struct kvaser_pciefd *pcie,
					     struct kvaser_pciefd_rx_packet *p)
{
	struct kvaser_pciefd_can *can;
	u8 ch_id = FIELD_GET(KVASER_PCIEFD_PACKET_CHID_MASK, p->header[1]);

	if (ch_id >= pcie->nr_channels)
		return -EIO;

	can = pcie->can[ch_id];
	kvaser_pciefd_rx_error_frame(can, p);
	if (can->err_rep_cnt >= KVASER_PCIEFD_MAX_ERR_REP)
		/* Do not report more errors, until bec_poll_timer expires */
		kvaser_pciefd_disable_err_gen(can);
	/* Start polling the error counters */
	mod_timer(&can->bec_poll_timer, KVASER_PCIEFD_BEC_POLL_FREQ);

	return 0;
}

static int kvaser_pciefd_handle_status_resp(struct kvaser_pciefd_can *can,
					    struct kvaser_pciefd_rx_packet *p)
{
	struct can_berr_counter bec;
	enum can_state old_state, new_state, tx_state, rx_state;
	int ret = 0;

	old_state = can->can.state;

	bec.txerr = FIELD_GET(KVASER_PCIEFD_SPACK_TXERR_MASK, p->header[0]);
	bec.rxerr = FIELD_GET(KVASER_PCIEFD_SPACK_RXERR_MASK, p->header[0]);

	kvaser_pciefd_packet_to_state(p, &bec, &new_state, &tx_state, &rx_state);
	if (new_state != old_state) {
		struct net_device *ndev = can->can.dev;
		struct sk_buff *skb;
		struct can_frame *cf;

		skb = alloc_can_err_skb(ndev, &cf);
		kvaser_pciefd_change_state(can, &bec, cf, new_state, tx_state, rx_state);
		if (skb) {
			kvaser_pciefd_set_skb_timestamp(can->kv_pcie, skb, p->timestamp);
			netif_rx(skb);
		} else {
			ndev->stats.rx_dropped++;
			netdev_warn(ndev, "No memory left for err_skb\n");
			ret = -ENOMEM;
		}
	}
	can->bec.txerr = bec.txerr;
	can->bec.rxerr = bec.rxerr;
	/* Check if we need to poll the error counters */
	if (bec.txerr || bec.rxerr)
		mod_timer(&can->bec_poll_timer, KVASER_PCIEFD_BEC_POLL_FREQ);

	return ret;
}

static int kvaser_pciefd_handle_status_packet(struct kvaser_pciefd *pcie,
					      struct kvaser_pciefd_rx_packet *p)
{
	struct kvaser_pciefd_can *can;
	u8 cmdseq;
	u32 status;
	u8 ch_id = FIELD_GET(KVASER_PCIEFD_PACKET_CHID_MASK, p->header[1]);

	if (ch_id >= pcie->nr_channels)
		return -EIO;

	can = pcie->can[ch_id];

	status = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_STAT_REG);
	cmdseq = FIELD_GET(KVASER_PCIEFD_KCAN_STAT_SEQNO_MASK, status);

	/* Reset done, start abort and flush */
	if (p->header[0] & KVASER_PCIEFD_SPACK_IRM &&
	    p->header[0] & KVASER_PCIEFD_SPACK_RMCD &&
	    p->header[1] & KVASER_PCIEFD_SPACK_AUTO &&
	    cmdseq == FIELD_GET(KVASER_PCIEFD_PACKET_SEQ_MASK, p->header[1]) &&
	    status & KVASER_PCIEFD_KCAN_STAT_IDLE) {
		iowrite32(KVASER_PCIEFD_KCAN_IRQ_ABD,
			  can->reg_base + KVASER_PCIEFD_KCAN_IRQ_REG);
		kvaser_pciefd_abort_flush_reset(can);
	} else if (p->header[0] & KVASER_PCIEFD_SPACK_IDET &&
		   p->header[0] & KVASER_PCIEFD_SPACK_IRM &&
		   cmdseq == FIELD_GET(KVASER_PCIEFD_PACKET_SEQ_MASK, p->header[1]) &&
		   status & KVASER_PCIEFD_KCAN_STAT_IDLE) {
		/* Reset detected, send end of flush if no packet are in FIFO */
		u8 count;

		count = FIELD_GET(KVASER_PCIEFD_KCAN_TX_NR_PACKETS_CURRENT_MASK,
				  ioread32(can->reg_base + KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG));
		if (!count)
			iowrite32(FIELD_PREP(KVASER_PCIEFD_KCAN_CTRL_TYPE_MASK,
					     KVASER_PCIEFD_KCAN_CTRL_TYPE_EFLUSH),
				  can->reg_base + KVASER_PCIEFD_KCAN_CTRL_REG);
	} else if (!(p->header[1] & KVASER_PCIEFD_SPACK_AUTO) &&
		   cmdseq == FIELD_GET(KVASER_PCIEFD_PACKET_SEQ_MASK, p->header[1])) {
		/* Response to status request received */
		kvaser_pciefd_handle_status_resp(can, p);
		if (can->can.state != CAN_STATE_BUS_OFF &&
		    can->can.state != CAN_STATE_ERROR_ACTIVE) {
			mod_timer(&can->bec_poll_timer, KVASER_PCIEFD_BEC_POLL_FREQ);
		}
	} else if (p->header[0] & KVASER_PCIEFD_SPACK_RMCD &&
		   !(status & KVASER_PCIEFD_KCAN_STAT_BUS_OFF_MASK)) {
		/* Reset to bus on detected */
		if (!completion_done(&can->start_comp))
			complete(&can->start_comp);
	}

	return 0;
}

static void kvaser_pciefd_handle_nack_packet(struct kvaser_pciefd_can *can,
					     struct kvaser_pciefd_rx_packet *p)
{
	struct sk_buff *skb;
	struct can_frame *cf;

	skb = alloc_can_err_skb(can->can.dev, &cf);
	can->can.dev->stats.tx_errors++;
	if (p->header[0] & KVASER_PCIEFD_APACKET_ABL) {
		if (skb)
			cf->can_id |= CAN_ERR_LOSTARB;
		can->can.can_stats.arbitration_lost++;
	} else if (skb) {
		cf->can_id |= CAN_ERR_ACK;
	}

	if (skb) {
		cf->can_id |= CAN_ERR_BUSERROR;
		kvaser_pciefd_set_skb_timestamp(can->kv_pcie, skb, p->timestamp);
		netif_rx(skb);
	} else {
		can->can.dev->stats.rx_dropped++;
		netdev_warn(can->can.dev, "No memory left for err_skb\n");
	}
}

static int kvaser_pciefd_handle_ack_packet(struct kvaser_pciefd *pcie,
					   struct kvaser_pciefd_rx_packet *p)
{
	struct kvaser_pciefd_can *can;
	bool one_shot_fail = false;
	u8 ch_id = FIELD_GET(KVASER_PCIEFD_PACKET_CHID_MASK, p->header[1]);

	if (ch_id >= pcie->nr_channels)
		return -EIO;

	can = pcie->can[ch_id];
	/* Ignore control packet ACK */
	if (p->header[0] & KVASER_PCIEFD_APACKET_CT)
		return 0;

	if (p->header[0] & KVASER_PCIEFD_APACKET_NACK) {
		kvaser_pciefd_handle_nack_packet(can, p);
		one_shot_fail = true;
	}

	if (p->header[0] & KVASER_PCIEFD_APACKET_FLU) {
		netdev_dbg(can->can.dev, "Packet was flushed\n");
	} else {
		int echo_idx = FIELD_GET(KVASER_PCIEFD_PACKET_SEQ_MASK, p->header[0]);
		unsigned int len, frame_len = 0;
		struct sk_buff *skb;

		if (echo_idx != (can->ack_idx & (can->can.echo_skb_max - 1)))
			return 0;
		skb = can->can.echo_skb[echo_idx];
		if (!skb)
			return 0;
		kvaser_pciefd_set_skb_timestamp(pcie, skb, p->timestamp);
		len = can_get_echo_skb(can->can.dev, echo_idx, &frame_len);

		/* Pairs with barrier in kvaser_pciefd_start_xmit() */
		smp_store_release(&can->ack_idx, can->ack_idx + 1);
		can->completed_tx_pkts++;
		can->completed_tx_bytes += frame_len;

		if (!one_shot_fail) {
			can->can.dev->stats.tx_bytes += len;
			can->can.dev->stats.tx_packets++;
		}
	}

	return 0;
}

static int kvaser_pciefd_handle_eflush_packet(struct kvaser_pciefd *pcie,
					      struct kvaser_pciefd_rx_packet *p)
{
	struct kvaser_pciefd_can *can;
	u8 ch_id = FIELD_GET(KVASER_PCIEFD_PACKET_CHID_MASK, p->header[1]);

	if (ch_id >= pcie->nr_channels)
		return -EIO;

	can = pcie->can[ch_id];

	if (!completion_done(&can->flush_comp))
		complete(&can->flush_comp);

	return 0;
}

static int kvaser_pciefd_read_packet(struct kvaser_pciefd *pcie, int *start_pos,
				     int dma_buf)
{
	__le32 *buffer = pcie->dma_data[dma_buf];
	__le64 timestamp;
	struct kvaser_pciefd_rx_packet packet;
	struct kvaser_pciefd_rx_packet *p = &packet;
	u8 type;
	int pos = *start_pos;
	int size;
	int ret = 0;

	size = le32_to_cpu(buffer[pos++]);
	if (!size) {
		*start_pos = 0;
		return 0;
	}

	p->header[0] = le32_to_cpu(buffer[pos++]);
	p->header[1] = le32_to_cpu(buffer[pos++]);

	/* Read 64-bit timestamp */
	memcpy(&timestamp, &buffer[pos], sizeof(__le64));
	pos += 2;
	p->timestamp = le64_to_cpu(timestamp);

	type = FIELD_GET(KVASER_PCIEFD_PACKET_TYPE_MASK, p->header[1]);
	switch (type) {
	case KVASER_PCIEFD_PACK_TYPE_DATA:
		ret = kvaser_pciefd_handle_data_packet(pcie, p, &buffer[pos]);
		if (!(p->header[0] & KVASER_PCIEFD_RPACKET_RTR)) {
			u8 data_len;

			data_len = can_fd_dlc2len(FIELD_GET(KVASER_PCIEFD_RPACKET_DLC_MASK,
							    p->header[1]));
			pos += DIV_ROUND_UP(data_len, 4);
		}
		break;

	case KVASER_PCIEFD_PACK_TYPE_ACK:
		ret = kvaser_pciefd_handle_ack_packet(pcie, p);
		break;

	case KVASER_PCIEFD_PACK_TYPE_STATUS:
		ret = kvaser_pciefd_handle_status_packet(pcie, p);
		break;

	case KVASER_PCIEFD_PACK_TYPE_ERROR:
		ret = kvaser_pciefd_handle_error_packet(pcie, p);
		break;

	case KVASER_PCIEFD_PACK_TYPE_EFLUSH_ACK:
		ret = kvaser_pciefd_handle_eflush_packet(pcie, p);
		break;

	case KVASER_PCIEFD_PACK_TYPE_ACK_DATA:
	case KVASER_PCIEFD_PACK_TYPE_BUS_LOAD:
	case KVASER_PCIEFD_PACK_TYPE_EFRAME_ACK:
	case KVASER_PCIEFD_PACK_TYPE_TXRQ:
		dev_info(&pcie->pci->dev,
			 "Received unexpected packet type 0x%08X\n", type);
		break;

	default:
		dev_err(&pcie->pci->dev, "Unknown packet type 0x%08X\n", type);
		ret = -EIO;
		break;
	}

	if (ret)
		return ret;

	/* Position does not point to the end of the package,
	 * corrupted packet size?
	 */
	if (unlikely((*start_pos + size) != pos))
		return -EIO;

	/* Point to the next packet header, if any */
	*start_pos = pos;

	return ret;
}

static int kvaser_pciefd_read_buffer(struct kvaser_pciefd *pcie, int dma_buf)
{
	int pos = 0;
	int res = 0;
	unsigned int i;

	do {
		res = kvaser_pciefd_read_packet(pcie, &pos, dma_buf);
	} while (!res && pos > 0 && pos < KVASER_PCIEFD_DMA_SIZE);

	/* Report ACKs in this buffer to BQL en masse for correct periods */
	for (i = 0; i < pcie->nr_channels; ++i) {
		struct kvaser_pciefd_can *can = pcie->can[i];

		if (!can->completed_tx_pkts)
			continue;
		netif_subqueue_completed_wake(can->can.dev, 0,
					      can->completed_tx_pkts,
					      can->completed_tx_bytes,
					      kvaser_pciefd_tx_avail(can), 1);
		can->completed_tx_pkts = 0;
		can->completed_tx_bytes = 0;
	}

	return res;
}

static void kvaser_pciefd_receive_irq(struct kvaser_pciefd *pcie)
{
	void __iomem *srb_cmd_reg = KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_CMD_REG;
	u32 irq = ioread32(KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_IRQ_REG);

	iowrite32(irq, KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_IRQ_REG);

	if (irq & KVASER_PCIEFD_SRB_IRQ_DPD0) {
		kvaser_pciefd_read_buffer(pcie, 0);
		iowrite32(KVASER_PCIEFD_SRB_CMD_RDB0, srb_cmd_reg); /* Rearm buffer */
	}

	if (irq & KVASER_PCIEFD_SRB_IRQ_DPD1) {
		kvaser_pciefd_read_buffer(pcie, 1);
		iowrite32(KVASER_PCIEFD_SRB_CMD_RDB1, srb_cmd_reg); /* Rearm buffer */
	}

	if (unlikely(irq & KVASER_PCIEFD_SRB_IRQ_DOF0 ||
		     irq & KVASER_PCIEFD_SRB_IRQ_DOF1 ||
		     irq & KVASER_PCIEFD_SRB_IRQ_DUF0 ||
		     irq & KVASER_PCIEFD_SRB_IRQ_DUF1))
		dev_err(&pcie->pci->dev, "DMA IRQ error 0x%08X\n", irq);
}

static void kvaser_pciefd_transmit_irq(struct kvaser_pciefd_can *can)
{
	u32 irq = ioread32(can->reg_base + KVASER_PCIEFD_KCAN_IRQ_REG);

	if (irq & KVASER_PCIEFD_KCAN_IRQ_TOF)
		netdev_err(can->can.dev, "Tx FIFO overflow\n");

	if (irq & KVASER_PCIEFD_KCAN_IRQ_BPP)
		netdev_err(can->can.dev,
			   "Fail to change bittiming, when not in reset mode\n");

	if (irq & KVASER_PCIEFD_KCAN_IRQ_FDIC)
		netdev_err(can->can.dev, "CAN FD frame in CAN mode\n");

	if (irq & KVASER_PCIEFD_KCAN_IRQ_ROF)
		netdev_err(can->can.dev, "Rx FIFO overflow\n");

	iowrite32(irq, can->reg_base + KVASER_PCIEFD_KCAN_IRQ_REG);
}

static irqreturn_t kvaser_pciefd_irq_handler(int irq, void *dev)
{
	struct kvaser_pciefd *pcie = (struct kvaser_pciefd *)dev;
	const struct kvaser_pciefd_irq_mask *irq_mask = pcie->driver_data->irq_mask;
	u32 pci_irq = ioread32(KVASER_PCIEFD_PCI_IRQ_ADDR(pcie));
	int i;

	if (!(pci_irq & irq_mask->all))
		return IRQ_NONE;

	iowrite32(0, KVASER_PCIEFD_PCI_IEN_ADDR(pcie));

	if (pci_irq & irq_mask->kcan_rx0)
		kvaser_pciefd_receive_irq(pcie);

	for (i = 0; i < pcie->nr_channels; i++) {
		if (pci_irq & irq_mask->kcan_tx[i])
			kvaser_pciefd_transmit_irq(pcie->can[i]);
	}

	iowrite32(irq_mask->all, KVASER_PCIEFD_PCI_IEN_ADDR(pcie));

	return IRQ_HANDLED;
}

static void kvaser_pciefd_teardown_can_ctrls(struct kvaser_pciefd *pcie)
{
	int i;

	for (i = 0; i < pcie->nr_channels; i++) {
		struct kvaser_pciefd_can *can = pcie->can[i];

		if (can) {
			iowrite32(0, can->reg_base + KVASER_PCIEFD_KCAN_IEN_REG);
			kvaser_pciefd_pwm_stop(can);
			kvaser_pciefd_devlink_port_unregister(can);
			free_candev(can->can.dev);
		}
	}
}

static void kvaser_pciefd_disable_irq_srcs(struct kvaser_pciefd *pcie)
{
	unsigned int i;

	/* Masking PCI_IRQ is insufficient as running ISR will unmask it */
	iowrite32(0, KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_IEN_REG);
	for (i = 0; i < pcie->nr_channels; ++i)
		iowrite32(0, pcie->can[i]->reg_base + KVASER_PCIEFD_KCAN_IEN_REG);
}

static int kvaser_pciefd_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	int ret;
	struct devlink *devlink;
	struct device *dev = &pdev->dev;
	struct kvaser_pciefd *pcie;
	const struct kvaser_pciefd_irq_mask *irq_mask;

	devlink = devlink_alloc(&kvaser_pciefd_devlink_ops, sizeof(*pcie), dev);
	if (!devlink)
		return -ENOMEM;

	pcie = devlink_priv(devlink);
	pci_set_drvdata(pdev, pcie);
	pcie->pci = pdev;
	pcie->driver_data = (const struct kvaser_pciefd_driver_data *)id->driver_data;
	irq_mask = pcie->driver_data->irq_mask;

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_free_devlink;

	ret = pci_request_regions(pdev, KVASER_PCIEFD_DRV_NAME);
	if (ret)
		goto err_disable_pci;

	pcie->reg_base = pci_iomap(pdev, 0, 0);
	if (!pcie->reg_base) {
		ret = -ENOMEM;
		goto err_release_regions;
	}

	ret = kvaser_pciefd_setup_board(pcie);
	if (ret)
		goto err_pci_iounmap;

	ret = kvaser_pciefd_setup_dma(pcie);
	if (ret)
		goto err_pci_iounmap;

	pci_set_master(pdev);

	ret = kvaser_pciefd_setup_can_ctrls(pcie);
	if (ret)
		goto err_teardown_can_ctrls;

	ret = pci_alloc_irq_vectors(pcie->pci, 1, 1, PCI_IRQ_INTX | PCI_IRQ_MSI);
	if (ret < 0) {
		dev_err(dev, "Failed to allocate IRQ vectors.\n");
		goto err_teardown_can_ctrls;
	}

	ret = pci_irq_vector(pcie->pci, 0);
	if (ret < 0)
		goto err_pci_free_irq_vectors;

	pcie->pci->irq = ret;

	ret = request_irq(pcie->pci->irq, kvaser_pciefd_irq_handler,
			  IRQF_SHARED, KVASER_PCIEFD_DRV_NAME, pcie);
	if (ret) {
		dev_err(dev, "Failed to request IRQ %d\n", pcie->pci->irq);
		goto err_pci_free_irq_vectors;
	}
	iowrite32(KVASER_PCIEFD_SRB_IRQ_DPD0 | KVASER_PCIEFD_SRB_IRQ_DPD1,
		  KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_IRQ_REG);

	iowrite32(KVASER_PCIEFD_SRB_IRQ_DPD0 | KVASER_PCIEFD_SRB_IRQ_DPD1 |
		  KVASER_PCIEFD_SRB_IRQ_DOF0 | KVASER_PCIEFD_SRB_IRQ_DOF1 |
		  KVASER_PCIEFD_SRB_IRQ_DUF0 | KVASER_PCIEFD_SRB_IRQ_DUF1,
		  KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_IEN_REG);

	/* Enable PCI interrupts */
	iowrite32(irq_mask->all, KVASER_PCIEFD_PCI_IEN_ADDR(pcie));
	/* Ready the DMA buffers */
	iowrite32(KVASER_PCIEFD_SRB_CMD_RDB0,
		  KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_CMD_REG);
	iowrite32(KVASER_PCIEFD_SRB_CMD_RDB1,
		  KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_CMD_REG);

	ret = kvaser_pciefd_reg_candev(pcie);
	if (ret)
		goto err_free_irq;

	devlink_register(devlink);

	return 0;

err_free_irq:
	kvaser_pciefd_disable_irq_srcs(pcie);
	free_irq(pcie->pci->irq, pcie);

err_pci_free_irq_vectors:
	pci_free_irq_vectors(pcie->pci);

err_teardown_can_ctrls:
	kvaser_pciefd_teardown_can_ctrls(pcie);
	iowrite32(0, KVASER_PCIEFD_SRB_ADDR(pcie) + KVASER_PCIEFD_SRB_CTRL_REG);
	pci_clear_master(pdev);

err_pci_iounmap:
	pci_iounmap(pdev, pcie->reg_base);

err_release_regions:
	pci_release_regions(pdev);

err_disable_pci:
	pci_disable_device(pdev);

err_free_devlink:
	devlink_free(devlink);

	return ret;
}

static void kvaser_pciefd_remove(struct pci_dev *pdev)
{
	struct kvaser_pciefd *pcie = pci_get_drvdata(pdev);
	unsigned int i;

	for (i = 0; i < pcie->nr_channels; ++i) {
		struct kvaser_pciefd_can *can = pcie->can[i];

		unregister_candev(can->can.dev);
		timer_delete(&can->bec_poll_timer);
		kvaser_pciefd_pwm_stop(can);
		kvaser_pciefd_devlink_port_unregister(can);
	}

	kvaser_pciefd_disable_irq_srcs(pcie);
	free_irq(pcie->pci->irq, pcie);
	pci_free_irq_vectors(pcie->pci);

	for (i = 0; i < pcie->nr_channels; ++i)
		free_candev(pcie->can[i]->can.dev);

	devlink_unregister(priv_to_devlink(pcie));
	pci_iounmap(pdev, pcie->reg_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	devlink_free(priv_to_devlink(pcie));
}

static struct pci_driver kvaser_pciefd = {
	.name = KVASER_PCIEFD_DRV_NAME,
	.id_table = kvaser_pciefd_id_table,
	.probe = kvaser_pciefd_probe,
	.remove = kvaser_pciefd_remove,
};

module_pci_driver(kvaser_pciefd)
