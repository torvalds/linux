/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 */

#ifndef _CORESIGHT_CORESIGHT_ETM_H
#define _CORESIGHT_CORESIGHT_ETM_H

#include <asm/local.h>
#include <linux/spinlock.h>
#include "coresight-priv.h"

/*
 * Device registers:
 * 0x000 - 0x2FC: Trace		registers
 * 0x300 - 0x314: Management	registers
 * 0x318 - 0xEFC: Trace		registers
 * 0xF00: Management		registers
 * 0xFA0 - 0xFA4: Trace		registers
 * 0xFA8 - 0xFFC: Management	registers
 */
/* Trace registers (0x000-0x2FC) */
/* Main control and configuration registers */
#define TRCPRGCTLR			0x004
#define TRCPROCSELR			0x008
#define TRCSTATR			0x00C
#define TRCCONFIGR			0x010
#define TRCAUXCTLR			0x018
#define TRCEVENTCTL0R			0x020
#define TRCEVENTCTL1R			0x024
#define TRCSTALLCTLR			0x02C
#define TRCTSCTLR			0x030
#define TRCSYNCPR			0x034
#define TRCCCCTLR			0x038
#define TRCBBCTLR			0x03C
#define TRCTRACEIDR			0x040
#define TRCQCTLR			0x044
/* Filtering control registers */
#define TRCVICTLR			0x080
#define TRCVIIECTLR			0x084
#define TRCVISSCTLR			0x088
#define TRCVIPCSSCTLR			0x08C
#define TRCVDCTLR			0x0A0
#define TRCVDSACCTLR			0x0A4
#define TRCVDARCCTLR			0x0A8
/* Derived resources registers */
#define TRCSEQEVRn(n)			(0x100 + (n * 4))
#define TRCSEQRSTEVR			0x118
#define TRCSEQSTR			0x11C
#define TRCEXTINSELR			0x120
#define TRCCNTRLDVRn(n)			(0x140 + (n * 4))
#define TRCCNTCTLRn(n)			(0x150 + (n * 4))
#define TRCCNTVRn(n)			(0x160 + (n * 4))
/* ID registers */
#define TRCIDR8				0x180
#define TRCIDR9				0x184
#define TRCIDR10			0x188
#define TRCIDR11			0x18C
#define TRCIDR12			0x190
#define TRCIDR13			0x194
#define TRCIMSPEC0			0x1C0
#define TRCIMSPECn(n)			(0x1C0 + (n * 4))
#define TRCIDR0				0x1E0
#define TRCIDR1				0x1E4
#define TRCIDR2				0x1E8
#define TRCIDR3				0x1EC
#define TRCIDR4				0x1F0
#define TRCIDR5				0x1F4
#define TRCIDR6				0x1F8
#define TRCIDR7				0x1FC
/* Resource selection registers */
#define TRCRSCTLRn(n)			(0x200 + (n * 4))
/* Single-shot comparator registers */
#define TRCSSCCRn(n)			(0x280 + (n * 4))
#define TRCSSCSRn(n)			(0x2A0 + (n * 4))
#define TRCSSPCICRn(n)			(0x2C0 + (n * 4))
/* Management registers (0x300-0x314) */
#define TRCOSLAR			0x300
#define TRCOSLSR			0x304
#define TRCPDCR				0x310
#define TRCPDSR				0x314
/* Trace registers (0x318-0xEFC) */
/* Comparator registers */
#define TRCACVRn(n)			(0x400 + (n * 8))
#define TRCACATRn(n)			(0x480 + (n * 8))
#define TRCDVCVRn(n)			(0x500 + (n * 16))
#define TRCDVCMRn(n)			(0x580 + (n * 16))
#define TRCCIDCVRn(n)			(0x600 + (n * 8))
#define TRCVMIDCVRn(n)			(0x640 + (n * 8))
#define TRCCIDCCTLR0			0x680
#define TRCCIDCCTLR1			0x684
#define TRCVMIDCCTLR0			0x688
#define TRCVMIDCCTLR1			0x68C
/* Management register (0xF00) */
/* Integration control registers */
#define TRCITCTRL			0xF00
/* Trace registers (0xFA0-0xFA4) */
/* Claim tag registers */
#define TRCCLAIMSET			0xFA0
#define TRCCLAIMCLR			0xFA4
/* Management registers (0xFA8-0xFFC) */
#define TRCDEVAFF0			0xFA8
#define TRCDEVAFF1			0xFAC
#define TRCLAR				0xFB0
#define TRCLSR				0xFB4
#define TRCAUTHSTATUS			0xFB8
#define TRCDEVARCH			0xFBC
#define TRCDEVID			0xFC8
#define TRCDEVTYPE			0xFCC
#define TRCPIDR4			0xFD0
#define TRCPIDR5			0xFD4
#define TRCPIDR6			0xFD8
#define TRCPIDR7			0xFDC
#define TRCPIDR0			0xFE0
#define TRCPIDR1			0xFE4
#define TRCPIDR2			0xFE8
#define TRCPIDR3			0xFEC
#define TRCCIDR0			0xFF0
#define TRCCIDR1			0xFF4
#define TRCCIDR2			0xFF8
#define TRCCIDR3			0xFFC

