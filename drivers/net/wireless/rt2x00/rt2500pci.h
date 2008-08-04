/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2500pci
	Abstract: Data structures and registers for the rt2500pci module.
	Supported chipsets: RT2560.
 */

#ifndef RT2500PCI_H
#define RT2500PCI_H

/*
 * RF chip defines.
 */
#define RF2522				0x0000
#define RF2523				0x0001
#define RF2524				0x0002
#define RF2525				0x0003
#define RF2525E				0x0004
#define RF5222				0x0010

/*
 * RT2560 version
 */
#define RT2560_VERSION_B		2
#define RT2560_VERSION_C		3
#define RT2560_VERSION_D		4

/*
 * Signal information.
 * Defaul offset is required for RSSI <-> dBm conversion.
 */
#define DEFAULT_RSSI_OFFSET		121

/*
 * Register layout information.
 */
#define CSR_REG_BASE			0x0000
#define CSR_REG_SIZE			0x0174
#define EEPROM_BASE			0x0000
#define EEPROM_SIZE			0x0200
#define BBP_SIZE			0x0040
#define RF_SIZE				0x0014

/*
 * Number of TX queues.
 */
#define NUM_TX_QUEUES			2

/*
 * Control/Status Registers(CSR).
 * Some values are set in TU, whereas 1 TU == 1024 us.
 */

/*
 * CSR0: ASIC revision number.
 */
#define CSR0				0x0000

/*
 * CSR1: System control register.
 * SOFT_RESET: Software reset, 1: reset, 0: normal.
 * BBP_RESET: Hardware reset, 1: reset, 0, release.
 * HOST_READY: Host ready after initialization.
 */
#define CSR1				0x0004
#define CSR1_SOFT_RESET			FIELD32(0x00000001)
#define CSR1_BBP_RESET			FIELD32(0x00000002)
#define CSR1_HOST_READY			FIELD32(0x00000004)

/*
 * CSR2: System admin status register (invalid).
 */
#define CSR2				0x0008

/*
 * CSR3: STA MAC address register 0.
 */
#define CSR3				0x000c
#define CSR3_BYTE0			FIELD32(0x000000ff)
#define CSR3_BYTE1			FIELD32(0x0000ff00)
#define CSR3_BYTE2			FIELD32(0x00ff0000)
#define CSR3_BYTE3			FIELD32(0xff000000)

/*
 * CSR4: STA MAC address register 1.
 */
#define CSR4				0x0010
#define CSR4_BYTE4			FIELD32(0x000000ff)
#define CSR4_BYTE5			FIELD32(0x0000ff00)

/*
 * CSR5: BSSID register 0.
 */
#define CSR5				0x0014
#define CSR5_BYTE0			FIELD32(0x000000ff)
#define CSR5_BYTE1			FIELD32(0x0000ff00)
#define CSR5_BYTE2			FIELD32(0x00ff0000)
#define CSR5_BYTE3			FIELD32(0xff000000)

/*
 * CSR6: BSSID register 1.
 */
#define CSR6				0x0018
#define CSR6_BYTE4			FIELD32(0x000000ff)
#define CSR6_BYTE5			FIELD32(0x0000ff00)

/*
 * CSR7: Interrupt source register.
 * Write 1 to clear.
 * TBCN_EXPIRE: Beacon timer expired interrupt.
 * TWAKE_EXPIRE: Wakeup timer expired interrupt.
 * TATIMW_EXPIRE: Timer of atim window expired interrupt.
 * TXDONE_TXRING: Tx ring transmit done interrupt.
 * TXDONE_ATIMRING: Atim ring transmit done interrupt.
 * TXDONE_PRIORING: Priority ring transmit done interrupt.
 * RXDONE: Receive done interrupt.
 * DECRYPTION_DONE: Decryption done interrupt.
 * ENCRYPTION_DONE: Encryption done interrupt.
 * UART1_TX_TRESHOLD: UART1 TX reaches threshold.
 * UART1_RX_TRESHOLD: UART1 RX reaches threshold.
 * UART1_IDLE_TRESHOLD: UART1 IDLE over threshold.
 * UART1_TX_BUFF_ERROR: UART1 TX buffer error.
 * UART1_RX_BUFF_ERROR: UART1 RX buffer error.
 * UART2_TX_TRESHOLD: UART2 TX reaches threshold.
 * UART2_RX_TRESHOLD: UART2 RX reaches threshold.
 * UART2_IDLE_TRESHOLD: UART2 IDLE over threshold.
 * UART2_TX_BUFF_ERROR: UART2 TX buffer error.
 * UART2_RX_BUFF_ERROR: UART2 RX buffer error.
 * TIMER_CSR3_EXPIRE: TIMECSR3 timer expired (802.1H quiet period).

 */
#define CSR7				0x001c
#define CSR7_TBCN_EXPIRE		FIELD32(0x00000001)
#define CSR7_TWAKE_EXPIRE		FIELD32(0x00000002)
#define CSR7_TATIMW_EXPIRE		FIELD32(0x00000004)
#define CSR7_TXDONE_TXRING		FIELD32(0x00000008)
#define CSR7_TXDONE_ATIMRING		FIELD32(0x00000010)
#define CSR7_TXDONE_PRIORING		FIELD32(0x00000020)
#define CSR7_RXDONE			FIELD32(0x00000040)
#define CSR7_DECRYPTION_DONE		FIELD32(0x00000080)
#define CSR7_ENCRYPTION_DONE		FIELD32(0x00000100)
#define CSR7_UART1_TX_TRESHOLD		FIELD32(0x00000200)
#define CSR7_UART1_RX_TRESHOLD		FIELD32(0x00000400)
#define CSR7_UART1_IDLE_TRESHOLD	FIELD32(0x00000800)
#define CSR7_UART1_TX_BUFF_ERROR	FIELD32(0x00001000)
#define CSR7_UART1_RX_BUFF_ERROR	FIELD32(0x00002000)
#define CSR7_UART2_TX_TRESHOLD		FIELD32(0x00004000)
#define CSR7_UART2_RX_TRESHOLD		FIELD32(0x00008000)
#define CSR7_UART2_IDLE_TRESHOLD	FIELD32(0x00010000)
#define CSR7_UART2_TX_BUFF_ERROR	FIELD32(0x00020000)
#define CSR7_UART2_RX_BUFF_ERROR	FIELD32(0x00040000)
#define CSR7_TIMER_CSR3_EXPIRE		FIELD32(0x00080000)

