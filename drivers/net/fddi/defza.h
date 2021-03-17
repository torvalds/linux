/* SPDX-License-Identifier: GPL-2.0+ */
/*	FDDI network adapter driver for DEC FDDIcontroller 700/700-C devices.
 *
 *	Copyright (c) 2018  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	References:
 *
 *	Dave Sawyer & Phil Weeks & Frank Itkowsky,
 *	"DEC FDDIcontroller 700 Port Specification",
 *	Revision 1.1, Digital Equipment Corporation
 */

#include <linux/compiler.h>
#include <linux/if_fddi.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/types.h>

/* IOmem register offsets. */
#define FZA_REG_BASE		0x100000	/* register base address */
#define FZA_REG_RESET		0x100200	/* reset, r/w */
#define FZA_REG_INT_EVENT	0x100400	/* interrupt event, r/w1c */
#define FZA_REG_STATUS		0x100402	/* status, r/o */
#define FZA_REG_INT_MASK	0x100404	/* interrupt mask, r/w */
#define FZA_REG_CONTROL_A	0x100500	/* control A, r/w1s */
#define FZA_REG_CONTROL_B	0x100502	/* control B, r/w */

/* Reset register constants.  Bits 1:0 are r/w, others are fixed at 0. */
#define FZA_RESET_DLU	0x0002	/* OR with INIT to blast flash memory */
#define FZA_RESET_INIT	0x0001	/* switch into the reset state */
#define FZA_RESET_CLR	0x0000	/* run self-test and return to work */

/* Interrupt event register constants.  All bits are r/w1c. */
#define FZA_EVENT_DLU_DONE	0x0800	/* flash memory write complete */
#define FZA_EVENT_FLUSH_TX	0x0400	/* transmit ring flush request */
#define FZA_EVENT_PM_PARITY_ERR	0x0200	/* onboard packet memory parity err */
#define FZA_EVENT_HB_PARITY_ERR	0x0100	/* host bus parity error */
#define FZA_EVENT_NXM_ERR	0x0080	/* non-existent memory access error;
					 * also raised for unaligned and
					 * unsupported partial-word accesses
					 */
#define FZA_EVENT_LINK_ST_CHG	0x0040	/* link status change */
#define FZA_EVENT_STATE_CHG	0x0020	/* adapter state change */
#define FZA_EVENT_UNS_POLL	0x0010	/* unsolicited event service request */
#define FZA_EVENT_CMD_DONE	0x0008	/* command done ack */
#define FZA_EVENT_SMT_TX_POLL	0x0004	/* SMT frame transmit request */
#define FZA_EVENT_RX_POLL	0x0002	/* receive request (packet avail.) */
#define FZA_EVENT_TX_DONE	0x0001	/* RMC transmit done ack */

/* Status register constants.  All bits are r/o. */
#define FZA_STATUS_DLU_SHIFT	0xc	/* down line upgrade status bits */
#define FZA_STATUS_DLU_MASK	0x03
#define FZA_STATUS_LINK_SHIFT	0xb	/* link status bits */
#define FZA_STATUS_LINK_MASK	0x01
#define FZA_STATUS_STATE_SHIFT	0x8	/* adapter state bits */
#define FZA_STATUS_STATE_MASK	0x07
#define FZA_STATUS_HALT_SHIFT	0x0	/* halt reason bits */
#define FZA_STATUS_HALT_MASK	0xff
#define FZA_STATUS_TEST_SHIFT	0x0	/* test failure bits */
#define FZA_STATUS_TEST_MASK	0xff

#define FZA_STATUS_GET_DLU(x)	(((x) >> FZA_STATUS_DLU_SHIFT) &	\
				 FZA_STATUS_DLU_MASK)
#define FZA_STATUS_GET_LINK(x)	(((x) >> FZA_STATUS_LINK_SHIFT) &	\
				 FZA_STATUS_LINK_MASK)
#define FZA_STATUS_GET_STATE(x)	(((x) >> FZA_STATUS_STATE_SHIFT) &	\
				 FZA_STATUS_STATE_MASK)
#define FZA_STATUS_GET_HALT(x)	(((x) >> FZA_STATUS_HALT_SHIFT) &	\
				 FZA_STATUS_HALT_MASK)
#define FZA_STATUS_GET_TEST(x)	(((x) >> FZA_STATUS_TEST_SHIFT) &	\
				 FZA_STATUS_TEST_MASK)

#define FZA_DLU_FAILURE		0x0	/* DLU catastrophic error; brain dead */
#define FZA_DLU_ERROR		0x1	/* DLU error; old firmware intact */
#define FZA_DLU_SUCCESS		0x2	/* DLU OK; new firmware loaded */

#define FZA_LINK_OFF		0x0	/* link unavailable */
#define FZA_LINK_ON		0x1	/* link available */

#define FZA_STATE_RESET		0x0	/* resetting */
#define FZA_STATE_UNINITIALIZED	0x1	/* after a reset */
#define FZA_STATE_INITIALIZED	0x2	/* initialized */
#define FZA_STATE_RUNNING	0x3	/* running (link active) */
#define FZA_STATE_MAINTENANCE	0x4	/* running (link looped back) */
#define FZA_STATE_HALTED	0x5	/* halted (error condition) */