/* ETMv4 resources */
#define ETM_MAX_NR_PE			8
#define ETMv4_MAX_CNTR			4
#define ETM_MAX_SEQ_STATES		4
#define ETM_MAX_EXT_INP_SEL		4
#define ETM_MAX_EXT_INP			256
#define ETM_MAX_EXT_OUT			4
#define ETM_MAX_SINGLE_ADDR_CMP		16
#define ETM_MAX_ADDR_RANGE_CMP		(ETM_MAX_SINGLE_ADDR_CMP / 2)
#define ETM_MAX_DATA_VAL_CMP		8
#define ETMv4_MAX_CTXID_CMP		8
#define ETM_MAX_VMID_CMP		8
#define ETM_MAX_PE_CMP			8
#define ETM_MAX_RES_SEL			16
#define ETM_MAX_SS_CMP			8

#define ETM_ARCH_V4			0x40
#define ETMv4_SYNC_MASK			0x1F
#define ETM_CYC_THRESHOLD_MASK		0xFFF
#define ETM_CYC_THRESHOLD_DEFAULT       0x100
#define ETMv4_EVENT_MASK		0xFF
#define ETM_CNTR_MAX_VAL		0xFFFF
#define ETM_TRACEID_MASK		0x3f

/* ETMv4 programming modes */
#define ETM_MODE_EXCLUDE		BIT(0)
#define ETM_MODE_LOAD			BIT(1)
#define ETM_MODE_STORE			BIT(2)
#define ETM_MODE_LOAD_STORE		BIT(3)
#define ETM_MODE_BB			BIT(4)
#define ETMv4_MODE_CYCACC		BIT(5)
#define ETMv4_MODE_CTXID		BIT(6)
#define ETM_MODE_VMID			BIT(7)
#define ETM_MODE_COND(val)		BMVAL(val, 8, 10)
#define ETMv4_MODE_TIMESTAMP		BIT(11)
#define ETM_MODE_RETURNSTACK		BIT(12)
#define ETM_MODE_QELEM(val)		BMVAL(val, 13, 14)
#define ETM_MODE_DATA_TRACE_ADDR	BIT(15)
#define ETM_MODE_DATA_TRACE_VAL		BIT(16)
#define ETM_MODE_ISTALL			BIT(17)
#define ETM_MODE_DSTALL			BIT(18)
#define ETM_MODE_ATB_TRIGGER		BIT(19)
#define ETM_MODE_LPOVERRIDE		BIT(20)
#define ETM_MODE_ISTALL_EN		BIT(21)
#define ETM_MODE_DSTALL_EN		BIT(22)
#define ETM_MODE_INSTPRIO		BIT(23)
#define ETM_MODE_NOOVERFLOW		BIT(24)
#define ETM_MODE_TRACE_RESET		BIT(25)
#define ETM_MODE_TRACE_ERR		BIT(26)
#define ETM_MODE_VIEWINST_STARTSTOP	BIT(27)
#define ETMv4_MODE_ALL			(GENMASK(27, 0) | \
					 ETM_MODE_EXCL_KERN | \
					 ETM_MODE_EXCL_USER)

#define TRCSTATR_IDLE_BIT		0
#define ETM_DEFAULT_ADDR_COMP		0

/* PowerDown Control Register bits */
#define TRCPDCR_PU			BIT(3)

/* secure state access levels */
#define ETM_EXLEVEL_S_APP		BIT(8)
#define ETM_EXLEVEL_S_OS		BIT(9)
#define ETM_EXLEVEL_S_NA		BIT(10)
#define ETM_EXLEVEL_S_HYP		BIT(11)
/* non-secure state access levels */
#define ETM_EXLEVEL_NS_APP		BIT(12)
#define ETM_EXLEVEL_NS_OS		BIT(13)
#define ETM_EXLEVEL_NS_HYP		BIT(14)
#define ETM_EXLEVEL_NS_NA		BIT(15)