/*
 * CSR8: Interrupt mask register.
 * Write 1 to mask interrupt.
 * TBCN_EXPIRE: Beacon timer expired interrupt.
 * TWAKE_EXPIRE: Wakeup timer expired interrupt.
 * TATIMW_EXPIRE: Timer of atim window expired interrupt.
 * TXDONE_TXRING: Tx ring transmit done interrupt.
 * TXDONE_ATIMRING: Atim ring transmit done interrupt.
 * TXDONE_PRIORING: Priority ring transmit done interrupt.
 * RXDONE: Receive done interrupt.
 * DECRYPTION_DONE: Decryption done interrupt.
 * ENCRYPTION_DONE: Encryption done interrupt.
 * UART1_TX_TRESHOLD: UART1 TX reaches threshold.
 * UART1_RX_TRESHOLD: UART1 RX reaches threshold.
 * UART1_IDLE_TRESHOLD: UART1 IDLE over threshold.
 * UART1_TX_BUFF_ERROR: UART1 TX buffer error.
 * UART1_RX_BUFF_ERROR: UART1 RX buffer error.
 * UART2_TX_TRESHOLD: UART2 TX reaches threshold.
 * UART2_RX_TRESHOLD: UART2 RX reaches threshold.
 * UART2_IDLE_TRESHOLD: UART2 IDLE over threshold.
 * UART2_TX_BUFF_ERROR: UART2 TX buffer error.
 * UART2_RX_BUFF_ERROR: UART2 RX buffer error.
 * TIMER_CSR3_EXPIRE: TIMECSR3 timer expired (802.1H quiet period).
 */
#define CSR8				0x0020
#define CSR8_TBCN_EXPIRE		FIELD32(0x00000001)
#define CSR8_TWAKE_EXPIRE		FIELD32(0x00000002)
#define CSR8_TATIMW_EXPIRE		FIELD32(0x00000004)
#define CSR8_TXDONE_TXRING		FIELD32(0x00000008)
#define CSR8_TXDONE_ATIMRING		FIELD32(0x00000010)
#define CSR8_TXDONE_PRIORING		FIELD32(0x00000020)
#define CSR8_RXDONE			FIELD32(0x00000040)
#define CSR8_DECRYPTION_DONE		FIELD32(0x00000080)
#define CSR8_ENCRYPTION_DONE		FIELD32(0x00000100)
#define CSR8_UART1_TX_TRESHOLD		FIELD32(0x00000200)
#define CSR8_UART1_RX_TRESHOLD		FIELD32(0x00000400)
#define CSR8_UART1_IDLE_TRESHOLD	FIELD32(0x00000800)
#define CSR8_UART1_TX_BUFF_ERROR	FIELD32(0x00001000)
#define CSR8_UART1_RX_BUFF_ERROR	FIELD32(0x00002000)
#define CSR8_UART2_TX_TRESHOLD		FIELD32(0x00004000)
#define CSR8_UART2_RX_TRESHOLD		FIELD32(0x00008000)
#define CSR8_UART2_IDLE_TRESHOLD	FIELD32(0x00010000)
#define CSR8_UART2_TX_BUFF_ERROR	FIELD32(0x00020000)
#define CSR8_UART2_RX_BUFF_ERROR	FIELD32(0x00040000)
#define CSR8_TIMER_CSR3_EXPIRE		FIELD32(0x00080000)

/*
 * CSR9: Maximum frame length register.
 * MAX_FRAME_UNIT: Maximum frame length in 128b unit, default: 12.
 */
#define CSR9				0x0024
#define CSR9_MAX_FRAME_UNIT		FIELD32(0x00000f80)

/*
 * SECCSR0: WEP control register.
 * KICK_DECRYPT: Kick decryption engine, self-clear.
 * ONE_SHOT: 0: ring mode, 1: One shot only mode.
 * DESC_ADDRESS: Descriptor physical address of frame.
 */
#define SECCSR0				0x0028
#define SECCSR0_KICK_DECRYPT		FIELD32(0x00000001)
#define SECCSR0_ONE_SHOT		FIELD32(0x00000002)
#define SECCSR0_DESC_ADDRESS		FIELD32(0xfffffffc)

/*
 * CSR11: Back-off control register.
 * CWMIN: CWmin. Default cwmin is 31 (2^5 - 1).
 * CWMAX: CWmax. Default cwmax is 1023 (2^10 - 1).
 * SLOT_TIME: Slot time, default is 20us for 802.11b
 * CW_SELECT: CWmin/CWmax selection, 1: Register, 0: TXD.
 * LONG_RETRY: Long retry count.
 * SHORT_RETRY: Short retry count.
 */
#define CSR11				0x002c
#define CSR11_CWMIN			FIELD32(0x0000000f)
#define CSR11_CWMAX			FIELD32(0x000000f0)
#define CSR11_SLOT_TIME			FIELD32(0x00001f00)
#define CSR11_CW_SELECT			FIELD32(0x00002000)
#define CSR11_LONG_RETRY		FIELD32(0x00ff0000)
#define CSR11_SHORT_RETRY		FIELD32(0xff000000)

/*
 * CSR12: Synchronization configuration register 0.
 * All units in 1/16 TU.
 * BEACON_INTERVAL: Beacon interval, default is 100 TU.
 * CFP_MAX_DURATION: Cfp maximum duration, default is 100 TU.
 */
#define CSR12				0x0030
#define CSR12_BEACON_INTERVAL		FIELD32(0x0000ffff)
#define CSR12_CFP_MAX_DURATION		FIELD32(0xffff0000)

/*
 * CSR13: Synchronization configuration register 1.
 * All units in 1/16 TU.
 * ATIMW_DURATION: Atim window duration.
 * CFP_PERIOD: Cfp period, default is 0 TU.
 */
#define CSR13				0x0034
#define CSR13_ATIMW_DURATION		FIELD32(0x0000ffff)
#define CSR13_CFP_PERIOD		FIELD32(0x00ff0000)

/*
 * CSR14: Synchronization control register.
 * TSF_COUNT: Enable tsf auto counting.
 * TSF_SYNC: Tsf sync, 0: disable, 1: infra, 2: ad-hoc/master mode.
 * TBCN: Enable tbcn with reload value.
 * TCFP: Enable tcfp & cfp / cp switching.
 * TATIMW: Enable tatimw & atim window switching.
 * BEACON_GEN: Enable beacon generator.
 * CFP_COUNT_PRELOAD: Cfp count preload value.
 * TBCM_PRELOAD: Tbcn preload value in units of 64us.
 */
#define CSR14				0x0038
#define CSR14_TSF_COUNT			FIELD32(0x00000001)
#define CSR14_TSF_SYNC			FIELD32(0x00000006)
#define CSR14_TBCN			FIELD32(0x00000008)
#define CSR14_TCFP			FIELD32(0x00000010)
#define CSR14_TATIMW			FIELD32(0x00000020)
#define CSR14_BEACON_GEN		FIELD32(0x00000040)
#define CSR14_CFP_COUNT_PRELOAD		FIELD32(0x0000ff00)
#define CSR14_TBCM_PRELOAD		FIELD32(0xffff0000)