#define FZA_HALT_UNKNOWN	0x00	/* unknown reason */
#define FZA_HALT_HOST		0x01	/* host-directed HALT */
#define FZA_HALT_HB_PARITY	0x02	/* host bus parity error */
#define FZA_HALT_NXM		0x03	/* adapter non-existent memory ref. */
#define FZA_HALT_SW		0x04	/* adapter software fault */
#define FZA_HALT_HW		0x05	/* adapter hardware fault */
#define FZA_HALT_PC_TRACE	0x06	/* PC Trace path test */
#define FZA_HALT_DLSW		0x07	/* data link software fault */
#define FZA_HALT_DLHW		0x08	/* data link hardware fault */

#define FZA_TEST_FATAL		0x00	/* self-test catastrophic failure */
#define FZA_TEST_68K		0x01	/* 68000 CPU */
#define FZA_TEST_SRAM_BWADDR	0x02	/* SRAM byte/word address */
#define FZA_TEST_SRAM_DBUS	0x03	/* SRAM data bus */
#define FZA_TEST_SRAM_STUCK1	0x04	/* SRAM stuck-at range 1 */
#define FZA_TEST_SRAM_STUCK2	0x05	/* SRAM stuck-at range 2 */
#define FZA_TEST_SRAM_COUPL1	0x06	/* SRAM coupling range 1 */
#define FZA_TEST_SRAM_COUPL2	0x07	/* SRAM coupling */
#define FZA_TEST_FLASH_CRC	0x08	/* Flash CRC */
#define FZA_TEST_ROM		0x09	/* option ROM */
#define FZA_TEST_PHY_CSR	0x0a	/* PHY CSR */
#define FZA_TEST_MAC_BIST	0x0b	/* MAC BiST */
#define FZA_TEST_MAC_CSR	0x0c	/* MAC CSR */
#define FZA_TEST_MAC_ADDR_UNIQ	0x0d	/* MAC unique address */
#define FZA_TEST_ELM_BIST	0x0e	/* ELM BiST */
#define FZA_TEST_ELM_CSR	0x0f	/* ELM CSR */
#define FZA_TEST_ELM_ADDR_UNIQ	0x10	/* ELM unique address */
#define FZA_TEST_CAM		0x11	/* CAM */
#define FZA_TEST_NIROM		0x12	/* NI ROM checksum */
#define FZA_TEST_SC_LOOP	0x13	/* SC loopback packet */
#define FZA_TEST_LM_LOOP	0x14	/* LM loopback packet */
#define FZA_TEST_EB_LOOP	0x15	/* EB loopback packet */
#define FZA_TEST_SC_LOOP_BYPS	0x16	/* SC bypass loopback packet */
#define FZA_TEST_LM_LOOP_LOCAL	0x17	/* LM local loopback packet */
#define FZA_TEST_EB_LOOP_LOCAL	0x18	/* EB local loopback packet */
#define FZA_TEST_CDC_LOOP	0x19	/* CDC loopback packet */
#define FZA_TEST_FIBER_LOOP	0x1A	/* FIBER loopback packet */
#define FZA_TEST_CAM_MATCH_LOOP	0x1B	/* CAM match packet loopback */
#define FZA_TEST_68K_IRQ_STUCK	0x1C	/* 68000 interrupt line stuck-at */
#define FZA_TEST_IRQ_PRESENT	0x1D	/* interrupt present register */
#define FZA_TEST_RMC_BIST	0x1E	/* RMC BiST */
#define FZA_TEST_RMC_CSR	0x1F	/* RMC CSR */
#define FZA_TEST_RMC_ADDR_UNIQ	0x20	/* RMC unique address */
#define FZA_TEST_PM_DPATH	0x21	/* packet memory data path */
#define FZA_TEST_PM_ADDR	0x22	/* packet memory address */
#define FZA_TEST_RES_23		0x23	/* reserved */
#define FZA_TEST_PM_DESC	0x24	/* packet memory descriptor */
#define FZA_TEST_PM_OWN		0x25	/* packet memory own bit */
#define FZA_TEST_PM_PARITY	0x26	/* packet memory parity */
#define FZA_TEST_PM_BSWAP	0x27	/* packet memory byte swap */
#define FZA_TEST_PM_WSWAP	0x28	/* packet memory word swap */
#define FZA_TEST_PM_REF		0x29	/* packet memory refresh */
#define FZA_TEST_PM_CSR		0x2A	/* PM CSR */
#define FZA_TEST_PORT_STATUS	0x2B	/* port status register */
#define FZA_TEST_HOST_IRQMASK	0x2C	/* host interrupt mask */
#define FZA_TEST_TIMER_IRQ1	0x2D	/* RTOS timer */
#define FZA_TEST_FORCE_IRQ1	0x2E	/* force RTOS IRQ1 */
#define FZA_TEST_TIMER_IRQ5	0x2F	/* IRQ5 backoff timer */
#define FZA_TEST_FORCE_IRQ5	0x30	/* force IRQ5 */
#define FZA_TEST_RES_31		0x31	/* reserved */
#define FZA_TEST_IC_PRIO	0x32	/* interrupt controller priority */
#define FZA_TEST_PM_FULL	0x33	/* full packet memory */
#define FZA_TEST_PMI_DMA	0x34	/* PMI DMA */