/**
 * struct etmv4_config - configuration information related to an ETMv4
 * @mode:	Controls various modes supported by this ETM.
 * @pe_sel:	Controls which PE to trace.
 * @cfg:	Controls the tracing options.
 * @eventctrl0: Controls the tracing of arbitrary events.
 * @eventctrl1: Controls the behavior of the events that @event_ctrl0 selects.
 * @stallctl:	If functionality that prevents trace unit buffer overflows
 *		is available.
 * @ts_ctrl:	Controls the insertion of global timestamps in the
 *		trace streams.
 * @syncfreq:	Controls how often trace synchronization requests occur.
 *		the TRCCCCTLR register.
 * @ccctlr:	Sets the threshold value for cycle counting.
 * @vinst_ctrl:	Controls instruction trace filtering.
 * @viiectlr:	Set or read, the address range comparators.
 * @vissctlr:	Set, or read, the single address comparators that control the
 *		ViewInst start-stop logic.
 * @vipcssctlr:	Set, or read, which PE comparator inputs can control the
 *		ViewInst start-stop logic.
 * @seq_idx:	Sequencor index selector.
 * @seq_ctrl:	Control for the sequencer state transition control register.
 * @seq_rst:	Moves the sequencer to state 0 when a programmed event occurs.
 * @seq_state:	Set, or read the sequencer state.
 * @cntr_idx:	Counter index seletor.
 * @cntrldvr:	Sets or returns the reload count value for a counter.
 * @cntr_ctrl:	Controls the operation of a counter.
 * @cntr_val:	Sets or returns the value for a counter.
 * @res_idx:	Resource index selector.
 * @res_ctrl:	Controls the selection of the resources in the trace unit.
 * @ss_ctrl:	Controls the corresponding single-shot comparator resource.
 * @ss_status:	The status of the corresponding single-shot comparator.
 * @ss_pe_cmp:	Selects the PE comparator inputs for Single-shot control.
 * @addr_idx:	Address comparator index selector.
 * @addr_val:	Value for address comparator.
 * @addr_acc:	Address comparator access type.
 * @addr_type:	Current status of the comparator register.
 * @ctxid_idx:	Context ID index selector.
 * @ctxid_pid:	Value of the context ID comparator.
 * @ctxid_vpid:	Virtual PID seen by users if PID namespace is enabled, otherwise
 *		the same value of ctxid_pid.
 * @ctxid_mask0:Context ID comparator mask for comparator 0-3.
 * @ctxid_mask1:Context ID comparator mask for comparator 4-7.
 * @vmid_idx:	VM ID index selector.
 * @vmid_val:	Value of the VM ID comparator.
 * @vmid_mask0:	VM ID comparator mask for comparator 0-3.
 * @vmid_mask1:	VM ID comparator mask for comparator 4-7.
 * @ext_inp:	External input selection.
 */
struct etmv4_config {
	u32				mode;
	u32				pe_sel;
	u32				cfg;
	u32				eventctrl0;
	u32				eventctrl1;
	u32				stall_ctrl;
	u32				ts_ctrl;
	u32				syncfreq;
	u32				ccctlr;
	u32				bb_ctrl;
	u32				vinst_ctrl;
	u32				viiectlr;
	u32				vissctlr;
	u32				vipcssctlr;
	u8				seq_idx;
	u32				seq_ctrl[ETM_MAX_SEQ_STATES];
	u32				seq_rst;
	u32				seq_state;
	u8				cntr_idx;
	u32				cntrldvr[ETMv4_MAX_CNTR];
	u32				cntr_ctrl[ETMv4_MAX_CNTR];
	u32				cntr_val[ETMv4_MAX_CNTR];
	u8				res_idx;
	u32				res_ctrl[ETM_MAX_RES_SEL];
	u32				ss_ctrl[ETM_MAX_SS_CMP];
	u32				ss_status[ETM_MAX_SS_CMP];
	u32				ss_pe_cmp[ETM_MAX_SS_CMP];
	u8				addr_idx;
	u64				addr_val[ETM_MAX_SINGLE_ADDR_CMP];
	u64				addr_acc[ETM_MAX_SINGLE_ADDR_CMP];
	u8				addr_type[ETM_MAX_SINGLE_ADDR_CMP];
	u8				ctxid_idx;
	u64				ctxid_pid[ETMv4_MAX_CTXID_CMP];
	u64				ctxid_vpid[ETMv4_MAX_CTXID_CMP];
	u32				ctxid_mask0;
	u32				ctxid_mask1;
	u8				vmid_idx;
	u64				vmid_val[ETM_MAX_VMID_CMP];
	u32				vmid_mask0;
	u32				vmid_mask1;
	u32				ext_inp;
};