/*
 * CSR15: Synchronization status register.
 * CFP: ASIC is in contention-free period.
 * ATIMW: ASIC is in ATIM window.
 * BEACON_SENT: Beacon is send.
 */
#define CSR15				0x003c
#define CSR15_CFP			FIELD32(0x00000001)
#define CSR15_ATIMW			FIELD32(0x00000002)
#define CSR15_BEACON_SENT		FIELD32(0x00000004)

/*
 * CSR16: TSF timer register 0.
 */
#define CSR16				0x0040
#define CSR16_LOW_TSFTIMER		FIELD32(0xffffffff)

/*
 * CSR17: TSF timer register 1.
 */
#define CSR17				0x0044
#define CSR17_HIGH_TSFTIMER		FIELD32(0xffffffff)

/*
 * CSR18: IFS timer register 0.
 * SIFS: Sifs, default is 10 us.
 * PIFS: Pifs, default is 30 us.
 */
#define CSR18				0x0048
#define CSR18_SIFS			FIELD32(0x000001ff)
#define CSR18_PIFS			FIELD32(0x001f0000)

/*
 * CSR19: IFS timer register 1.
 * DIFS: Difs, default is 50 us.
 * EIFS: Eifs, default is 364 us.
 */
#define CSR19				0x004c
#define CSR19_DIFS			FIELD32(0x0000ffff)
#define CSR19_EIFS			FIELD32(0xffff0000)

/*
 * CSR20: Wakeup timer register.
 * DELAY_AFTER_TBCN: Delay after tbcn expired in units of 1/16 TU.
 * TBCN_BEFORE_WAKEUP: Number of beacon before wakeup.
 * AUTOWAKE: Enable auto wakeup / sleep mechanism.
 */
#define CSR20				0x0050
#define CSR20_DELAY_AFTER_TBCN		FIELD32(0x0000ffff)
#define CSR20_TBCN_BEFORE_WAKEUP	FIELD32(0x00ff0000)
#define CSR20_AUTOWAKE			FIELD32(0x01000000)

/*
 * CSR21: EEPROM control register.
 * RELOAD: Write 1 to reload eeprom content.
 * TYPE_93C46: 1: 93c46, 0:93c66.
 */
#define CSR21				0x0054
#define CSR21_RELOAD			FIELD32(0x00000001)
#define CSR21_EEPROM_DATA_CLOCK		FIELD32(0x00000002)
#define CSR21_EEPROM_CHIP_SELECT	FIELD32(0x00000004)
#define CSR21_EEPROM_DATA_IN		FIELD32(0x00000008)
#define CSR21_EEPROM_DATA_OUT		FIELD32(0x00000010)
#define CSR21_TYPE_93C46		FIELD32(0x00000020)

/*
 * CSR22: CFP control register.
 * CFP_DURATION_REMAIN: Cfp duration remain, in units of TU.
 * RELOAD_CFP_DURATION: Write 1 to reload cfp duration remain.
 */
#define CSR22				0x0058
#define CSR22_CFP_DURATION_REMAIN	FIELD32(0x0000ffff)
#define CSR22_RELOAD_CFP_DURATION	FIELD32(0x00010000)

/*
 * Transmit related CSRs.
 * Some values are set in TU, whereas 1 TU == 1024 us.
 */

/*
 * TXCSR0: TX Control Register.
 * KICK_TX: Kick tx ring.
 * KICK_ATIM: Kick atim ring.
 * KICK_PRIO: Kick priority ring.
 * ABORT: Abort all transmit related ring operation.
 */
#define TXCSR0				0x0060
#define TXCSR0_KICK_TX			FIELD32(0x00000001)
#define TXCSR0_KICK_ATIM		FIELD32(0x00000002)
#define TXCSR0_KICK_PRIO		FIELD32(0x00000004)
#define TXCSR0_ABORT			FIELD32(0x00000008)

/*
 * TXCSR1: TX Configuration Register.
 * ACK_TIMEOUT: Ack timeout, default = sifs + 2*slottime + acktime @ 1mbps.
 * ACK_CONSUME_TIME: Ack consume time, default = sifs + acktime @ 1mbps.
 * TSF_OFFSET: Insert tsf offset.
 * AUTORESPONDER: Enable auto responder which include ack & cts.
 */
#define TXCSR1				0x0064
#define TXCSR1_ACK_TIMEOUT		FIELD32(0x000001ff)
#define TXCSR1_ACK_CONSUME_TIME		FIELD32(0x0003fe00)
#define TXCSR1_TSF_OFFSET		FIELD32(0x00fc0000)
#define TXCSR1_AUTORESPONDER		FIELD32(0x01000000)

/*
 * TXCSR2: Tx descriptor configuration register.
 * TXD_SIZE: Tx descriptor size, default is 48.
 * NUM_TXD: Number of tx entries in ring.
 * NUM_ATIM: Number of atim entries in ring.
 * NUM_PRIO: Number of priority entries in ring.
 */
#define TXCSR2				0x0068
#define TXCSR2_TXD_SIZE			FIELD32(0x000000ff)
#define TXCSR2_NUM_TXD			FIELD32(0x0000ff00)
#define TXCSR2_NUM_ATIM			FIELD32(0x00ff0000)
#define TXCSR2_NUM_PRIO			FIELD32(0xff000000)

/*
 * TXCSR3: TX Ring Base address register.
 */
#define TXCSR3				0x006c
#define TXCSR3_TX_RING_REGISTER		FIELD32(0xffffffff)

/*
 * TXCSR4: TX Atim Ring Base address register.
 */
#define TXCSR4				0x0070
#define TXCSR4_ATIM_RING_REGISTER	FIELD32(0xffffffff)

/*
 * TXCSR5: TX Prio Ring Base address register.
 */
#define TXCSR5				0x0074
#define TXCSR5_PRIO_RING_REGISTER	FIELD32(0xffffffff)

/*
 * TXCSR6: Beacon Base address register.
 */
#define TXCSR6				0x0078
#define TXCSR6_BEACON_RING_REGISTER	FIELD32(0xffffffff)

/*
 * TXCSR7: Auto responder control register.
 * AR_POWERMANAGEMENT: Auto responder power management bit.
 */
#define TXCSR7				0x007c
#define TXCSR7_AR_POWERMANAGEMENT	FIELD32(0x00000001)

/*
 * TXCSR8: CCK Tx BBP register.
 */