/* Interrupt mask register constants.  All bits are r/w. */
#define FZA_MASK_RESERVED	0xf000	/* unused */
#define FZA_MASK_DLU_DONE	0x0800	/* flash memory write complete */
#define FZA_MASK_FLUSH_TX	0x0400	/* transmit ring flush request */
#define FZA_MASK_PM_PARITY_ERR	0x0200	/* onboard packet memory parity error
					 */
#define FZA_MASK_HB_PARITY_ERR	0x0100	/* host bus parity error */
#define FZA_MASK_NXM_ERR	0x0080	/* adapter non-existent memory
					 * reference
					 */
#define FZA_MASK_LINK_ST_CHG	0x0040	/* link status change */
#define FZA_MASK_STATE_CHG	0x0020	/* adapter state change */
#define FZA_MASK_UNS_POLL	0x0010	/* unsolicited event service request */
#define FZA_MASK_CMD_DONE	0x0008	/* command ring entry processed */
#define FZA_MASK_SMT_TX_POLL	0x0004	/* SMT frame transmit request */
#define FZA_MASK_RCV_POLL	0x0002	/* receive request (packet available)
					 */
#define FZA_MASK_TX_DONE	0x0001	/* RMC transmit done acknowledge */

/* Which interrupts to receive: 0/1 is mask/unmask. */
#define FZA_MASK_NONE		0x0000
#define FZA_MASK_NORMAL							\
		((~(FZA_MASK_RESERVED | FZA_MASK_DLU_DONE |		\
		    FZA_MASK_PM_PARITY_ERR | FZA_MASK_HB_PARITY_ERR |	\
		    FZA_MASK_NXM_ERR)) & 0xffff)

/* Control A register constants. */
#define FZA_CONTROL_A_HB_PARITY_ERR	0x8000	/* host bus parity error */
#define FZA_CONTROL_A_NXM_ERR		0x4000	/* adapter non-existent memory
						 * reference
						 */
#define FZA_CONTROL_A_SMT_RX_OVFL	0x0040	/* SMT receive overflow */
#define FZA_CONTROL_A_FLUSH_DONE	0x0020	/* flush tx request complete */
#define FZA_CONTROL_A_SHUT		0x0010	/* turn the interface off */
#define FZA_CONTROL_A_HALT		0x0008	/* halt the controller */
#define FZA_CONTROL_A_CMD_POLL		0x0004	/* command ring poll */
#define FZA_CONTROL_A_SMT_RX_POLL	0x0002	/* SMT receive ring poll */
#define FZA_CONTROL_A_TX_POLL		0x0001	/* transmit poll */

/* Control B register constants.  All bits are r/w.
 *
 * Possible values:
 *	0x0000 after booting into REX,
 *	0x0003 after issuing `boot #/mop'.
 */
#define FZA_CONTROL_B_CONSOLE	0x0002	/* OR with DRIVER for console
					 * (TC firmware) mode
					 */
#define FZA_CONTROL_B_DRIVER	0x0001	/* driver mode */
#define FZA_CONTROL_B_IDLE	0x0000	/* no driver installed */

#define FZA_RESET_PAD							\
		(FZA_REG_RESET - FZA_REG_BASE)
#define FZA_INT_EVENT_PAD						\
		(FZA_REG_INT_EVENT - FZA_REG_RESET - sizeof(u16))
#define FZA_CONTROL_A_PAD						\
		(FZA_REG_CONTROL_A - FZA_REG_INT_MASK - sizeof(u16))

/* Layout of registers. */
struct fza_regs {
	u8  pad0[FZA_RESET_PAD];
	u16 reset;				/* reset register */
	u8  pad1[FZA_INT_EVENT_PAD];
	u16 int_event;				/* interrupt event register */
	u16 status;				/* status register */
	u16 int_mask;				/* interrupt mask register */
	u8  pad2[FZA_CONTROL_A_PAD];
	u16 control_a;				/* control A register */
	u16 control_b;				/* control B register */
};

/* Command descriptor ring entry. */
struct fza_ring_cmd {
	u32 cmd_own;		/* bit 31: ownership, bits [30:0]: command */
	u32 stat;		/* command status */
	u32 buffer;		/* address of the buffer in the FZA space */
	u32 pad0;
};

#define FZA_RING_CMD		0x200400	/* command ring address */
#define FZA_RING_CMD_SIZE	0x40		/* command descriptor ring
						 * size
						 */
/* Command constants. */
#define FZA_RING_CMD_MASK	0x7fffffff
#define FZA_RING_CMD_NOP	0x00000000	/* nop */
#define FZA_RING_CMD_INIT	0x00000001	/* initialize */
#define FZA_RING_CMD_MODCAM	0x00000002	/* modify CAM */
#define FZA_RING_CMD_PARAM	0x00000003	/* set system parameters */
#define FZA_RING_CMD_MODPROM	0x00000004	/* modify promiscuous mode */
#define FZA_RING_CMD_SETCHAR	0x00000005	/* set link characteristics */
#define FZA_RING_CMD_RDCNTR	0x00000006	/* read counters */
#define FZA_RING_CMD_STATUS	0x00000007	/* get link status */
#define FZA_RING_CMD_RDCAM	0x00000008	/* read CAM */