/**
 * struct etm4_drvdata - specifics associated to an ETM component
 * @base:       Memory mapped base address for this component.
 * @dev:        The device entity associated to this component.
 * @csdev:      Component vitals needed by the framework.
 * @spinlock:   Only one at a time pls.
 * @mode:	This tracer's mode, i.e sysFS, Perf or disabled.
 * @cpu:        The cpu this component is affined to.
 * @arch:       ETM version number.
 * @nr_pe:	The number of processing entity available for tracing.
 * @nr_pe_cmp:	The number of processing entity comparator inputs that are
 *		available for tracing.
 * @nr_addr_cmp:Number of pairs of address comparators available
 *		as found in ETMIDR4 0-3.
 * @nr_cntr:    Number of counters as found in ETMIDR5 bit 28-30.
 * @nr_ext_inp: Number of external input.
 * @numcidc:	Number of contextID comparators.
 * @numvmidc:	Number of VMID comparators.
 * @nrseqstate: The number of sequencer states that are implemented.
 * @nr_event:	Indicates how many events the trace unit support.
 * @nr_resource:The number of resource selection pairs available for tracing.
 * @nr_ss_cmp:	Number of single-shot comparator controls that are available.
 * @trcid:	value of the current ID for this component.
 * @trcid_size: Indicates the trace ID width.
 * @ts_size:	Global timestamp size field.
 * @ctxid_size:	Size of the context ID field to consider.
 * @vmid_size:	Size of the VM ID comparator to consider.
 * @ccsize:	Indicates the size of the cycle counter in bits.
 * @ccitmin:	minimum value that can be programmed in
 * @s_ex_level:	In secure state, indicates whether instruction tracing is
 *		supported for the corresponding Exception level.
 * @ns_ex_level:In non-secure state, indicates whether instruction tracing is
 *		supported for the corresponding Exception level.
 * @sticky_enable: true if ETM base configuration has been done.
 * @boot_enable:True if we should start tracing at boot time.
 * @os_unlock:  True if access to management registers is allowed.
 * @instrp0:	Tracing of load and store instructions
 *		as P0 elements is supported.
 * @trcbb:	Indicates if the trace unit supports branch broadcast tracing.
 * @trccond:	If the trace unit supports conditional
 *		instruction tracing.
 * @retstack:	Indicates if the implementation supports a return stack.
 * @trccci:	Indicates if the trace unit supports cycle counting
 *		for instruction.
 * @q_support:	Q element support characteristics.
 * @trc_error:	Whether a trace unit can trace a system
 *		error exception.
 * @syncpr:	Indicates if an implementation has a fixed
 *		synchronization period.
 * @stall_ctrl:	Enables trace unit functionality that prevents trace
 *		unit buffer overflows.
 * @sysstall:	Does the system support stall control of the PE?
 * @nooverflow:	Indicate if overflow prevention is supported.
 * @atbtrig:	If the implementation can support ATB triggers
 * @lpoverride:	If the implementation can support low-power state over.
 * @config:	structure holding configuration parameters.
 */
struct etmv4_drvdata {
	void __iomem			*base;
	struct device			*dev;
	struct coresight_device		*csdev;
	spinlock_t			spinlock;
	local_t				mode;
	int				cpu;
	u8				arch;
	u8				nr_pe;
	u8				nr_pe_cmp;
	u8				nr_addr_cmp;
	u8				nr_cntr;
	u8				nr_ext_inp;
	u8				numcidc;
	u8				numvmidc;
	u8				nrseqstate;
	u8				nr_event;
	u8				nr_resource;
	u8				nr_ss_cmp;
	u8				trcid;
	u8				trcid_size;
	u8				ts_size;
	u8				ctxid_size;
	u8				vmid_size;
	u8				ccsize;
	u8				ccitmin;
	u8				s_ex_level;
	u8				ns_ex_level;
	u8				q_support;
	bool				sticky_enable;
	bool				boot_enable;
	bool				os_unlock;
	bool				instrp0;
	bool				trcbb;
	bool				trccond;
	bool				retstack;
	bool				trccci;
	bool				trc_error;
	bool				syncpr;
	bool				stallctl;
	bool				sysstall;
	bool				nooverflow;
	bool				atbtrig;
	bool				lpoverride;
	struct etmv4_config		config;
};

/* Address comparator access types */
enum etm_addr_acctype {
	ETM_INSTR_ADDR,
	ETM_DATA_LOAD_ADDR,
	ETM_DATA_STORE_ADDR,
	ETM_DATA_LOAD_STORE_ADDR,
};

/* Address comparator context types */
enum etm_addr_ctxtype {
	ETM_CTX_NONE,
	ETM_CTX_CTXID,
	ETM_CTX_VMID,
	ETM_CTX_CTXID_VMID,
};

extern const struct attribute_group *coresight_etmv4_groups[];
void etm4_config_trace_mode(struct etmv4_config *config);
#endif