#define TXCSR8				0x0098
#define TXCSR8_BBP_ID0			FIELD32(0x0000007f)
#define TXCSR8_BBP_ID0_VALID		FIELD32(0x00000080)
#define TXCSR8_BBP_ID1			FIELD32(0x00007f00)
#define TXCSR8_BBP_ID1_VALID		FIELD32(0x00008000)
#define TXCSR8_BBP_ID2			FIELD32(0x007f0000)
#define TXCSR8_BBP_ID2_VALID		FIELD32(0x00800000)
#define TXCSR8_BBP_ID3			FIELD32(0x7f000000)
#define TXCSR8_BBP_ID3_VALID		FIELD32(0x80000000)

/*
 * TXCSR9: OFDM TX BBP registers
 * OFDM_SIGNAL: BBP rate field address for OFDM.
 * OFDM_SERVICE: BBP service field address for OFDM.
 * OFDM_LENGTH_LOW: BBP length low byte address for OFDM.
 * OFDM_LENGTH_HIGH: BBP length high byte address for OFDM.
 */
#define TXCSR9				0x0094
#define TXCSR9_OFDM_RATE		FIELD32(0x000000ff)
#define TXCSR9_OFDM_SERVICE		FIELD32(0x0000ff00)
#define TXCSR9_OFDM_LENGTH_LOW		FIELD32(0x00ff0000)
#define TXCSR9_OFDM_LENGTH_HIGH		FIELD32(0xff000000)

/*
 * Receive related CSRs.
 * Some values are set in TU, whereas 1 TU == 1024 us.
 */

/*
 * RXCSR0: RX Control Register.
 * DISABLE_RX: Disable rx engine.
 * DROP_CRC: Drop crc error.
 * DROP_PHYSICAL: Drop physical error.
 * DROP_CONTROL: Drop control frame.
 * DROP_NOT_TO_ME: Drop not to me unicast frame.
 * DROP_TODS: Drop frame tods bit is true.
 * DROP_VERSION_ERROR: Drop version error frame.
 * PASS_CRC: Pass all packets with crc attached.
 * PASS_CRC: Pass all packets with crc attached.
 * PASS_PLCP: Pass all packets with 4 bytes PLCP attached.
 * DROP_MCAST: Drop multicast frames.
 * DROP_BCAST: Drop broadcast frames.
 * ENABLE_QOS: Accept QOS data frame and parse QOS field.
 */
#define RXCSR0				0x0080
#define RXCSR0_DISABLE_RX		FIELD32(0x00000001)
#define RXCSR0_DROP_CRC			FIELD32(0x00000002)
#define RXCSR0_DROP_PHYSICAL		FIELD32(0x00000004)
#define RXCSR0_DROP_CONTROL		FIELD32(0x00000008)
#define RXCSR0_DROP_NOT_TO_ME		FIELD32(0x00000010)
#define RXCSR0_DROP_TODS		FIELD32(0x00000020)
#define RXCSR0_DROP_VERSION_ERROR	FIELD32(0x00000040)
#define RXCSR0_PASS_CRC			FIELD32(0x00000080)
#define RXCSR0_PASS_PLCP		FIELD32(0x00000100)
#define RXCSR0_DROP_MCAST		FIELD32(0x00000200)
#define RXCSR0_DROP_BCAST		FIELD32(0x00000400)
#define RXCSR0_ENABLE_QOS		FIELD32(0x00000800)

/*
 * RXCSR1: RX descriptor configuration register.
 * RXD_SIZE: Rx descriptor size, default is 32b.
 * NUM_RXD: Number of rx entries in ring.
 */
#define RXCSR1				0x0084
#define RXCSR1_RXD_SIZE			FIELD32(0x000000ff)
#define RXCSR1_NUM_RXD			FIELD32(0x0000ff00)

/*
 * RXCSR2: RX Ring base address register.
 */
#define RXCSR2				0x0088
#define RXCSR2_RX_RING_REGISTER		FIELD32(0xffffffff)

/*
 * RXCSR3: BBP ID register for Rx operation.
 * BBP_ID#: BBP register # id.
 * BBP_ID#_VALID: BBP register # id is valid or not.
 */
#define RXCSR3				0x0090
#define RXCSR3_BBP_ID0			FIELD32(0x0000007f)
#define RXCSR3_BBP_ID0_VALID		FIELD32(0x00000080)
#define RXCSR3_BBP_ID1			FIELD32(0x00007f00)
#define RXCSR3_BBP_ID1_VALID		FIELD32(0x00008000)
#define RXCSR3_BBP_ID2			FIELD32(0x007f0000)
#define RXCSR3_BBP_ID2_VALID		FIELD32(0x00800000)
#define RXCSR3_BBP_ID3			FIELD32(0x7f000000)
#define RXCSR3_BBP_ID3_VALID		FIELD32(0x80000000)

/*
 * ARCSR1: Auto Responder PLCP config register 1.
 * AR_BBP_DATA#: Auto responder BBP register # data.
 * AR_BBP_ID#: Auto responder BBP register # Id.
 */
#define ARCSR1				0x009c
#define ARCSR1_AR_BBP_DATA2		FIELD32(0x000000ff)
#define ARCSR1_AR_BBP_ID2		FIELD32(0x0000ff00)
#define ARCSR1_AR_BBP_DATA3		FIELD32(0x00ff0000)
#define ARCSR1_AR_BBP_ID3		FIELD32(0xff000000)

/*
 * Miscellaneous Registers.
 * Some values are set in TU, whereas 1 TU == 1024 us.

 */

/*
 * PCICSR: PCI control register.
 * BIG_ENDIAN: 1: big endian, 0: little endian.
 * RX_TRESHOLD: Rx threshold in dw to start pci access
 * 0: 16dw (default), 1: 8dw, 2: 4dw, 3: 32dw.
 * TX_TRESHOLD: Tx threshold in dw to start pci access
 * 0: 0dw (default), 1: 1dw, 2: 4dw, 3: forward.
 * BURST_LENTH: Pci burst length 0: 4dw (default, 1: 8dw, 2: 16dw, 3:32dw.
 * ENABLE_CLK: Enable clk_run, pci clock can't going down to non-operational.
 * READ_MULTIPLE: Enable memory read multiple.
 * WRITE_INVALID: Enable memory write & invalid.
 */
#define PCICSR				0x008c
#define PCICSR_BIG_ENDIAN		FIELD32(0x00000001)
#define PCICSR_RX_TRESHOLD		FIELD32(0x00000006)
#define PCICSR_TX_TRESHOLD		FIELD32(0x00000018)
#define PCICSR_BURST_LENTH		FIELD32(0x00000060)
#define PCICSR_ENABLE_CLK		FIELD32(0x00000080)
#define PCICSR_READ_MULTIPLE		FIELD32(0x00000100)
#define PCICSR_WRITE_INVALID		FIELD32(0x00000200)

