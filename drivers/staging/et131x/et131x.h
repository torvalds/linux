/*
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#define DRIVER_NAME "et131x"
#define DRIVER_VERSION "v2.0"

/* EEPROM defines */

/* LBCIF Register Groups (addressed via 32-bit offsets) */
#define LBCIF_DWORD0_GROUP       0xAC
#define LBCIF_DWORD1_GROUP       0xB0

/* LBCIF Registers (addressed via 8-bit offsets) */
#define LBCIF_ADDRESS_REGISTER   0xAC
#define LBCIF_DATA_REGISTER      0xB0
#define LBCIF_CONTROL_REGISTER   0xB1
#define LBCIF_STATUS_REGISTER    0xB2

/* LBCIF Control Register Bits */
#define LBCIF_CONTROL_SEQUENTIAL_READ   0x01
#define LBCIF_CONTROL_PAGE_WRITE        0x02
#define LBCIF_CONTROL_EEPROM_RELOAD     0x08
#define LBCIF_CONTROL_TWO_BYTE_ADDR     0x20
#define LBCIF_CONTROL_I2C_WRITE         0x40
#define LBCIF_CONTROL_LBCIF_ENABLE      0x80

/* LBCIF Status Register Bits */
#define LBCIF_STATUS_PHY_QUEUE_AVAIL    0x01
#define LBCIF_STATUS_I2C_IDLE           0x02
#define LBCIF_STATUS_ACK_ERROR          0x04
#define LBCIF_STATUS_GENERAL_ERROR      0x08
#define LBCIF_STATUS_CHECKSUM_ERROR     0x40
#define LBCIF_STATUS_EEPROM_PRESENT     0x80

/* Miscellaneous Constraints */
#define MAX_NUM_REGISTER_POLLS          1000
#define MAX_NUM_WRITE_RETRIES           2

/* MAC defines */
#define COUNTER_WRAP_16_BIT 0x10000
#define COUNTER_WRAP_12_BIT 0x1000

/* PCI defines */
#define INTERNAL_MEM_SIZE       0x400	/* 1024 of internal memory */
#define INTERNAL_MEM_RX_OFFSET  0x1FF	/* 50%   Tx, 50%   Rx */

/* ISR defines */
/*
 * For interrupts, normal running is:
 *       rxdma_xfr_done, phy_interrupt, mac_stat_interrupt,
 *       watchdog_interrupt & txdma_xfer_done
 *
 * In both cases, when flow control is enabled for either Tx or bi-direction,
 * we additional enable rx_fbr0_low and rx_fbr1_low, so we know when the
 * buffer rings are running low.
 */
#define INT_MASK_DISABLE            0xffffffff

/* NOTE: Masking out MAC_STAT Interrupt for now...
 * #define INT_MASK_ENABLE             0xfff6bf17
 * #define INT_MASK_ENABLE_NO_FLOW     0xfff6bfd7
 */
#define INT_MASK_ENABLE             0xfffebf17
#define INT_MASK_ENABLE_NO_FLOW     0xfffebfd7


/* et131x_eeprom.c */
int et131x_init_eeprom(struct et131x_adapter *adapter);

/* et131x_initpci.c */
void et131x_error_timer_handler(unsigned long data);
void et131x_configure_global_regs(struct et131x_adapter *adapter);
void et131x_enable_interrupts(struct et131x_adapter *adapter);
void et131x_disable_interrupts(struct et131x_adapter *adapter);
void et131x_align_allocated_memory(struct et131x_adapter *adapter,
				   u64 *phys_addr,
				   u64 *offset, u64 mask);

void et131x_adapter_setup(struct et131x_adapter *adapter);
int et131x_adapter_memory_alloc(struct et131x_adapter *adapter);
void et131x_adapter_memory_free(struct et131x_adapter *adapter);
void et131x_hwaddr_init(struct et131x_adapter *adapter);
void et131x_soft_reset(struct et131x_adapter *adapter);

/* et131x_isr.c */
irqreturn_t et131x_isr(int irq, void *dev_id);
void et131x_isr_handler(struct work_struct *work);

