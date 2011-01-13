/*
 * Merged from files
 *
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

/* et131x_eeprom.c */
int et131x_init_eeprom(struct et131x_adapter *etdev);

/* et131x_initpci.c */
void ConfigGlobalRegs(struct et131x_adapter *pAdapter);
void ConfigMMCRegs(struct et131x_adapter *pAdapter);
void et131x_enable_interrupts(struct et131x_adapter *adapter);
void et131x_disable_interrupts(struct et131x_adapter *adapter);
void et131x_align_allocated_memory(struct et131x_adapter *adapter,
				   u64 *phys_addr,
				   u64 *offset, u64 mask);

int et131x_adapter_setup(struct et131x_adapter *adapter);
int et131x_adapter_memory_alloc(struct et131x_adapter *adapter);
void et131x_adapter_memory_free(struct et131x_adapter *adapter);
void et131x_hwaddr_init(struct et131x_adapter *adapter);
void et131x_soft_reset(struct et131x_adapter *adapter);

/* et131x_isr.c */
irqreturn_t et131x_isr(int irq, void *dev_id);
void et131x_isr_handler(struct work_struct *work);

/* et1310_mac.c */
void ConfigMACRegs1(struct et131x_adapter *adapter);
void ConfigMACRegs2(struct et131x_adapter *adapter);
void ConfigRxMacRegs(struct et131x_adapter *adapter);
void ConfigTxMacRegs(struct et131x_adapter *adapter);
void ConfigMacStatRegs(struct et131x_adapter *adapter);
void ConfigFlowControl(struct et131x_adapter *adapter);
void UpdateMacStatHostCounters(struct et131x_adapter *adapter);
void HandleMacStatInterrupt(struct et131x_adapter *adapter);
void SetupDeviceForMulticast(struct et131x_adapter *adapter);
void SetupDeviceForUnicast(struct et131x_adapter *adapter);

/* et131x_netdev.c */
struct net_device *et131x_device_alloc(void);

/* et131x_pm.c */
void EnablePhyComa(struct et131x_adapter *adapter);
void DisablePhyComa(struct et131x_adapter *adapter);

/* et131x_phy.c */
void ET1310_PhyInit(struct et131x_adapter *adapter);
void ET1310_PhyReset(struct et131x_adapter *adapter);
void ET1310_PhyPowerDown(struct et131x_adapter *adapter, bool down);
void ET1310_PhyAdvertise1000BaseT(struct et131x_adapter *adapter,
				  u16 duplex);
void ET1310_PhyAccessMiBit(struct et131x_adapter *adapter,
			   u16 action,
			   u16 regnum, u16 bitnum, u8 *value);

int et131x_xcvr_find(struct et131x_adapter *adapter);
void et131x_setphy_normal(struct et131x_adapter *adapter);

/* static inline function does not work because et131x_adapter is not always
 * defined
 */
int PhyMiRead(struct et131x_adapter *adapter, u8 xcvrAddr,
	      u8 xcvrReg, u16 *value);
#define MiRead(adapter, xcvrReg, value) \
	PhyMiRead((adapter), (adapter)->Stats.xcvr_addr, (xcvrReg), (value))

int32_t MiWrite(struct et131x_adapter *adapter,
		u8 xcvReg, u16 value);
void et131x_Mii_check(struct et131x_adapter *pAdapter,
		      MI_BMSR_t bmsr, MI_BMSR_t bmsr_ints);

/* This last is not strictly required (the driver could call the TPAL
 * version instead), but this sets the adapter up correctly, and calls the
 * access routine indirectly.  This protects the driver from changes in TPAL.
 */
void SetPhy_10BaseTHalfDuplex(struct et131x_adapter *adapter);


/* et1310_rx.c */
int et131x_rx_dma_memory_alloc(struct et131x_adapter *adapter);
void et131x_rx_dma_memory_free(struct et131x_adapter *adapter);
int et131x_rfd_resources_alloc(struct et131x_adapter *adapter,
			       struct _MP_RFD *pMpRfd);
void et131x_rfd_resources_free(struct et131x_adapter *adapter,
			       struct _MP_RFD *pMpRfd);
int et131x_init_recv(struct et131x_adapter *adapter);

void ConfigRxDmaRegs(struct et131x_adapter *adapter);
void SetRxDmaTimer(struct et131x_adapter *adapter);
void et131x_rx_dma_disable(struct et131x_adapter *adapter);
void et131x_rx_dma_enable(struct et131x_adapter *adapter);

void et131x_reset_recv(struct et131x_adapter *adapter);

void et131x_handle_recv_interrupt(struct et131x_adapter *adapter);

/* et131x_tx.c */
int et131x_tx_dma_memory_alloc(struct et131x_adapter *adapter);
void et131x_tx_dma_memory_free(struct et131x_adapter *adapter);
void ConfigTxDmaRegs(struct et131x_adapter *adapter);
void et131x_init_send(struct et131x_adapter *adapter);
void et131x_tx_dma_disable(struct et131x_adapter *adapter);
void et131x_tx_dma_enable(struct et131x_adapter *adapter);
void et131x_handle_send_interrupt(struct et131x_adapter *adapter);
void et131x_free_busy_send_packets(struct et131x_adapter *adapter);
int et131x_send_packets(struct sk_buff *skb, struct net_device *netdev);