/*
 * CNT0: FCS error count.
 * FCS_ERROR: FCS error count, cleared when read.
 */
#define CNT0				0x00a0
#define CNT0_FCS_ERROR			FIELD32(0x0000ffff)

/*
 * Statistic Register.
 * CNT1: PLCP error count.
 * CNT2: Long error count.
 */
#define TIMECSR2			0x00a8
#define CNT1				0x00ac
#define CNT2				0x00b0
#define TIMECSR3			0x00b4

/*
 * CNT3: CCA false alarm count.
 */
#define CNT3				0x00b8
#define CNT3_FALSE_CCA			FIELD32(0x0000ffff)

/*
 * Statistic Register.
 * CNT4: Rx FIFO overflow count.
 * CNT5: Tx FIFO underrun count.
 */
#define CNT4				0x00bc
#define CNT5				0x00c0

/*
 * Baseband Control Register.
 */

/*
 * PWRCSR0: Power mode configuration register.
 */
#define PWRCSR0				0x00c4

/*
 * Power state transition time registers.
 */
#define PSCSR0				0x00c8
#define PSCSR1				0x00cc
#define PSCSR2				0x00d0
#define PSCSR3				0x00d4

/*
 * PWRCSR1: Manual power control / status register.
 * Allowed state: 0 deep_sleep, 1: sleep, 2: standby, 3: awake.
 * SET_STATE: Set state. Write 1 to trigger, self cleared.
 * BBP_DESIRE_STATE: BBP desired state.
 * RF_DESIRE_STATE: RF desired state.
 * BBP_CURR_STATE: BBP current state.
 * RF_CURR_STATE: RF current state.
 * PUT_TO_SLEEP: Put to sleep. Write 1 to trigger, self cleared.
 */
#define PWRCSR1				0x00d8
#define PWRCSR1_SET_STATE		FIELD32(0x00000001)
#define PWRCSR1_BBP_DESIRE_STATE	FIELD32(0x00000006)
#define PWRCSR1_RF_DESIRE_STATE		FIELD32(0x00000018)
#define PWRCSR1_BBP_CURR_STATE		FIELD32(0x00000060)
#define PWRCSR1_RF_CURR_STATE		FIELD32(0x00000180)
#define PWRCSR1_PUT_TO_SLEEP		FIELD32(0x00000200)

/*
 * TIMECSR: Timer control register.
 * US_COUNT: 1 us timer count in units of clock cycles.
 * US_64_COUNT: 64 us timer count in units of 1 us timer.
 * BEACON_EXPECT: Beacon expect window.
 */
#define TIMECSR				0x00dc
#define TIMECSR_US_COUNT		FIELD32(0x000000ff)
#define TIMECSR_US_64_COUNT		FIELD32(0x0000ff00)
#define TIMECSR_BEACON_EXPECT		FIELD32(0x00070000)

/*
 * MACCSR0: MAC configuration register 0.
 */
#define MACCSR0				0x00e0

/*
 * MACCSR1: MAC configuration register 1.
 * KICK_RX: Kick one-shot rx in one-shot rx mode.
 * ONESHOT_RXMODE: Enable one-shot rx mode for debugging.
 * BBPRX_RESET_MODE: Ralink bbp rx reset mode.
 * AUTO_TXBBP: Auto tx logic access bbp control register.
 * AUTO_RXBBP: Auto rx logic access bbp control register.
 * LOOPBACK: Loopback mode. 0: normal, 1: internal, 2: external, 3:rsvd.
 * INTERSIL_IF: Intersil if calibration pin.
 */
#define MACCSR1				0x00e4
#define MACCSR1_KICK_RX			FIELD32(0x00000001)
#define MACCSR1_ONESHOT_RXMODE		FIELD32(0x00000002)
#define MACCSR1_BBPRX_RESET_MODE	FIELD32(0x00000004)
#define MACCSR1_AUTO_TXBBP		FIELD32(0x00000008)
#define MACCSR1_AUTO_RXBBP		FIELD32(0x00000010)
#define MACCSR1_LOOPBACK		FIELD32(0x00000060)
#define MACCSR1_INTERSIL_IF		FIELD32(0x00000080)

/*
 * RALINKCSR: Ralink Rx auto-reset BBCR.
 * AR_BBP_DATA#: Auto reset BBP register # data.
 * AR_BBP_ID#: Auto reset BBP register # id.
 */
#define RALINKCSR			0x00e8
#define RALINKCSR_AR_BBP_DATA0		FIELD32(0x000000ff)
#define RALINKCSR_AR_BBP_ID0		FIELD32(0x00007f00)
#define RALINKCSR_AR_BBP_VALID0		FIELD32(0x00008000)
#define RALINKCSR_AR_BBP_DATA1		FIELD32(0x00ff0000)
#define RALINKCSR_AR_BBP_ID1		FIELD32(0x7f000000)
#define RALINKCSR_AR_BBP_VALID1		FIELD32(0x80000000)

/*
 * BCNCSR: Beacon interval control register.
 * CHANGE: Write one to change beacon interval.
 * DELTATIME: The delta time value.
 * NUM_BEACON: Number of beacon according to mode.
 * MODE: Please refer to asic specs.
 * PLUS: Plus or minus delta time value.
 */
#define BCNCSR				0x00ec
#define BCNCSR_CHANGE			FIELD32(0x00000001)
#define BCNCSR_DELTATIME		FIELD32(0x0000001e)
#define BCNCSR_NUM_BEACON		FIELD32(0x00001fe0)
#define BCNCSR_MODE			FIELD32(0x00006000)
#define BCNCSR_PLUS			FIELD32(0x00008000)

/*
 * BBP / RF / IF Control Register.
 */

/*
 * BBPCSR: BBP serial control register.
 * VALUE: Register value to program into BBP.
 * REGNUM: Selected BBP register.
 * BUSY: 1: asic is busy execute BBP programming.
 * WRITE_CONTROL: 1: write BBP, 0: read BBP.
 */
#define BBPCSR				0x00f0
#define BBPCSR_VALUE			FIELD32(0x000000ff)
#define BBPCSR_REGNUM			FIELD32(0x00007f00)
#define BBPCSR_BUSY			FIELD32(0x00008000)
#define BBPCSR_WRITE_CONTROL		FIELD32(0x00010000)

/*
 * RFCSR: RF serial control register.
 * VALUE: Register value + id to program into rf/if.
 * NUMBER_OF_BITS: Number of bits used in value (i:20, rfmd:22).
 * IF_SELECT: Chip to program: 0: rf, 1: if.
 * PLL_LD: Rf pll_ld status.
 * BUSY: 1: asic is busy execute rf programming.
 */