/* et1310_mac.c */
void et1310_config_mac_regs1(struct et131x_adapter *adapter);
void et1310_config_mac_regs2(struct et131x_adapter *adapter);
void et1310_config_rxmac_regs(struct et131x_adapter *adapter);
void et1310_config_txmac_regs(struct et131x_adapter *adapter);
void et1310_config_macstat_regs(struct et131x_adapter *adapter);
void et1310_config_flow_control(struct et131x_adapter *adapter);
void et1310_update_macstat_host_counters(struct et131x_adapter *adapter);
void et1310_handle_macstat_interrupt(struct et131x_adapter *adapter);
void et1310_setup_device_for_multicast(struct et131x_adapter *adapter);
void et1310_setup_device_for_unicast(struct et131x_adapter *adapter);

/* et131x_netdev.c */
int et131x_open(struct net_device *netdev);
int et131x_close(struct net_device *netdev);
void et131x_up(struct net_device *netdev);
void et131x_down(struct net_device *netdev);
struct net_device *et131x_device_alloc(void);
void et131x_enable_txrx(struct net_device *netdev);
void et131x_disable_txrx(struct net_device *netdev);

/* et1310_pm.c */
int et1310_in_phy_coma(struct et131x_adapter *adapter);
void et1310_enable_phy_coma(struct et131x_adapter *adapter);
void et1310_disable_phy_coma(struct et131x_adapter *adapter);

/* et1310_phy.c */
void et1310_phy_power_down(struct et131x_adapter *adapter, bool down);
void et1310_phy_access_mii_bit(struct et131x_adapter *adapter,
			       u16 action,
			       u16 regnum, u16 bitnum, u8 *value);

void et131x_xcvr_init(struct et131x_adapter *adapter);

/* static inline function does not work because et131x_adapter is not always
 * defined
 */
int et131x_phy_mii_read(struct et131x_adapter *adapter, u8 addr,
	      u8 reg, u16 *value);
int32_t et131x_mii_read(struct et131x_adapter *adapter,
		u8 reg, u16 *value);
int32_t et131x_mii_write(struct et131x_adapter *adapter,
		u8 reg, u16 value);

int et131x_mdio_read(struct mii_bus *bus, int phy_addr, int reg);
int et131x_mdio_write(struct mii_bus *bus, int phy_addr, int reg, u16 value);
int et131x_mdio_reset(struct mii_bus *bus);

/* et1310_rx.c */
int et131x_rx_dma_memory_alloc(struct et131x_adapter *adapter);
void et131x_rx_dma_memory_free(struct et131x_adapter *adapter);
int et131x_rfd_resources_alloc(struct et131x_adapter *adapter,
			       struct rfd *rfd);
void et131x_rfd_resources_free(struct et131x_adapter *adapter,
			       struct rfd *rfd);
int et131x_init_recv(struct et131x_adapter *adapter);

void et131x_config_rx_dma_regs(struct et131x_adapter *adapter);
void et131x_set_rx_dma_timer(struct et131x_adapter *adapter);
void et131x_rx_dma_disable(struct et131x_adapter *adapter);
void et131x_rx_dma_enable(struct et131x_adapter *adapter);

void et131x_reset_recv(struct et131x_adapter *adapter);

void et131x_handle_recv_interrupt(struct et131x_adapter *adapter);

/* et131x_tx.c */
int et131x_tx_dma_memory_alloc(struct et131x_adapter *adapter);
void et131x_tx_dma_memory_free(struct et131x_adapter *adapter);
void et131x_config_tx_dma_regs(struct et131x_adapter *adapter);
void et131x_init_send(struct et131x_adapter *adapter);
void et131x_tx_dma_disable(struct et131x_adapter *adapter);
void et131x_tx_dma_enable(struct et131x_adapter *adapter);
void et131x_handle_send_interrupt(struct et131x_adapter *adapter);
void et131x_free_busy_send_packets(struct et131x_adapter *adapter);
int et131x_send_packets(struct sk_buff *skb, struct net_device *netdev);

/* et131x_ethtool.c */
void et131x_set_ethtool_ops(struct net_device *netdev);