/* Command status constants. */
#define FZA_RING_STAT_SUCCESS	0x00000000

/* Unsolicited event descriptor ring entry. */
struct fza_ring_uns {
	u32 own;		/* bit 31: ownership, bits [30:0]: reserved */
	u32 id;			/* event ID */
	u32 buffer;		/* address of the buffer in the FZA space */
	u32 pad0;		/* reserved */
};

#define FZA_RING_UNS		0x200800	/* unsolicited ring address */
#define FZA_RING_UNS_SIZE	0x40		/* unsolicited descriptor ring
						 * size
						 */
/* Unsolicited event constants. */
#define FZA_RING_UNS_UND	0x00000000	/* undefined event ID */
#define FZA_RING_UNS_INIT_IN	0x00000001	/* ring init initiated */
#define FZA_RING_UNS_INIT_RX	0x00000002	/* ring init received */
#define FZA_RING_UNS_BEAC_IN	0x00000003	/* ring beaconing initiated */
#define FZA_RING_UNS_DUP_ADDR	0x00000004	/* duplicate address detected */
#define FZA_RING_UNS_DUP_TOK	0x00000005	/* duplicate token detected */
#define FZA_RING_UNS_PURG_ERR	0x00000006	/* ring purger error */
#define FZA_RING_UNS_STRIP_ERR	0x00000007	/* bridge strip error */
#define FZA_RING_UNS_OP_OSC	0x00000008	/* ring op oscillation */
#define FZA_RING_UNS_BEAC_RX	0x00000009	/* directed beacon received */
#define FZA_RING_UNS_PCT_IN	0x0000000a	/* PC trace initiated */
#define FZA_RING_UNS_PCT_RX	0x0000000b	/* PC trace received */
#define FZA_RING_UNS_TX_UNDER	0x0000000c	/* transmit underrun */
#define FZA_RING_UNS_TX_FAIL	0x0000000d	/* transmit failure */
#define FZA_RING_UNS_RX_OVER	0x0000000e	/* receive overrun */

/* RMC (Ring Memory Control) transmit descriptor ring entry. */
struct fza_ring_rmc_tx {
	u32 rmc;		/* RMC information */
	u32 avl;		/* available for host (unused by RMC) */
	u32 own;		/* bit 31: ownership, bits [30:0]: reserved */
	u32 pad0;		/* reserved */
};

#define FZA_TX_BUFFER_ADDR(x)	(0x200000 | (((x) & 0xffff) << 5))
#define FZA_TX_BUFFER_SIZE	512
struct fza_buffer_tx {
	u32 data[FZA_TX_BUFFER_SIZE / sizeof(u32)];
};

/* Transmit ring RMC constants. */
#define FZA_RING_TX_SOP		0x80000000	/* start of packet */
#define FZA_RING_TX_EOP		0x40000000	/* end of packet */
#define FZA_RING_TX_DTP		0x20000000	/* discard this packet */
#define FZA_RING_TX_VBC		0x10000000	/* valid buffer byte count */
#define FZA_RING_TX_DCC_MASK	0x0f000000	/* DMA completion code */
#define FZA_RING_TX_DCC_SUCCESS	0x01000000	/* transmit succeeded */
#define FZA_RING_TX_DCC_DTP_SOP	0x02000000	/* DTP set at SOP */
#define FZA_RING_TX_DCC_DTP	0x04000000	/* DTP set within packet */
#define FZA_RING_TX_DCC_ABORT	0x05000000	/* MAC-requested abort */
#define FZA_RING_TX_DCC_PARITY	0x06000000	/* xmit data parity error */
#define FZA_RING_TX_DCC_UNDRRUN	0x07000000	/* transmit underrun */
#define FZA_RING_TX_XPO_MASK	0x003fe000	/* transmit packet offset */

/* Host receive descriptor ring entry. */
struct fza_ring_hst_rx {
	u32 buf0_own;		/* bit 31: ownership, bits [30:23]: unused,
				 * bits [22:0]: right-shifted address of the
				 * buffer in system memory (low buffer)
				 */
	u32 buffer1;		/* bits [31:23]: unused,
				 * bits [22:0]: right-shifted address of the
				 * buffer in system memory (high buffer)
				 */
	u32 rmc;		/* RMC information */
	u32 pad0;
};

#define FZA_RX_BUFFER_SIZE	(4096 + 512)	/* buffer length */