#define RFCSR				0x00f4
#define RFCSR_VALUE			FIELD32(0x00ffffff)
#define RFCSR_NUMBER_OF_BITS		FIELD32(0x1f000000)
#define RFCSR_IF_SELECT			FIELD32(0x20000000)
#define RFCSR_PLL_LD			FIELD32(0x40000000)
#define RFCSR_BUSY			FIELD32(0x80000000)

/*
 * LEDCSR: LED control register.
 * ON_PERIOD: On period, default 70ms.
 * OFF_PERIOD: Off period, default 30ms.
 * LINK: 0: linkoff, 1: linkup.
 * ACTIVITY: 0: idle, 1: active.
 * LINK_POLARITY: 0: active low, 1: active high.
 * ACTIVITY_POLARITY: 0: active low, 1: active high.
 * LED_DEFAULT: LED state for "enable" 0: ON, 1: OFF.
 */
#define LEDCSR				0x00f8
#define LEDCSR_ON_PERIOD		FIELD32(0x000000ff)
#define LEDCSR_OFF_PERIOD		FIELD32(0x0000ff00)
#define LEDCSR_LINK			FIELD32(0x00010000)
#define LEDCSR_ACTIVITY			FIELD32(0x00020000)
#define LEDCSR_LINK_POLARITY		FIELD32(0x00040000)
#define LEDCSR_ACTIVITY_POLARITY	FIELD32(0x00080000)
#define LEDCSR_LED_DEFAULT		FIELD32(0x00100000)

/*
 * SECCSR3: AES control register.
 */
#define SECCSR3				0x00fc

/*
 * ASIC pointer information.
 * RXPTR: Current RX ring address.
 * TXPTR: Current Tx ring address.
 * PRIPTR: Current Priority ring address.
 * ATIMPTR: Current ATIM ring address.
 */
#define RXPTR				0x0100
#define TXPTR				0x0104
#define PRIPTR				0x0108
#define ATIMPTR				0x010c

/*
 * TXACKCSR0: TX ACK timeout.
 */
#define TXACKCSR0			0x0110

/*
 * ACK timeout count registers.
 * ACKCNT0: TX ACK timeout count.
 * ACKCNT1: RX ACK timeout count.
 */
#define ACKCNT0				0x0114
#define ACKCNT1				0x0118

/*
 * GPIO and others.
 */

/*
 * GPIOCSR: GPIO control register.
 */
#define GPIOCSR				0x0120
#define GPIOCSR_BIT0			FIELD32(0x00000001)
#define GPIOCSR_BIT1			FIELD32(0x00000002)
#define GPIOCSR_BIT2			FIELD32(0x00000004)
#define GPIOCSR_BIT3			FIELD32(0x00000008)
#define GPIOCSR_BIT4			FIELD32(0x00000010)
#define GPIOCSR_BIT5			FIELD32(0x00000020)
#define GPIOCSR_BIT6			FIELD32(0x00000040)
#define GPIOCSR_BIT7			FIELD32(0x00000080)
#define GPIOCSR_DIR0			FIELD32(0x00000100)
#define GPIOCSR_DIR1			FIELD32(0x00000200)
#define GPIOCSR_DIR2			FIELD32(0x00000400)
#define GPIOCSR_DIR3			FIELD32(0x00000800)
#define GPIOCSR_DIR4			FIELD32(0x00001000)
#define GPIOCSR_DIR5			FIELD32(0x00002000)
#define GPIOCSR_DIR6			FIELD32(0x00004000)
#define GPIOCSR_DIR7			FIELD32(0x00008000)

/*
 * FIFO pointer registers.
 * FIFOCSR0: TX FIFO pointer.
 * FIFOCSR1: RX FIFO pointer.
 */
#define FIFOCSR0			0x0128
#define FIFOCSR1			0x012c

/*
 * BCNCSR1: Tx BEACON offset time control register.
 * PRELOAD: Beacon timer offset in units of usec.
 * BEACON_CWMIN: 2^CwMin.
 */
#define BCNCSR1				0x0130
#define BCNCSR1_PRELOAD			FIELD32(0x0000ffff)
#define BCNCSR1_BEACON_CWMIN		FIELD32(0x000f0000)

/*
 * MACCSR2: TX_PE to RX_PE turn-around time control register
 * DELAY: RX_PE low width, in units of pci clock cycle.
 */
#define MACCSR2				0x0134
#define MACCSR2_DELAY			FIELD32(0x000000ff)

/*
 * TESTCSR: TEST mode selection register.
 */
#define TESTCSR				0x0138

/*
 * ARCSR2: 1 Mbps ACK/CTS PLCP.
 */
#define ARCSR2				0x013c
#define ARCSR2_SIGNAL			FIELD32(0x000000ff)
#define ARCSR2_SERVICE			FIELD32(0x0000ff00)
#define ARCSR2_LENGTH			FIELD32(0xffff0000)

/*
 * ARCSR3: 2 Mbps ACK/CTS PLCP.
 */
#define ARCSR3				0x0140
#define ARCSR3_SIGNAL			FIELD32(0x000000ff)
#define ARCSR3_SERVICE			FIELD32(0x0000ff00)
#define ARCSR3_LENGTH			FIELD32(0xffff0000)

/*
 * ARCSR4: 5.5 Mbps ACK/CTS PLCP.
 */
#define ARCSR4				0x0144
#define ARCSR4_SIGNAL			FIELD32(0x000000ff)
#define ARCSR4_SERVICE			FIELD32(0x0000ff00)
#define ARCSR4_LENGTH			FIELD32(0xffff0000)

/*
 * ARCSR5: 11 Mbps ACK/CTS PLCP.
 */
#define ARCSR5				0x0148
#define ARCSR5_SIGNAL			FIELD32(0x000000ff)
#define ARCSR5_SERVICE			FIELD32(0x0000ff00)
#define ARCSR5_LENGTH			FIELD32(0xffff0000)

/*
 * ARTCSR0: CCK ACK/CTS payload consumed time for 1/2/5.5/11 mbps.
 */
#define ARTCSR0				0x014c
#define ARTCSR0_ACK_CTS_11MBS		FIELD32(0x000000ff)
#define ARTCSR0_ACK_CTS_5_5MBS		FIELD32(0x0000ff00)
#define ARTCSR0_ACK_CTS_2MBS		FIELD32(0x00ff0000)
#define ARTCSR0_ACK_CTS_1MBS		FIELD32(0xff000000)


/*
 * ARTCSR1: OFDM ACK/CTS payload consumed time for 6/9/12/18 mbps.
 */
#define ARTCSR1				0x0150
#define ARTCSR1_ACK_CTS_6MBS		FIELD32(0x000000ff)
#define ARTCSR1_ACK_CTS_9MBS		FIELD32(0x0000ff00)
#define ARTCSR1_ACK_CTS_12MBS		FIELD32(0x00ff0000)
#define ARTCSR1_ACK_CTS_18MBS		FIELD32(0xff000000)