/* Receive ring RMC constants. */
#define FZA_RING_RX_SOP		0x80000000	/* start of packet */
#define FZA_RING_RX_EOP		0x40000000	/* end of packet */
#define FZA_RING_RX_FSC_MASK	0x38000000	/* # of frame status bits */
#define FZA_RING_RX_FSB_MASK	0x07c00000	/* frame status bits */
#define FZA_RING_RX_FSB_ERR	0x04000000	/* error detected */
#define FZA_RING_RX_FSB_ADDR	0x02000000	/* address recognized */
#define FZA_RING_RX_FSB_COP	0x01000000	/* frame copied */
#define FZA_RING_RX_FSB_F0	0x00800000	/* first additional flag */
#define FZA_RING_RX_FSB_F1	0x00400000	/* second additional flag */
#define FZA_RING_RX_BAD		0x00200000	/* bad packet */
#define FZA_RING_RX_CRC		0x00100000	/* CRC error */
#define FZA_RING_RX_RRR_MASK	0x000e0000	/* MAC receive status bits */
#define FZA_RING_RX_RRR_OK	0x00000000	/* receive OK */
#define FZA_RING_RX_RRR_SADDR	0x00020000	/* source address matched */
#define FZA_RING_RX_RRR_DADDR	0x00040000	/* dest address not matched */
#define FZA_RING_RX_RRR_ABORT	0x00060000	/* RMC abort */
#define FZA_RING_RX_RRR_LENGTH	0x00080000	/* invalid length */
#define FZA_RING_RX_RRR_FRAG	0x000a0000	/* fragment */
#define FZA_RING_RX_RRR_FORMAT	0x000c0000	/* format error */
#define FZA_RING_RX_RRR_RESET	0x000e0000	/* MAC reset */
#define FZA_RING_RX_DA_MASK	0x00018000	/* daddr match status bits */
#define FZA_RING_RX_DA_NONE	0x00000000	/* no match */
#define FZA_RING_RX_DA_PROM	0x00008000	/* promiscuous match */
#define FZA_RING_RX_DA_CAM	0x00010000	/* CAM entry match */
#define FZA_RING_RX_DA_LOCAL	0x00018000	/* link addr or LLC bcast */
#define FZA_RING_RX_SA_MASK	0x00006000	/* saddr match status bits */
#define FZA_RING_RX_SA_NONE	0x00000000	/* no match */
#define FZA_RING_RX_SA_ALIAS	0x00002000	/* alias address match */
#define FZA_RING_RX_SA_CAM	0x00004000	/* CAM entry match */
#define FZA_RING_RX_SA_LOCAL	0x00006000	/* link address match */

/* SMT (Station Management) transmit/receive descriptor ring entry. */
struct fza_ring_smt {
	u32 own;		/* bit 31: ownership, bits [30:0]: unused */
	u32 rmc;		/* RMC information */
	u32 buffer;		/* address of the buffer */
	u32 pad0;		/* reserved */
};

/* Ownership constants.
 *
 * Only an owner is permitted to process a given ring entry.
 * RMC transmit ring meanings are reversed.
 */
#define FZA_RING_OWN_MASK	0x80000000
#define FZA_RING_OWN_FZA	0x00000000	/* permit FZA, forbid host */
#define FZA_RING_OWN_HOST	0x80000000	/* permit host, forbid FZA */
#define FZA_RING_TX_OWN_RMC	0x80000000	/* permit RMC, forbid host */
#define FZA_RING_TX_OWN_HOST	0x00000000	/* permit host, forbid RMC */

/* RMC constants. */
#define FZA_RING_PBC_MASK	0x00001fff	/* frame length */

/* Layout of counter buffers. */

struct fza_counter {
	u32 msw;
	u32 lsw;
};

struct fza_counters {
	struct fza_counter sys_buf;	/* system buffer unavailable */
	struct fza_counter tx_under;	/* transmit underruns */
	struct fza_counter tx_fail;	/* transmit failures */
	struct fza_counter rx_over;	/* receive data overruns */
	struct fza_counter frame_cnt;	/* frame count */
	struct fza_counter error_cnt;	/* error count */
	struct fza_counter lost_cnt;	/* lost count */
	struct fza_counter rinit_in;	/* ring initialization initiated */
	struct fza_counter rinit_rx;	/* ring initialization received */
	struct fza_counter beac_in;	/* ring beacon initiated */
	struct fza_counter dup_addr;	/* duplicate address test failures */
	struct fza_counter dup_tok;	/* duplicate token detected */
	struct fza_counter purg_err;	/* ring purge errors */
	struct fza_counter strip_err;	/* bridge strip errors */
	struct fza_counter pct_in;	/* traces initiated */
	struct fza_counter pct_rx;	/* traces received */
	struct fza_counter lem_rej;	/* LEM rejects */
	struct fza_counter tne_rej;	/* TNE expiry rejects */
	struct fza_counter lem_event;	/* LEM events */
	struct fza_counter lct_rej;	/* LCT rejects */
	struct fza_counter conn_cmpl;	/* connections completed */
	struct fza_counter el_buf;	/* elasticity buffer errors */
};

/* Layout of command buffers. */

/* INIT command buffer.
 *
 * Values of default link parameters given are as obtained from a
 * DEFZA-AA rev. C03 board.  The board counts time in units of 80ns.
 */
struct fza_cmd_init {
	u32 tx_mode;			/* transmit mode */
	u32 hst_rx_size;		/* host receive ring entries */

	struct fza_counters counters;	/* counters */

	u8 rmc_rev[4];			/* RMC revision */
	u8 rom_rev[4];			/* ROM revision */
	u8 fw_rev[4];			/* firmware revision */

	u32 mop_type;			/* MOP device type */