/*
 * ARTCSR2: OFDM ACK/CTS payload consumed time for 24/36/48/54 mbps.
 */
#define ARTCSR2				0x0154
#define ARTCSR2_ACK_CTS_24MBS		FIELD32(0x000000ff)
#define ARTCSR2_ACK_CTS_36MBS		FIELD32(0x0000ff00)
#define ARTCSR2_ACK_CTS_48MBS		FIELD32(0x00ff0000)
#define ARTCSR2_ACK_CTS_54MBS		FIELD32(0xff000000)

/*
 * SECCSR1: WEP control register.
 * KICK_ENCRYPT: Kick encryption engine, self-clear.
 * ONE_SHOT: 0: ring mode, 1: One shot only mode.
 * DESC_ADDRESS: Descriptor physical address of frame.
 */
#define SECCSR1				0x0158
#define SECCSR1_KICK_ENCRYPT		FIELD32(0x00000001)
#define SECCSR1_ONE_SHOT		FIELD32(0x00000002)
#define SECCSR1_DESC_ADDRESS		FIELD32(0xfffffffc)

/*
 * BBPCSR1: BBP TX configuration.
 */
#define BBPCSR1				0x015c
#define BBPCSR1_CCK			FIELD32(0x00000003)
#define BBPCSR1_CCK_FLIP		FIELD32(0x00000004)
#define BBPCSR1_OFDM			FIELD32(0x00030000)
#define BBPCSR1_OFDM_FLIP		FIELD32(0x00040000)

/*
 * Dual band configuration registers.
 * DBANDCSR0: Dual band configuration register 0.
 * DBANDCSR1: Dual band configuration register 1.
 */
#define DBANDCSR0			0x0160
#define DBANDCSR1			0x0164

/*
 * BBPPCSR: BBP Pin control register.
 */
#define BBPPCSR				0x0168

/*
 * MAC special debug mode selection registers.
 * DBGSEL0: MAC special debug mode selection register 0.
 * DBGSEL1: MAC special debug mode selection register 1.
 */
#define DBGSEL0				0x016c
#define DBGSEL1				0x0170

/*
 * BISTCSR: BBP BIST register.
 */
#define BISTCSR				0x0174

/*
 * Multicast filter registers.
 * MCAST0: Multicast filter register 0.
 * MCAST1: Multicast filter register 1.
 */
#define MCAST0				0x0178
#define MCAST1				0x017c

/*
 * UART registers.
 * UARTCSR0: UART1 TX register.
 * UARTCSR1: UART1 RX register.
 * UARTCSR3: UART1 frame control register.
 * UARTCSR4: UART1 buffer control register.
 * UART2CSR0: UART2 TX register.
 * UART2CSR1: UART2 RX register.
 * UART2CSR3: UART2 frame control register.
 * UART2CSR4: UART2 buffer control register.
 */
#define UARTCSR0			0x0180
#define UARTCSR1			0x0184
#define UARTCSR3			0x0188
#define UARTCSR4			0x018c
#define UART2CSR0			0x0190
#define UART2CSR1			0x0194
#define UART2CSR3			0x0198
#define UART2CSR4			0x019c

/*
 * BBP registers.
 * The wordsize of the BBP is 8 bits.
 */

/*
 * R2: TX antenna control
 */
#define BBP_R2_TX_ANTENNA		FIELD8(0x03)
#define BBP_R2_TX_IQ_FLIP		FIELD8(0x04)

/*
 * R14: RX antenna control
 */
#define BBP_R14_RX_ANTENNA		FIELD8(0x03)
#define BBP_R14_RX_IQ_FLIP		FIELD8(0x04)

/*
 * BBP_R70
 */
#define BBP_R70_JAPAN_FILTER		FIELD8(0x08)

/*
 * RF registers
 */

/*
 * RF 1
 */
#define RF1_TUNER			FIELD32(0x00020000)

/*
 * RF 3
 */
#define RF3_TUNER			FIELD32(0x00000100)
#define RF3_TXPOWER			FIELD32(0x00003e00)

/*
 * EEPROM content.
 * The wordsize of the EEPROM is 16 bits.
 */

/*
 * HW MAC address.
 */
#define EEPROM_MAC_ADDR_0		0x0002
#define EEPROM_MAC_ADDR_BYTE0		FIELD16(0x00ff)
#define EEPROM_MAC_ADDR_BYTE1		FIELD16(0xff00)
#define EEPROM_MAC_ADDR1		0x0003
#define EEPROM_MAC_ADDR_BYTE2		FIELD16(0x00ff)
#define EEPROM_MAC_ADDR_BYTE3		FIELD16(0xff00)
#define EEPROM_MAC_ADDR_2		0x0004
#define EEPROM_MAC_ADDR_BYTE4		FIELD16(0x00ff)
#define EEPROM_MAC_ADDR_BYTE5		FIELD16(0xff00)

/*
 * EEPROM antenna.
 * ANTENNA_NUM: Number of antenna's.
 * TX_DEFAULT: Default antenna 0: diversity, 1: A, 2: B.
 * RX_DEFAULT: Default antenna 0: diversity, 1: A, 2: B.
 * LED_MODE: 0: default, 1: TX/RX activity,2: Single (ignore link), 3: rsvd.
 * DYN_TXAGC: Dynamic TX AGC control.
 * HARDWARE_RADIO: 1: Hardware controlled radio. Read GPIO0.
 * RF_TYPE: Rf_type of this adapter.
 */
#define EEPROM_ANTENNA			0x10
#define EEPROM_ANTENNA_NUM		FIELD16(0x0003)
#define EEPROM_ANTENNA_TX_DEFAULT	FIELD16(0x000c)
#define EEPROM_ANTENNA_RX_DEFAULT	FIELD16(0x0030)
#define EEPROM_ANTENNA_LED_MODE		FIELD16(0x01c0)
#define EEPROM_ANTENNA_DYN_TXAGC	FIELD16(0x0200)
#define EEPROM_ANTENNA_HARDWARE_RADIO	FIELD16(0x0400)
#define EEPROM_ANTENNA_RF_TYPE		FIELD16(0xf800)

/*
 * EEPROM NIC config.
 * CARDBUS_ACCEL: 0: enable, 1: disable.
 * DYN_BBP_TUNE: 0: enable, 1: disable.
 * CCK_TX_POWER: CCK TX power compensation.
 */
#define EEPROM_NIC			0x11
#define EEPROM_NIC_CARDBUS_ACCEL	FIELD16(0x0001)
#define EEPROM_NIC_DYN_BBP_TUNE		FIELD16(0x0002)
#define EEPROM_NIC_CCK_TX_POWER		FIELD16(0x000c)