	u32 hst_rx;			/* base of host rx descriptor ring */
	u32 rmc_tx;			/* base of RMC tx descriptor ring */
	u32 rmc_tx_size;		/* size of RMC tx descriptor ring */
	u32 smt_tx;			/* base of SMT tx descriptor ring */
	u32 smt_tx_size;		/* size of SMT tx descriptor ring */
	u32 smt_rx;			/* base of SMT rx descriptor ring */
	u32 smt_rx_size;		/* size of SMT rx descriptor ring */

	u32 hw_addr[2];			/* link address */

	u32 def_t_req;			/* default Requested TTRT (T_REQ) --
					 * C03: 100000 [80ns]
					 */
	u32 def_tvx;			/* default Valid Transmission Time
					 * (TVX) -- C03: 32768 [80ns]
					 */
	u32 def_t_max;			/* default Maximum TTRT (T_MAX) --
					 * C03: 2162688 [80ns]
					 */
	u32 lem_threshold;		/* default LEM threshold -- C03: 8 */
	u32 def_station_id[2];		/* default station ID */

	u32 pmd_type_alt;		/* alternative PMD type code */

	u32 smt_ver;			/* SMT version */

	u32 rtoken_timeout;		/* default restricted token timeout
					 * -- C03: 12500000 [80ns]
					 */
	u32 ring_purger;		/* default ring purger enable --
					 * C03: 1
					 */

	u32 smt_ver_max;		/* max SMT version ID */
	u32 smt_ver_min;		/* min SMT version ID */
	u32 pmd_type;			/* PMD type code */
};

/* INIT command PMD type codes. */
#define FZA_PMD_TYPE_MMF	  0	/* Multimode fiber */
#define FZA_PMD_TYPE_TW		101	/* ThinWire */
#define FZA_PMD_TYPE_STP	102	/* STP */

/* MODCAM/RDCAM command buffer. */
#define FZA_CMD_CAM_SIZE	64		/* CAM address entry count */
struct fza_cmd_cam {
	u32 hw_addr[FZA_CMD_CAM_SIZE][2];	/* CAM address entries */
};

/* PARAM command buffer.
 *
 * Permitted ranges given are as defined by the spec and obtained from a
 * DEFZA-AA rev. C03 board, respectively.  The rtoken_timeout field is
 * erroneously interpreted in units of ms.
 */
struct fza_cmd_param {
	u32 loop_mode;			/* loopback mode */
	u32 t_max;			/* Maximum TTRT (T_MAX)
					 * def: ??? [80ns]
					 * C03: [t_req+1,4294967295] [80ns]
					 */
	u32 t_req;			/* Requested TTRT (T_REQ)
					 * def: [50000,2097151] [80ns]
					 * C03: [50001,t_max-1] [80ns]
					 */
	u32 tvx;			/* Valid Transmission Time (TVX)
					 * def: [29375,65280] [80ns]
					 * C03: [29376,65279] [80ns]
					 */
	u32 lem_threshold;		/* LEM threshold */
	u32 station_id[2];		/* station ID */
	u32 rtoken_timeout;		/* restricted token timeout
					 * def: [0,125000000] [80ns]
					 * C03: [0,9999] [ms]
					 */
	u32 ring_purger;		/* ring purger enable: 0|1 */
};

/* Loopback modes for the PARAM command. */
#define FZA_LOOP_NORMAL		0
#define FZA_LOOP_INTERN		1
#define FZA_LOOP_EXTERN		2

/* MODPROM command buffer. */
struct fza_cmd_modprom {
	u32 llc_prom;			/* LLC promiscuous enable */
	u32 smt_prom;			/* SMT promiscuous enable */
	u32 llc_multi;			/* LLC multicast promiscuous enable */
	u32 llc_bcast;			/* LLC broadcast promiscuous enable */
};

/* SETCHAR command buffer.
 *
 * Permitted ranges are as for the PARAM command.
 */
struct fza_cmd_setchar {
	u32 t_max;			/* Maximum TTRT (T_MAX) */
	u32 t_req;			/* Requested TTRT (T_REQ) */
	u32 tvx;			/* Valid Transmission Time (TVX) */
	u32 lem_threshold;		/* LEM threshold */
	u32 rtoken_timeout;		/* restricted token timeout */
	u32 ring_purger;		/* ring purger enable */
};

/* RDCNTR command buffer. */
struct fza_cmd_rdcntr {
	struct fza_counters counters;	/* counters */
};

/* STATUS command buffer. */
struct fza_cmd_status {
	u32 led_state;			/* LED state */
	u32 rmt_state;			/* ring management state */
	u32 link_state;			/* link state */
	u32 dup_addr;			/* duplicate address flag */
	u32 ring_purger;		/* ring purger state */
	u32 t_neg;			/* negotiated TTRT [80ns] */
	u32 una[2];			/* upstream neighbour address */
	u32 una_timeout;		/* UNA timed out */
	u32 strip_mode;			/* frame strip mode */
	u32 yield_mode;			/* claim token yield mode */
	u32 phy_state;			/* PHY state */
	u32 neigh_phy;			/* neighbour PHY type */
	u32 reject;			/* reject reason */
	u32 phy_lee;			/* PHY link error estimate [-log10] */
	u32 una_old[2];			/* old upstream neighbour address */
	u32 rmt_mac;			/* remote MAC indicated */
	u32 ring_err;			/* ring error reason */
	u32 beac_rx[2];			/* sender of last directed beacon */
	u32 un_dup_addr;		/* upstream neighbr dup address flag */
	u32 dna[2];			/* downstream neighbour address */
	u32 dna_old[2];			/* old downstream neighbour address */
};

/* Common command buffer. */
union fza_cmd_buf {
	struct fza_cmd_init init;
	struct fza_cmd_cam cam;
	struct fza_cmd_param param;
	struct fza_cmd_modprom modprom;
	struct fza_cmd_setchar setchar;
	struct fza_cmd_rdcntr rdcntr;
	struct fza_cmd_status status;
};

/* MAC (Media Access Controller) chip packet request header constants. */

/* Packet request header byte #0. */
#define FZA_PRH0_FMT_TYPE_MASK	0xc0	/* type of packet, always zero */
#define FZA_PRH0_TOK_TYPE_MASK	0x30	/* type of token required
					 * to send this frame
					 */
#define FZA_PRH0_TKN_TYPE_ANY	0x30	/* use either token type */
#define FZA_PRH0_TKN_TYPE_UNR	0x20	/* use an unrestricted token */
#define FZA_PRH0_TKN_TYPE_RST	0x10	/* use a restricted token */
#define FZA_PRH0_TKN_TYPE_IMM	0x00	/* send immediately, no token required
					 */
#define FZA_PRH0_FRAME_MASK	0x08	/* type of frame to send */
#define FZA_PRH0_FRAME_SYNC	0x08	/* send a synchronous frame */
#define FZA_PRH0_FRAME_ASYNC	0x00	/* send an asynchronous frame */
#define FZA_PRH0_MODE_MASK	0x04	/* send mode */
#define FZA_PRH0_MODE_IMMED	0x04	/* an immediate mode, send regardless
					 * of the ring operational state
					 */
#define FZA_PRH0_MODE_NORMAL	0x00	/* a normal mode, send only if ring
					 * operational
					 */
#define FZA_PRH0_SF_MASK	0x02	/* send frame first */
#define FZA_PRH0_SF_FIRST	0x02	/* send this frame first
					 * with this token capture
					 */
#define FZA_PRH0_SF_NORMAL	0x00	/* treat this frame normally */
#define FZA_PRH0_BCN_MASK	0x01	/* beacon frame */
#define FZA_PRH0_BCN_BEACON	0x01	/* send the frame only
					 * if in the beacon state
					 */
#define FZA_PRH0_BCN_DATA	0x01	/* send the frame only
					 * if in the data state
					 */
/* Packet request header byte #1. */
					/* bit 7 always zero */
#define FZA_PRH1_SL_MASK	0x40	/* send frame last */
#define FZA_PRH1_SL_LAST	0x40	/* send this frame last, releasing
					 * the token afterwards
					 */
#define FZA_PRH1_SL_NORMAL	0x00	/* treat this frame normally */
#define FZA_PRH1_CRC_MASK	0x20	/* CRC append */
#define FZA_PRH1_CRC_NORMAL	0x20	/* calculate the CRC and append it
					 * as the FCS field to the frame
					 */
#define FZA_PRH1_CRC_SKIP	0x00	/* leave the frame as is */
#define FZA_PRH1_TKN_SEND_MASK	0x18	/* type of token to send after the
					 * frame if this is the last frame
					 */
#define FZA_PRH1_TKN_SEND_ORIG	0x18	/* send a token of the same type as the
					 * originally captured one
					 */
#define FZA_PRH1_TKN_SEND_RST	0x10	/* send a restricted token */
#define FZA_PRH1_TKN_SEND_UNR	0x08	/* send an unrestricted token */
#define FZA_PRH1_TKN_SEND_NONE	0x00	/* send no token */
#define FZA_PRH1_EXTRA_FS_MASK	0x07	/* send extra frame status indicators
					 */
#define FZA_PRH1_EXTRA_FS_ST	0x07	/* TR RR ST II */
#define FZA_PRH1_EXTRA_FS_SS	0x06	/* TR RR SS II */
#define FZA_PRH1_EXTRA_FS_SR	0x05	/* TR RR SR II */
#define FZA_PRH1_EXTRA_FS_NONE1	0x04	/* TR RR II II */
#define FZA_PRH1_EXTRA_FS_RT	0x03	/* TR RR RT II */
#define FZA_PRH1_EXTRA_FS_RS	0x02	/* TR RR RS II */
#define FZA_PRH1_EXTRA_FS_RR	0x01	/* TR RR RR II */
#define FZA_PRH1_EXTRA_FS_NONE	0x00	/* TR RR II II */
/* Packet request header byte #2. */
#define FZA_PRH2_NORMAL		0x00	/* always zero */

/* PRH used for LLC frames. */
#define FZA_PRH0_LLC		(FZA_PRH0_TKN_TYPE_UNR)
#define FZA_PRH1_LLC		(FZA_PRH1_CRC_NORMAL | FZA_PRH1_TKN_SEND_UNR)
#define FZA_PRH2_LLC		(FZA_PRH2_NORMAL)