/*
 * EEPROM geography.
 * GEO: Default geography setting for device.
 */
#define EEPROM_GEOGRAPHY		0x12
#define EEPROM_GEOGRAPHY_GEO		FIELD16(0x0f00)

/*
 * EEPROM BBP.
 */
#define EEPROM_BBP_START		0x13
#define EEPROM_BBP_SIZE			16
#define EEPROM_BBP_VALUE		FIELD16(0x00ff)
#define EEPROM_BBP_REG_ID		FIELD16(0xff00)

/*
 * EEPROM TXPOWER
 */
#define EEPROM_TXPOWER_START		0x23
#define EEPROM_TXPOWER_SIZE		7
#define EEPROM_TXPOWER_1		FIELD16(0x00ff)
#define EEPROM_TXPOWER_2		FIELD16(0xff00)

/*
 * RSSI <-> dBm offset calibration
 */
#define EEPROM_CALIBRATE_OFFSET		0x3e
#define EEPROM_CALIBRATE_OFFSET_RSSI	FIELD16(0x00ff)

/*
 * DMA descriptor defines.
 */
#define TXD_DESC_SIZE			( 11 * sizeof(__le32) )
#define RXD_DESC_SIZE			( 11 * sizeof(__le32) )

/*
 * TX descriptor format for TX, PRIO, ATIM and Beacon Ring.
 */

/*
 * Word0
 */
#define TXD_W0_OWNER_NIC		FIELD32(0x00000001)
#define TXD_W0_VALID			FIELD32(0x00000002)
#define TXD_W0_RESULT			FIELD32(0x0000001c)
#define TXD_W0_RETRY_COUNT		FIELD32(0x000000e0)
#define TXD_W0_MORE_FRAG		FIELD32(0x00000100)
#define TXD_W0_ACK			FIELD32(0x00000200)
#define TXD_W0_TIMESTAMP		FIELD32(0x00000400)
#define TXD_W0_OFDM			FIELD32(0x00000800)
#define TXD_W0_CIPHER_OWNER		FIELD32(0x00001000)
#define TXD_W0_IFS			FIELD32(0x00006000)
#define TXD_W0_RETRY_MODE		FIELD32(0x00008000)
#define TXD_W0_DATABYTE_COUNT		FIELD32(0x0fff0000)
#define TXD_W0_CIPHER_ALG		FIELD32(0xe0000000)

/*
 * Word1
 */
#define TXD_W1_BUFFER_ADDRESS		FIELD32(0xffffffff)

/*
 * Word2
 */
#define TXD_W2_IV_OFFSET		FIELD32(0x0000003f)
#define TXD_W2_AIFS			FIELD32(0x000000c0)
#define TXD_W2_CWMIN			FIELD32(0x00000f00)
#define TXD_W2_CWMAX			FIELD32(0x0000f000)

/*
 * Word3: PLCP information
 */
#define TXD_W3_PLCP_SIGNAL		FIELD32(0x000000ff)
#define TXD_W3_PLCP_SERVICE		FIELD32(0x0000ff00)
#define TXD_W3_PLCP_LENGTH_LOW		FIELD32(0x00ff0000)
#define TXD_W3_PLCP_LENGTH_HIGH		FIELD32(0xff000000)

/*
 * Word4
 */
#define TXD_W4_IV			FIELD32(0xffffffff)

/*
 * Word5
 */
#define TXD_W5_EIV			FIELD32(0xffffffff)

/*
 * Word6-9: Key
 */
#define TXD_W6_KEY			FIELD32(0xffffffff)
#define TXD_W7_KEY			FIELD32(0xffffffff)
#define TXD_W8_KEY			FIELD32(0xffffffff)
#define TXD_W9_KEY			FIELD32(0xffffffff)

/*
 * Word10
 */
#define TXD_W10_RTS			FIELD32(0x00000001)
#define TXD_W10_TX_RATE			FIELD32(0x000000fe)

/*
 * RX descriptor format for RX Ring.
 */

/*
 * Word0
 */
#define RXD_W0_OWNER_NIC		FIELD32(0x00000001)
#define RXD_W0_UNICAST_TO_ME		FIELD32(0x00000002)
#define RXD_W0_MULTICAST		FIELD32(0x00000004)
#define RXD_W0_BROADCAST		FIELD32(0x00000008)
#define RXD_W0_MY_BSS			FIELD32(0x00000010)
#define RXD_W0_CRC_ERROR		FIELD32(0x00000020)
#define RXD_W0_OFDM			FIELD32(0x00000040)
#define RXD_W0_PHYSICAL_ERROR		FIELD32(0x00000080)
#define RXD_W0_CIPHER_OWNER		FIELD32(0x00000100)
#define RXD_W0_ICV_ERROR		FIELD32(0x00000200)
#define RXD_W0_IV_OFFSET		FIELD32(0x0000fc00)
#define RXD_W0_DATABYTE_COUNT		FIELD32(0x0fff0000)
#define RXD_W0_CIPHER_ALG		FIELD32(0xe0000000)

/*
 * Word1
 */
#define RXD_W1_BUFFER_ADDRESS		FIELD32(0xffffffff)

/*
 * Word2
 */
#define RXD_W2_SIGNAL			FIELD32(0x000000ff)
#define RXD_W2_RSSI			FIELD32(0x0000ff00)
#define RXD_W2_TA			FIELD32(0xffff0000)

/*
 * Word3
 */
#define RXD_W3_TA			FIELD32(0xffffffff)

/*
 * Word4
 */
#define RXD_W4_IV			FIELD32(0xffffffff)

/*
 * Word5
 */
#define RXD_W5_EIV			FIELD32(0xffffffff)

/*
 * Word6-9: Key
 */
#define RXD_W6_KEY			FIELD32(0xffffffff)
#define RXD_W7_KEY			FIELD32(0xffffffff)
#define RXD_W8_KEY			FIELD32(0xffffffff)
#define RXD_W9_KEY			FIELD32(0xffffffff)

/*
 * Word10
 */
#define RXD_W10_DROP			FIELD32(0x00000001)

/*
 * Macro's for converting txpower from EEPROM to mac80211 value
 * and from mac80211 value to register value.
 */
#define MIN_TXPOWER	0
#define MAX_TXPOWER	31
#define DEFAULT_TXPOWER	24

#define TXPOWER_FROM_DEV(__txpower) \
	(((u8)(__txpower)) > MAX_TXPOWER) ? DEFAULT_TXPOWER : (__txpower)

#define TXPOWER_TO_DEV(__txpower) \
	clamp_t(char, __txpower, MIN_TXPOWER, MAX_TXPOWER)

#endif /* RT2500PCI_H */