/* PRH used for SMT frames. */
#define FZA_PRH0_SMT		(FZA_PRH0_TKN_TYPE_UNR)
#define FZA_PRH1_SMT		(FZA_PRH1_CRC_NORMAL | FZA_PRH1_TKN_SEND_UNR)
#define FZA_PRH2_SMT		(FZA_PRH2_NORMAL)

#if ((FZA_RING_RX_SIZE) < 2) || ((FZA_RING_RX_SIZE) > 256)
# error FZA_RING_RX_SIZE has to be from 2 up to 256
#endif
#if ((FZA_RING_TX_MODE) != 0) && ((FZA_RING_TX_MODE) != 1)
# error FZA_RING_TX_MODE has to be either 0 or 1
#endif

#define FZA_RING_TX_SIZE (512 << (FZA_RING_TX_MODE))

struct fza_private {
	struct device *bdev;		/* pointer to the bus device */
	const char *name;		/* printable device name */
	void __iomem *mmio;		/* MMIO ioremap cookie */
	struct fza_regs __iomem *regs;	/* pointer to FZA registers */

	struct sk_buff *rx_skbuff[FZA_RING_RX_SIZE];
					/* all skbs assigned to the host
					 * receive descriptors
					 */
	dma_addr_t rx_dma[FZA_RING_RX_SIZE];
					/* their corresponding DMA addresses */

	struct fza_ring_cmd __iomem *ring_cmd;
					/* pointer to the command descriptor
					 * ring
					 */
	int ring_cmd_index;		/* index to the command descriptor ring
					 * for the next command
					 */
	struct fza_ring_uns __iomem *ring_uns;
					/* pointer to the unsolicited
					 * descriptor ring
					 */
	int ring_uns_index;		/* index to the unsolicited descriptor
					 * ring for the next event
					 */

	struct fza_ring_rmc_tx __iomem *ring_rmc_tx;
					/* pointer to the RMC transmit
					 * descriptor ring (obtained from the
					 * INIT command)
					 */
	int ring_rmc_tx_size;		/* number of entries in the RMC
					 * transmit descriptor ring (obtained
					 * from the INIT command)
					 */
	int ring_rmc_tx_index;		/* index to the RMC transmit descriptor
					 * ring for the next transmission
					 */
	int ring_rmc_txd_index;		/* index to the RMC transmit descriptor
					 * ring for the next transmit done
					 * acknowledge
					 */

	struct fza_ring_hst_rx __iomem *ring_hst_rx;
					/* pointer to the host receive
					 * descriptor ring (obtained from the
					 * INIT command)
					 */
	int ring_hst_rx_size;		/* number of entries in the host
					 * receive descriptor ring (set by the
					 * INIT command)
					 */
	int ring_hst_rx_index;		/* index to the host receive descriptor
					 * ring for the next transmission
					 */

	struct fza_ring_smt __iomem *ring_smt_tx;
					/* pointer to the SMT transmit
					 * descriptor ring (obtained from the
					 * INIT command)
					 */
	int ring_smt_tx_size;		/* number of entries in the SMT
					 * transmit descriptor ring (obtained
					 * from the INIT command)
					 */
	int ring_smt_tx_index;		/* index to the SMT transmit descriptor
					 * ring for the next transmission
					 */

	struct fza_ring_smt __iomem *ring_smt_rx;
					/* pointer to the SMT transmit
					 * descriptor ring (obtained from the
					 * INIT command)
					 */
	int ring_smt_rx_size;		/* number of entries in the SMT
					 * receive descriptor ring (obtained
					 * from the INIT command)
					 */
	int ring_smt_rx_index;		/* index to the SMT receive descriptor
					 * ring for the next transmission
					 */

	struct fza_buffer_tx __iomem *buffer_tx;
					/* pointer to the RMC transmit buffers
					 */

	uint state;			/* adapter expected state */

	spinlock_t lock;		/* for device & private data access */
	uint int_mask;			/* interrupt source selector */

	int cmd_done_flag;		/* command completion trigger */
	wait_queue_head_t cmd_done_wait;

	int state_chg_flag;		/* state change trigger */
	wait_queue_head_t state_chg_wait;

	struct timer_list reset_timer;	/* RESET time-out trigger */
	int timer_state;		/* RESET trigger state */

	int queue_active;		/* whether to enable queueing */

	struct net_device_stats stats;

	uint irq_count_flush_tx;	/* transmit flush irqs */
	uint irq_count_uns_poll;	/* unsolicited event irqs */
	uint irq_count_smt_tx_poll;	/* SMT transmit irqs */
	uint irq_count_rx_poll;		/* host receive irqs */
	uint irq_count_tx_done;		/* transmit done irqs */
	uint irq_count_cmd_done;	/* command done irqs */
	uint irq_count_state_chg;	/* state change irqs */
	uint irq_count_link_st_chg;	/* link status change irqs */

	uint t_max;			/* T_MAX */
	uint t_req;			/* T_REQ */
	uint tvx;			/* TVX */
	uint lem_threshold;		/* LEM threshold */
	uint station_id[2];		/* station ID */
	uint rtoken_timeout;		/* restricted token timeout */
	uint ring_purger;		/* ring purger enable flag */
};

struct fza_fddihdr {
	u8 pa[2];			/* preamble */
	u8 sd;				/* starting delimiter */
	struct fddihdr hdr;
} __packed;
