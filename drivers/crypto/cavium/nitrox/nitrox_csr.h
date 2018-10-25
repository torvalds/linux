/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NITROX_CSR_H
#define __NITROX_CSR_H

#include <asm/byteorder.h>
#include <linux/types.h>

/* EMU clusters */
#define NR_CLUSTERS		4
/* Maximum cores per cluster,
 * varies based on partname
 */
#define AE_CORES_PER_CLUSTER	20
#define SE_CORES_PER_CLUSTER	16

#define AE_MAX_CORES	(AE_CORES_PER_CLUSTER * NR_CLUSTERS)
#define SE_MAX_CORES	(SE_CORES_PER_CLUSTER * NR_CLUSTERS)
#define ZIP_MAX_CORES	5

/* BIST registers */
#define EMU_BIST_STATUSX(_i)	(0x1402700 + ((_i) * 0x40000))
#define UCD_BIST_STATUS		0x12C0070
#define NPS_CORE_BIST_REG	0x10000E8
#define NPS_CORE_NPC_BIST_REG	0x1000128
#define NPS_PKT_SLC_BIST_REG	0x1040088
#define NPS_PKT_IN_BIST_REG	0x1040100
#define POM_BIST_REG		0x11C0100
#define BMI_BIST_REG		0x1140080
#define EFL_CORE_BIST_REGX(_i)	(0x1240100 + ((_i) * 0x400))
#define EFL_TOP_BIST_STAT	0x1241090
#define BMO_BIST_REG		0x1180080
#define LBC_BIST_STATUS		0x1200020
#define PEM_BIST_STATUSX(_i)	(0x1080468 | ((_i) << 18))

/* EMU registers */
#define EMU_SE_ENABLEX(_i)	(0x1400000 + ((_i) * 0x40000))
#define EMU_AE_ENABLEX(_i)	(0x1400008 + ((_i) * 0x40000))
#define EMU_WD_INT_ENA_W1SX(_i)	(0x1402318 + ((_i) * 0x40000))
#define EMU_GE_INT_ENA_W1SX(_i)	(0x1402518 + ((_i) * 0x40000))
#define EMU_FUSE_MAPX(_i)	(0x1402708 + ((_i) * 0x40000))

/* UCD registers */
#define UCD_UCODE_LOAD_BLOCK_NUM	0x12C0010
#define UCD_UCODE_LOAD_IDX_DATAX(_i)	(0x12C0018 + ((_i) * 0x20))
#define UCD_SE_EID_UCODE_BLOCK_NUMX(_i)	(0x12C0000 + ((_i) * 0x1000))

/* NPS core registers */
#define NPS_CORE_GBL_VFCFG	0x1000000
#define NPS_CORE_CONTROL	0x1000008
#define NPS_CORE_INT_ACTIVE	0x1000080
#define NPS_CORE_INT		0x10000A0
#define NPS_CORE_INT_ENA_W1S	0x10000B8
#define NPS_STATS_PKT_DMA_RD_CNT	0x1000180
#define NPS_STATS_PKT_DMA_WR_CNT	0x1000190

/* NPS packet registers */
#define NPS_PKT_INT				0x1040018
#define NPS_PKT_IN_RERR_HI		0x1040108
#define NPS_PKT_IN_RERR_HI_ENA_W1S	0x1040120
#define NPS_PKT_IN_RERR_LO		0x1040128
#define NPS_PKT_IN_RERR_LO_ENA_W1S	0x1040140
#define NPS_PKT_IN_ERR_TYPE		0x1040148
#define NPS_PKT_IN_ERR_TYPE_ENA_W1S	0x1040160
#define NPS_PKT_IN_INSTR_CTLX(_i)	(0x10060 + ((_i) * 0x40000))
#define NPS_PKT_IN_INSTR_BADDRX(_i)	(0x10068 + ((_i) * 0x40000))
#define NPS_PKT_IN_INSTR_RSIZEX(_i)	(0x10070 + ((_i) * 0x40000))
#define NPS_PKT_IN_DONE_CNTSX(_i)	(0x10080 + ((_i) * 0x40000))
#define NPS_PKT_IN_INSTR_BAOFF_DBELLX(_i)	(0x10078 + ((_i) * 0x40000))
#define NPS_PKT_IN_INT_LEVELSX(_i)		(0x10088 + ((_i) * 0x40000))

#define NPS_PKT_SLC_RERR_HI		0x1040208
#define NPS_PKT_SLC_RERR_HI_ENA_W1S	0x1040220
#define NPS_PKT_SLC_RERR_LO		0x1040228
#define NPS_PKT_SLC_RERR_LO_ENA_W1S	0x1040240
#define NPS_PKT_SLC_ERR_TYPE		0x1040248
#define NPS_PKT_SLC_ERR_TYPE_ENA_W1S	0x1040260
#define NPS_PKT_SLC_CTLX(_i)		(0x10000 + ((_i) * 0x40000))
#define NPS_PKT_SLC_CNTSX(_i)		(0x10008 + ((_i) * 0x40000))
#define NPS_PKT_SLC_INT_LEVELSX(_i)	(0x10010 + ((_i) * 0x40000))

/* POM registers */
#define POM_INT_ENA_W1S		0x11C0018
#define POM_GRP_EXECMASKX(_i)	(0x11C1100 | ((_i) * 8))
#define POM_INT		0x11C0000
#define POM_PERF_CTL	0x11CC400

/* BMI registers */
#define BMI_INT		0x1140000
#define BMI_CTL		0x1140020
#define BMI_INT_ENA_W1S	0x1140018
#define BMI_NPS_PKT_CNT	0x1140070

/* EFL registers */
#define EFL_CORE_INT_ENA_W1SX(_i)		(0x1240018 + ((_i) * 0x400))
#define EFL_CORE_VF_ERR_INT0X(_i)		(0x1240050 + ((_i) * 0x400))
#define EFL_CORE_VF_ERR_INT0_ENA_W1SX(_i)	(0x1240068 + ((_i) * 0x400))
#define EFL_CORE_VF_ERR_INT1X(_i)		(0x1240070 + ((_i) * 0x400))
#define EFL_CORE_VF_ERR_INT1_ENA_W1SX(_i)	(0x1240088 + ((_i) * 0x400))
#define EFL_CORE_SE_ERR_INTX(_i)		(0x12400A0 + ((_i) * 0x400))
#define EFL_RNM_CTL_STATUS			0x1241800
#define EFL_CORE_INTX(_i)			(0x1240000 + ((_i) * 0x400))

/* BMO registers */
#define BMO_CTL2		0x1180028
#define BMO_NPS_SLC_PKT_CNT	0x1180078

/* LBC registers */
#define LBC_INT			0x1200000
#define LBC_INVAL_CTL		0x1201010
#define LBC_PLM_VF1_64_INT	0x1202008
#define LBC_INVAL_STATUS	0x1202010
#define LBC_INT_ENA_W1S		0x1203000
#define LBC_PLM_VF1_64_INT_ENA_W1S	0x1205008
#define LBC_PLM_VF65_128_INT		0x1206008
#define LBC_ELM_VF1_64_INT		0x1208000
#define LBC_PLM_VF65_128_INT_ENA_W1S	0x1209008
#define LBC_ELM_VF1_64_INT_ENA_W1S	0x120B000
#define LBC_ELM_VF65_128_INT		0x120C000
#define LBC_ELM_VF65_128_INT_ENA_W1S	0x120F000

#define RST_BOOT	0x10C1600
#define FUS_DAT1	0x10C1408

/* PEM registers */
#define PEM0_INT 0x1080428

/**
 * struct emu_fuse_map - EMU Fuse Map Registers
 * @ae_fuse: Fuse settings for AE 19..0
 * @se_fuse: Fuse settings for SE 15..0
 *
 * A set bit indicates the unit is fuse disabled.
 */
union emu_fuse_map {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 valid : 1;
		u64 raz_52_62 : 11;
		u64 ae_fuse : 20;
		u64 raz_16_31 : 16;
		u64 se_fuse : 16;
#else
		u64 se_fuse : 16;
		u64 raz_16_31 : 16;
		u64 ae_fuse : 20;
		u64 raz_52_62 : 11;
		u64 valid : 1;
#endif
	} s;
};

/**
 * struct emu_se_enable - Symmetric Engine Enable Registers
 * @enable: Individual enables for each of the clusters
 *   16 symmetric engines.
 */
union emu_se_enable {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz	: 48;
		u64 enable : 16;
#else
		u64 enable : 16;
		u64 raz	: 48;
#endif
	} s;
};

/**
 * struct emu_ae_enable - EMU Asymmetric engines.
 * @enable: Individual enables for each of the cluster's
 *   20 Asymmetric Engines.
 */
union emu_ae_enable {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz	: 44;
		u64 enable : 20;
#else
		u64 enable : 20;
		u64 raz	: 44;
#endif
	} s;
};

/**
 * struct emu_wd_int_ena_w1s - EMU Interrupt Enable Registers
 * @ae_wd: Reads or sets enable for EMU(0..3)_WD_INT[AE_WD]
 * @se_wd: Reads or sets enable for EMU(0..3)_WD_INT[SE_WD]
 */
union emu_wd_int_ena_w1s {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz2 : 12;
		u64 ae_wd : 20;
		u64 raz1 : 16;
		u64 se_wd : 16;
#else
		u64 se_wd : 16;
		u64 raz1 : 16;
		u64 ae_wd : 20;
		u64 raz2 : 12;
#endif
	} s;
};

/**
 * struct emu_ge_int_ena_w1s - EMU Interrupt Enable set registers
 * @ae_ge: Reads or sets enable for EMU(0..3)_GE_INT[AE_GE]
 * @se_ge: Reads or sets enable for EMU(0..3)_GE_INT[SE_GE]
 */
union emu_ge_int_ena_w1s {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz_52_63 : 12;
		u64 ae_ge : 20;
		u64 raz_16_31: 16;
		u64 se_ge : 16;
#else
		u64 se_ge : 16;
		u64 raz_16_31: 16;
		u64 ae_ge : 20;
		u64 raz_52_63 : 12;
#endif
	} s;
};

/**
 * struct nps_pkt_slc_ctl - Solicited Packet Out Control Registers
 * @rh: Indicates whether to remove or include the response header
 *   1 = Include, 0 = Remove
 * @z: If set, 8 trailing 0x00 bytes will be added to the end of the
 *   outgoing packet.
 * @enb: Enable for this port.
 */
union nps_pkt_slc_ctl {
	u64 value;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 raz : 61;
		u64 rh : 1;
		u64 z : 1;
		u64 enb : 1;
#else
		u64 enb : 1;
		u64 z : 1;
		u64 rh : 1;
		u64 raz : 61;
#endif
	} s;
};

/**
 * struct nps_pkt_slc_cnts - Solicited Packet Out Count Registers
 * @slc_int: Returns a 1 when:
 *   NPS_PKT_SLC(i)_CNTS[CNT] > NPS_PKT_SLC(i)_INT_LEVELS[CNT], or
 *   NPS_PKT_SLC(i)_CNTS[TIMER] > NPS_PKT_SLC(i)_INT_LEVELS[TIMET].
 *   To clear the bit, the CNTS register must be written to clear.
 * @in_int: Returns a 1 when:
 *   NPS_PKT_IN(i)_DONE_CNTS[CNT] > NPS_PKT_IN(i)_INT_LEVELS[CNT].
 *   To clear the bit, the DONE_CNTS register must be written to clear.
 * @mbox_int: Returns a 1 when:
 *   NPS_PKT_MBOX_PF_VF(i)_INT[INTR] is set. To clear the bit,
 *   write NPS_PKT_MBOX_PF_VF(i)_INT[INTR] with 1.
 * @timer: Timer, incremented every 2048 coprocessor clock cycles
 *   when [CNT] is not zero. The hardware clears both [TIMER] and
 *   [INT] when [CNT] goes to 0.
 * @cnt: Packet counter. Hardware adds to [CNT] as it sends packets out.
 *   On a write to this CSR, hardware subtracts the amount written to the
 *   [CNT] field from [CNT].
 */
union nps_pkt_slc_cnts {
	u64 value;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 slc_int : 1;
		u64 uns_int : 1;
		u64 in_int : 1;
		u64 mbox_int : 1;
		u64 resend : 1;
		u64 raz : 5;
		u64 timer : 22;
		u64 cnt : 32;
#else
		u64 cnt	: 32;
		u64 timer : 22;
		u64 raz	: 5;
		u64 resend : 1;
		u64 mbox_int : 1;
		u64 in_int : 1;
		u64 uns_int : 1;
		u64 slc_int : 1;
#endif
	} s;
};

/**
 * struct nps_pkt_slc_int_levels - Solicited Packet Out Interrupt Levels
 *   Registers.
 * @bmode: Determines whether NPS_PKT_SLC_CNTS[CNT] is a byte or
 *   packet counter.
 * @timet: Output port counter time interrupt threshold.
 * @cnt: Output port counter interrupt threshold.
 */
union nps_pkt_slc_int_levels {
	u64 value;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 bmode : 1;
		u64 raz	: 9;
		u64 timet : 22;
		u64 cnt	: 32;
#else
		u64 cnt : 32;
		u64 timet : 22;
		u64 raz : 9;
		u64 bmode : 1;
#endif
	} s;
};

/**
 * struct nps_pkt_inst - NPS Packet Interrupt Register
 * @in_err: Set when any NPS_PKT_IN_RERR_HI/LO bit and
 *    corresponding NPS_PKT_IN_RERR_*_ENA_* bit are bot set.
 * @uns_err: Set when any NSP_PKT_UNS_RERR_HI/LO bit and
 *    corresponding NPS_PKT_UNS_RERR_*_ENA_* bit are both set.
 * @slc_er: Set when any NSP_PKT_SLC_RERR_HI/LO bit and
 *    corresponding NPS_PKT_SLC_RERR_*_ENA_* bit are both set.
 */
union nps_pkt_int {
	u64 value;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 raz	: 54;
		u64 uns_wto : 1;
		u64 in_err : 1;
		u64 uns_err : 1;
		u64 slc_err : 1;
		u64 in_dbe : 1;
		u64 in_sbe : 1;
		u64 uns_dbe : 1;
		u64 uns_sbe : 1;
		u64 slc_dbe : 1;
		u64 slc_sbe : 1;
#else
		u64 slc_sbe : 1;
		u64 slc_dbe : 1;
		u64 uns_sbe : 1;
		u64 uns_dbe : 1;
		u64 in_sbe : 1;
		u64 in_dbe : 1;
		u64 slc_err : 1;
		u64 uns_err : 1;
		u64 in_err : 1;
		u64 uns_wto : 1;
		u64 raz	: 54;
#endif
	} s;
};

/**
 * struct nps_pkt_in_done_cnts - Input instruction ring counts registers
 * @slc_cnt: Returns a 1 when:
 *    NPS_PKT_SLC(i)_CNTS[CNT] > NPS_PKT_SLC(i)_INT_LEVELS[CNT], or
 *    NPS_PKT_SLC(i)_CNTS[TIMER] > NPS_PKT_SCL(i)_INT_LEVELS[TIMET]
 *    To clear the bit, the CNTS register must be
 *    written to clear the underlying condition
 * @uns_int: Return a 1 when:
 *    NPS_PKT_UNS(i)_CNTS[CNT] > NPS_PKT_UNS(i)_INT_LEVELS[CNT], or
 *    NPS_PKT_UNS(i)_CNTS[TIMER] > NPS_PKT_UNS(i)_INT_LEVELS[TIMET]
 *    To clear the bit, the CNTS register must be
 *    written to clear the underlying condition
 * @in_int: Returns a 1 when:
 *    NPS_PKT_IN(i)_DONE_CNTS[CNT] > NPS_PKT_IN(i)_INT_LEVELS[CNT]
 *    To clear the bit, the DONE_CNTS register
 *    must be written to clear the underlying condition
 * @mbox_int: Returns a 1 when:
 *    NPS_PKT_MBOX_PF_VF(i)_INT[INTR] is set.
 *    To clear the bit, write NPS_PKT_MBOX_PF_VF(i)_INT[INTR]
 *    with 1.
 * @resend: A write of 1 will resend an MSI-X interrupt message if any
 *    of the following conditions are true for this ring "i".
 *    NPS_PKT_SLC(i)_CNTS[CNT] > NPS_PKT_SLC(i)_INT_LEVELS[CNT]
 *    NPS_PKT_SLC(i)_CNTS[TIMER] > NPS_PKT_SLC(i)_INT_LEVELS[TIMET]
 *    NPS_PKT_UNS(i)_CNTS[CNT] > NPS_PKT_UNS(i)_INT_LEVELS[CNT]
 *    NPS_PKT_UNS(i)_CNTS[TIMER] > NPS_PKT_UNS(i)_INT_LEVELS[TIMET]
 *    NPS_PKT_IN(i)_DONE_CNTS[CNT] > NPS_PKT_IN(i)_INT_LEVELS[CNT]
 *    NPS_PKT_MBOX_PF_VF(i)_INT[INTR] is set
 * @cnt: Packet counter. Hardware adds to [CNT] as it reads
 *    packets. On a write to this CSR, hardware substracts the
 *    amount written to the [CNT] field from [CNT], which will
 *    clear PKT_IN(i)_INT_STATUS[INTR] if [CNT] becomes <=
 *    NPS_PKT_IN(i)_INT_LEVELS[CNT]. This register should be
 *    cleared before enabling a ring by reading the current
 *    value and writing it back.
 */
union nps_pkt_in_done_cnts {
	u64 value;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 slc_int : 1;
		u64 uns_int : 1;
		u64 in_int : 1;
		u64 mbox_int : 1;
		u64 resend : 1;
		u64 raz : 27;
		u64 cnt	: 32;
#else
		u64 cnt	: 32;
		u64 raz	: 27;
		u64 resend : 1;
		u64 mbox_int : 1;
		u64 in_int : 1;
		u64 uns_int : 1;
		u64 slc_int : 1;
#endif
	} s;
};

/**
 * struct nps_pkt_in_instr_ctl - Input Instruction Ring Control Registers.
 * @is64b: If 1, the ring uses 64-byte instructions. If 0, the
 *   ring uses 32-byte instructions.
 * @enb: Enable for the input ring.
 */
union nps_pkt_in_instr_ctl {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz	: 62;
		u64 is64b : 1;
		u64 enb	: 1;
#else
		u64 enb	: 1;
		u64 is64b : 1;
		u64 raz : 62;
#endif
	} s;
};

/**
 * struct nps_pkt_in_instr_rsize - Input instruction ring size registers
 * @rsize: Ring size (number of instructions)
 */
union nps_pkt_in_instr_rsize {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz	: 32;
		u64 rsize : 32;
#else
		u64 rsize : 32;
		u64 raz	: 32;
#endif
	} s;
};

/**
 * struct nps_pkt_in_instr_baoff_dbell - Input instruction ring
 *   base address offset and doorbell registers
 * @aoff: Address offset. The offset from the NPS_PKT_IN_INSTR_BADDR
 *   where the next pointer is read.
 * @dbell: Pointer list doorbell count. Write operations to this field
 *   increments the present value here. Read operations return the
 *   present value.
 */
union nps_pkt_in_instr_baoff_dbell {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 aoff : 32;
		u64 dbell : 32;
#else
		u64 dbell : 32;
		u64 aoff : 32;
#endif
	} s;
};

/**
 * struct nps_core_int_ena_w1s - NPS core interrupt enable set register
 * @host_nps_wr_err: Reads or sets enable for
 *   NPS_CORE_INT[HOST_NPS_WR_ERR].
 * @npco_dma_malform: Reads or sets enable for
 *   NPS_CORE_INT[NPCO_DMA_MALFORM].
 * @exec_wr_timeout: Reads or sets enable for
 *   NPS_CORE_INT[EXEC_WR_TIMEOUT].
 * @host_wr_timeout: Reads or sets enable for
 *   NPS_CORE_INT[HOST_WR_TIMEOUT].
 * @host_wr_err: Reads or sets enable for
 *   NPS_CORE_INT[HOST_WR_ERR]
 */
union nps_core_int_ena_w1s {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz4 : 55;
		u64 host_nps_wr_err : 1;
		u64 npco_dma_malform : 1;
		u64 exec_wr_timeout : 1;
		u64 host_wr_timeout : 1;
		u64 host_wr_err : 1;
		u64 raz3 : 1;
		u64 raz2 : 1;
		u64 raz1 : 1;
		u64 raz0 : 1;
#else
		u64 raz0 : 1;
		u64 raz1 : 1;
		u64 raz2 : 1;
		u64 raz3 : 1;
		u64 host_wr_err	: 1;
		u64 host_wr_timeout : 1;
		u64 exec_wr_timeout : 1;
		u64 npco_dma_malform : 1;
		u64 host_nps_wr_err : 1;
		u64 raz4 : 55;
#endif
	} s;
};

/**
 * struct nps_core_gbl_vfcfg - Global VF Configuration Register.
 * @ilk_disable: When set, this bit indicates that the ILK interface has
 *    been disabled.
 * @obaf: BMO allocation control
 *    0 = allocate per queue
 *    1 = allocate per VF
 * @ibaf: BMI allocation control
 *    0 = allocate per queue
 *    1 = allocate per VF
 * @zaf: ZIP allocation control
 *    0 = allocate per queue
 *    1 = allocate per VF
 * @aeaf: AE allocation control
 *    0 = allocate per queue
 *    1 = allocate per VF
 * @seaf: SE allocation control
 *    0 = allocation per queue
 *    1 = allocate per VF
 * @cfg: VF/PF mode.
 */
union nps_core_gbl_vfcfg {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64  raz :55;
		u64  ilk_disable :1;
		u64  obaf :1;
		u64  ibaf :1;
		u64  zaf :1;
		u64  aeaf :1;
		u64  seaf :1;
		u64  cfg :3;
#else
		u64  cfg :3;
		u64  seaf :1;
		u64  aeaf :1;
		u64  zaf :1;
		u64  ibaf :1;
		u64  obaf :1;
		u64  ilk_disable :1;
		u64  raz :55;
#endif
	} s;
};

/**
 * struct nps_core_int_active - NPS Core Interrupt Active Register
 * @resend: Resend MSI-X interrupt if needs to handle interrupts
 *    Sofware can set this bit and then exit the ISR.
 * @ocla: Set when any OCLA(0)_INT and corresponding OCLA(0_INT_ENA_W1C
 *    bit are set
 * @mbox: Set when any NPS_PKT_MBOX_INT_LO/HI and corresponding
 *    NPS_PKT_MBOX_INT_LO_ENA_W1C/HI_ENA_W1C bits are set
 * @emu: bit i is set in [EMU] when any EMU(i)_INT bit is set
 * @bmo: Set when any BMO_INT bit is set
 * @bmi: Set when any BMI_INT bit is set or when any non-RO
 *    BMI_INT and corresponding BMI_INT_ENA_W1C bits are both set
 * @aqm: Set when any AQM_INT bit is set
 * @zqm: Set when any ZQM_INT bit is set
 * @efl: Set when any EFL_INT RO bit is set or when any non-RO EFL_INT
 *    and corresponding EFL_INT_ENA_W1C bits are both set
 * @ilk: Set when any ILK_INT bit is set
 * @lbc: Set when any LBC_INT RO bit is set or when any non-RO LBC_INT
 *    and corresponding LBC_INT_ENA_W1C bits are bot set
 * @pem: Set when any PEM(0)_INT RO bit is set or when any non-RO
 *    PEM(0)_INT and corresponding PEM(0)_INT_ENA_W1C bit are both set
 * @ucd: Set when any UCD_INT bit is set
 * @zctl: Set when any ZIP_INT RO bit is set or when any non-RO ZIP_INT
 *    and corresponding ZIP_INT_ENA_W1C bits are both set
 * @lbm: Set when any LBM_INT bit is set
 * @nps_pkt: Set when any NPS_PKT_INT bit is set
 * @nps_core: Set when any NPS_CORE_INT RO bit is set or when non-RO
 *    NPS_CORE_INT and corresponding NSP_CORE_INT_ENA_W1C bits are both set
 */
union nps_core_int_active {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 resend : 1;
		u64 raz	: 43;
		u64 ocla : 1;
		u64 mbox : 1;
		u64 emu	: 4;
		u64 bmo	: 1;
		u64 bmi	: 1;
		u64 aqm	: 1;
		u64 zqm	: 1;
		u64 efl	: 1;
		u64 ilk	: 1;
		u64 lbc	: 1;
		u64 pem	: 1;
		u64 pom	: 1;
		u64 ucd	: 1;
		u64 zctl : 1;
		u64 lbm	: 1;
		u64 nps_pkt : 1;
		u64 nps_core : 1;
#else
		u64 nps_core : 1;
		u64 nps_pkt : 1;
		u64 lbm	: 1;
		u64 zctl: 1;
		u64 ucd	: 1;
		u64 pom	: 1;
		u64 pem	: 1;
		u64 lbc	: 1;
		u64 ilk	: 1;
		u64 efl	: 1;
		u64 zqm	: 1;
		u64 aqm	: 1;
		u64 bmi	: 1;
		u64 bmo	: 1;
		u64 emu	: 4;
		u64 mbox : 1;
		u64 ocla : 1;
		u64 raz	: 43;
		u64 resend : 1;
#endif
	} s;
};

/**
 * struct efl_core_int - EFL Interrupt Registers
 * @epci_decode_err: EPCI decoded a transacation that was unknown
 *    This error should only occurred when there is a micrcode/SE error
 *    and should be considered fatal
 * @ae_err: An AE uncorrectable error occurred.
 *    See EFL_CORE(0..3)_AE_ERR_INT
 * @se_err: An SE uncorrectable error occurred.
 *    See EFL_CORE(0..3)_SE_ERR_INT
 * @dbe: Double-bit error occurred in EFL
 * @sbe: Single-bit error occurred in EFL
 * @d_left: Asserted when new POM-Header-BMI-data is
 *    being sent to an Exec, and that Exec has Not read all BMI
 *    data associated with the previous POM header
 * @len_ovr: Asserted when an Exec-Read is issued that is more than
 *    14 greater in length that the BMI data left to be read
 */
union efl_core_int {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz	: 57;
		u64 epci_decode_err : 1;
		u64 ae_err : 1;
		u64 se_err : 1;
		u64 dbe	: 1;
		u64 sbe	: 1;
		u64 d_left : 1;
		u64 len_ovr : 1;
#else
		u64 len_ovr : 1;
		u64 d_left : 1;
		u64 sbe	: 1;
		u64 dbe	: 1;
		u64 se_err : 1;
		u64 ae_err : 1;
		u64 epci_decode_err  : 1;
		u64 raz	: 57;
#endif
	} s;
};

/**
 * struct efl_core_int_ena_w1s - EFL core interrupt enable set register
 * @epci_decode_err: Reads or sets enable for
 *   EFL_CORE(0..3)_INT[EPCI_DECODE_ERR].
 * @d_left: Reads or sets enable for
 *   EFL_CORE(0..3)_INT[D_LEFT].
 * @len_ovr: Reads or sets enable for
 *   EFL_CORE(0..3)_INT[LEN_OVR].
 */
union efl_core_int_ena_w1s {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz_7_63 : 57;
		u64 epci_decode_err : 1;
		u64 raz_2_5 : 4;
		u64 d_left : 1;
		u64 len_ovr : 1;
#else
		u64 len_ovr : 1;
		u64 d_left : 1;
		u64 raz_2_5 : 4;
		u64 epci_decode_err : 1;
		u64 raz_7_63 : 57;
#endif
	} s;
};

/**
 * struct efl_rnm_ctl_status - RNM Control and Status Register
 * @ent_sel: Select input to RNM FIFO
 * @exp_ent: Exported entropy enable for random number generator
 * @rng_rst: Reset to RNG. Setting this bit to 1 cancels the generation
 *    of the current random number.
 * @rnm_rst: Reset the RNM. Setting this bit to 1 clears all sorted numbers
 *    in the random number memory.
 * @rng_en: Enabled the output of the RNG.
 * @ent_en: Entropy enable for random number generator.
 */
union efl_rnm_ctl_status {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz_9_63 : 55;
		u64 ent_sel : 4;
		u64 exp_ent : 1;
		u64 rng_rst : 1;
		u64 rnm_rst : 1;
		u64 rng_en : 1;
		u64 ent_en : 1;
#else
		u64 ent_en : 1;
		u64 rng_en : 1;
		u64 rnm_rst : 1;
		u64 rng_rst : 1;
		u64 exp_ent : 1;
		u64 ent_sel : 4;
		u64 raz_9_63 : 55;
#endif
	} s;
};

/**
 * struct bmi_ctl - BMI control register
 * @ilk_hdrq_thrsh: Maximum number of header queue locations
 *   that ILK packets may consume. When the threshold is
 *   exceeded ILK_XOFF is sent to the BMI_X2P_ARB.
 * @nps_hdrq_thrsh: Maximum number of header queue locations
 *   that NPS packets may consume. When the threshold is
 *   exceeded NPS_XOFF is sent to the BMI_X2P_ARB.
 * @totl_hdrq_thrsh: Maximum number of header queue locations
 *   that the sum of ILK and NPS packets may consume.
 * @ilk_free_thrsh: Maximum number of buffers that ILK packet
 *   flows may consume before ILK_XOFF is sent to the BMI_X2P_ARB.
 * @nps_free_thrsh: Maximum number of buffers that NPS packet
 *   flows may consume before NPS XOFF is sent to the BMI_X2p_ARB.
 * @totl_free_thrsh: Maximum number of buffers that bot ILK and NPS
 *   packet flows may consume before both NPS_XOFF and ILK_XOFF
 *   are asserted to the BMI_X2P_ARB.
 * @max_pkt_len: Maximum packet length, integral number of 256B
 *   buffers.
 */
union bmi_ctl {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz_56_63 : 8;
		u64 ilk_hdrq_thrsh : 8;
		u64 nps_hdrq_thrsh : 8;
		u64 totl_hdrq_thrsh : 8;
		u64 ilk_free_thrsh : 8;
		u64 nps_free_thrsh : 8;
		u64 totl_free_thrsh : 8;
		u64 max_pkt_len : 8;
#else
		u64 max_pkt_len : 8;
		u64 totl_free_thrsh : 8;
		u64 nps_free_thrsh : 8;
		u64 ilk_free_thrsh : 8;
		u64 totl_hdrq_thrsh : 8;
		u64 nps_hdrq_thrsh : 8;
		u64 ilk_hdrq_thrsh : 8;
		u64 raz_56_63 : 8;
#endif
	} s;
};

/**
 * struct bmi_int_ena_w1s - BMI interrupt enable set register
 * @ilk_req_oflw: Reads or sets enable for
 *   BMI_INT[ILK_REQ_OFLW].
 * @nps_req_oflw: Reads or sets enable for
 *   BMI_INT[NPS_REQ_OFLW].
 * @fpf_undrrn: Reads or sets enable for
 *   BMI_INT[FPF_UNDRRN].
 * @eop_err_ilk: Reads or sets enable for
 *   BMI_INT[EOP_ERR_ILK].
 * @eop_err_nps: Reads or sets enable for
 *   BMI_INT[EOP_ERR_NPS].
 * @sop_err_ilk: Reads or sets enable for
 *   BMI_INT[SOP_ERR_ILK].
 * @sop_err_nps: Reads or sets enable for
 *   BMI_INT[SOP_ERR_NPS].
 * @pkt_rcv_err_ilk: Reads or sets enable for
 *   BMI_INT[PKT_RCV_ERR_ILK].
 * @pkt_rcv_err_nps: Reads or sets enable for
 *   BMI_INT[PKT_RCV_ERR_NPS].
 * @max_len_err_ilk: Reads or sets enable for
 *   BMI_INT[MAX_LEN_ERR_ILK].
 * @max_len_err_nps: Reads or sets enable for
 *   BMI_INT[MAX_LEN_ERR_NPS].
 */
union bmi_int_ena_w1s {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz_13_63	: 51;
		u64 ilk_req_oflw : 1;
		u64 nps_req_oflw : 1;
		u64 raz_10 : 1;
		u64 raz_9 : 1;
		u64 fpf_undrrn	: 1;
		u64 eop_err_ilk	: 1;
		u64 eop_err_nps	: 1;
		u64 sop_err_ilk	: 1;
		u64 sop_err_nps	: 1;
		u64 pkt_rcv_err_ilk : 1;
		u64 pkt_rcv_err_nps : 1;
		u64 max_len_err_ilk : 1;
		u64 max_len_err_nps : 1;
#else
		u64 max_len_err_nps : 1;
		u64 max_len_err_ilk : 1;
		u64 pkt_rcv_err_nps : 1;
		u64 pkt_rcv_err_ilk : 1;
		u64 sop_err_nps	: 1;
		u64 sop_err_ilk	: 1;
		u64 eop_err_nps	: 1;
		u64 eop_err_ilk	: 1;
		u64 fpf_undrrn	: 1;
		u64 raz_9 : 1;
		u64 raz_10 : 1;
		u64 nps_req_oflw : 1;
		u64 ilk_req_oflw : 1;
		u64 raz_13_63 : 51;
#endif
	} s;
};

/**
 * struct bmo_ctl2 - BMO Control2 Register
 * @arb_sel: Determines P2X Arbitration
 * @ilk_buf_thrsh: Maximum number of buffers that the
 *    ILK packet flows may consume before ILK XOFF is
 *    asserted to the POM.
 * @nps_slc_buf_thrsh: Maximum number of buffers that the
 *    NPS_SLC packet flow may consume before NPS_SLC XOFF is
 *    asserted to the POM.
 * @nps_uns_buf_thrsh: Maximum number of buffers that the
 *    NPS_UNS packet flow may consume before NPS_UNS XOFF is
 *    asserted to the POM.
 * @totl_buf_thrsh: Maximum number of buffers that ILK, NPS_UNS and
 *    NPS_SLC packet flows may consume before NPS_UNS XOFF, NSP_SLC and
 *    ILK_XOFF are all asserted POM.
 */
union bmo_ctl2 {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 arb_sel : 1;
		u64 raz_32_62 : 31;
		u64 ilk_buf_thrsh : 8;
		u64 nps_slc_buf_thrsh : 8;
		u64 nps_uns_buf_thrsh : 8;
		u64 totl_buf_thrsh : 8;
#else
		u64 totl_buf_thrsh : 8;
		u64 nps_uns_buf_thrsh : 8;
		u64 nps_slc_buf_thrsh : 8;
		u64 ilk_buf_thrsh : 8;
		u64 raz_32_62 : 31;
		u64 arb_sel : 1;
#endif
	} s;
};

/**
 * struct pom_int_ena_w1s - POM interrupt enable set register
 * @illegal_intf: Reads or sets enable for POM_INT[ILLEGAL_INTF].
 * @illegal_dport: Reads or sets enable for POM_INT[ILLEGAL_DPORT].
 */
union pom_int_ena_w1s {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz2 : 60;
		u64 illegal_intf : 1;
		u64 illegal_dport : 1;
		u64 raz1 : 1;
		u64 raz0 : 1;
#else
		u64 raz0 : 1;
		u64 raz1 : 1;
		u64 illegal_dport : 1;
		u64 illegal_intf : 1;
		u64 raz2 : 60;
#endif
	} s;
};

/**
 * struct lbc_inval_ctl - LBC invalidation control register
 * @wait_timer: Wait timer for wait state. [WAIT_TIMER] must
 *   always be written with its reset value.
 * @cam_inval_start: Software should write [CAM_INVAL_START]=1
 *   to initiate an LBC cache invalidation. After this, software
 *   should read LBC_INVAL_STATUS until LBC_INVAL_STATUS[DONE] is set.
 *   LBC hardware clears [CAVM_INVAL_START] before software can
 *   observed LBC_INVAL_STATUS[DONE] to be set
 */
union lbc_inval_ctl {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz2 : 48;
		u64 wait_timer : 8;
		u64 raz1 : 6;
		u64 cam_inval_start : 1;
		u64 raz0 : 1;
#else
		u64 raz0 : 1;
		u64 cam_inval_start : 1;
		u64 raz1 : 6;
		u64 wait_timer : 8;
		u64 raz2 : 48;
#endif
	} s;
};

/**
 * struct lbc_int_ena_w1s - LBC interrupt enable set register
 * @cam_hard_err: Reads or sets enable for LBC_INT[CAM_HARD_ERR].
 * @cam_inval_abort: Reads or sets enable for LBC_INT[CAM_INVAL_ABORT].
 * @over_fetch_err: Reads or sets enable for LBC_INT[OVER_FETCH_ERR].
 * @cache_line_to_err: Reads or sets enable for
 *   LBC_INT[CACHE_LINE_TO_ERR].
 * @cam_soft_err: Reads or sets enable for
 *   LBC_INT[CAM_SOFT_ERR].
 * @dma_rd_err: Reads or sets enable for
 *   LBC_INT[DMA_RD_ERR].
 */
union lbc_int_ena_w1s {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz_10_63 : 54;
		u64 cam_hard_err : 1;
		u64 cam_inval_abort : 1;
		u64 over_fetch_err : 1;
		u64 cache_line_to_err : 1;
		u64 raz_2_5 : 4;
		u64 cam_soft_err : 1;
		u64 dma_rd_err : 1;
#else
		u64 dma_rd_err : 1;
		u64 cam_soft_err : 1;
		u64 raz_2_5 : 4;
		u64 cache_line_to_err : 1;
		u64 over_fetch_err : 1;
		u64 cam_inval_abort : 1;
		u64 cam_hard_err : 1;
		u64 raz_10_63 : 54;
#endif
	} s;
};

/**
 * struct lbc_int - LBC interrupt summary register
 * @cam_hard_err: indicates a fatal hardware error.
 *   It requires system reset.
 *   When [CAM_HARD_ERR] is set, LBC stops logging any new information in
 *   LBC_POM_MISS_INFO_LOG,
 *   LBC_POM_MISS_ADDR_LOG,
 *   LBC_EFL_MISS_INFO_LOG, and
 *   LBC_EFL_MISS_ADDR_LOG.
 *   Software should sample them.
 * @cam_inval_abort: indicates a fatal hardware error.
 *   System reset is required.
 * @over_fetch_err: indicates a fatal hardware error
 *   System reset is required
 * @cache_line_to_err: is a debug feature.
 *   This timeout interrupt bit tells the software that
 *   a cacheline in LBC has non-zero usage and the context
 *   has not been used for greater than the
 *   LBC_TO_CNT[TO_CNT] time interval.
 * @sbe: Memory SBE error. This is recoverable via ECC.
 *   See LBC_ECC_INT for more details.
 * @dbe: Memory DBE error. This is a fatal and requires a
 *   system reset.
 * @pref_dat_len_mismatch_err: Summary bit for context length
 *   mismatch errors.
 * @rd_dat_len_mismatch_err: Summary bit for SE read data length
 *   greater than data prefect length errors.
 * @cam_soft_err: is recoverable. Software must complete a
 *   LBC_INVAL_CTL[CAM_INVAL_START] invalidation sequence and
 *   then clear [CAM_SOFT_ERR].
 * @dma_rd_err: A context prefect read of host memory returned with
 *   a read error.
 */
union lbc_int {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz_10_63 : 54;
		u64 cam_hard_err : 1;
		u64 cam_inval_abort : 1;
		u64 over_fetch_err : 1;
		u64 cache_line_to_err : 1;
		u64 sbe : 1;
		u64 dbe	: 1;
		u64 pref_dat_len_mismatch_err : 1;
		u64 rd_dat_len_mismatch_err : 1;
		u64 cam_soft_err : 1;
		u64 dma_rd_err : 1;
#else
		u64 dma_rd_err : 1;
		u64 cam_soft_err : 1;
		u64 rd_dat_len_mismatch_err : 1;
		u64 pref_dat_len_mismatch_err : 1;
		u64 dbe	: 1;
		u64 sbe	: 1;
		u64 cache_line_to_err : 1;
		u64 over_fetch_err : 1;
		u64 cam_inval_abort : 1;
		u64 cam_hard_err : 1;
		u64 raz_10_63 : 54;
#endif
	} s;
};

/**
 * struct lbc_inval_status: LBC Invalidation status register
 * @cam_clean_entry_complete_cnt: The number of entries that are
 *   cleaned up successfully.
 * @cam_clean_entry_cnt: The number of entries that have the CAM
 *   inval command issued.
 * @cam_inval_state: cam invalidation FSM state
 * @cam_inval_abort: cam invalidation abort
 * @cam_rst_rdy: lbc_cam reset ready
 * @done: LBC clears [DONE] when
 *   LBC_INVAL_CTL[CAM_INVAL_START] is written with a one,
 *   and sets [DONE] when it completes the invalidation
 *   sequence.
 */
union lbc_inval_status {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz3 : 23;
		u64 cam_clean_entry_complete_cnt : 9;
		u64 raz2 : 7;
		u64 cam_clean_entry_cnt : 9;
		u64 raz1 : 5;
		u64 cam_inval_state : 3;
		u64 raz0 : 5;
		u64 cam_inval_abort : 1;
		u64 cam_rst_rdy	: 1;
		u64 done : 1;
#else
		u64 done : 1;
		u64 cam_rst_rdy : 1;
		u64 cam_inval_abort : 1;
		u64 raz0 : 5;
		u64 cam_inval_state : 3;
		u64 raz1 : 5;
		u64 cam_clean_entry_cnt : 9;
		u64 raz2 : 7;
		u64 cam_clean_entry_complete_cnt : 9;
		u64 raz3 : 23;
#endif
	} s;
};

/**
 * struct rst_boot: RST Boot Register
 * @jtcsrdis: when set, internal CSR access via JTAG TAP controller
 *   is disabled
 * @jt_tst_mode: JTAG test mode
 * @io_supply: I/O power supply setting based on IO_VDD_SELECT pin:
 *    0x1 = 1.8V
 *    0x2 = 2.5V
 *    0x4 = 3.3V
 *    All other values are reserved
 * @pnr_mul: clock multiplier
 * @lboot: last boot cause mask, resets only with PLL_DC_OK
 * @rboot: determines whether core 0 remains in reset after
 *    chip cold or warm or soft reset
 * @rboot_pin: read only access to REMOTE_BOOT pin
 */
union rst_boot {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz_63 : 1;
		u64 jtcsrdis : 1;
		u64 raz_59_61 : 3;
		u64 jt_tst_mode : 1;
		u64 raz_40_57 : 18;
		u64 io_supply : 3;
		u64 raz_30_36 : 7;
		u64 pnr_mul : 6;
		u64 raz_12_23 : 12;
		u64 lboot : 10;
		u64 rboot : 1;
		u64 rboot_pin : 1;
#else
		u64 rboot_pin : 1;
		u64 rboot : 1;
		u64 lboot : 10;
		u64 raz_12_23 : 12;
		u64 pnr_mul : 6;
		u64 raz_30_36 : 7;
		u64 io_supply : 3;
		u64 raz_40_57 : 18;
		u64 jt_tst_mode : 1;
		u64 raz_59_61 : 3;
		u64 jtcsrdis : 1;
		u64 raz_63 : 1;
#endif
	};
};

/**
 * struct fus_dat1: Fuse Data 1 Register
 * @pll_mul: main clock PLL multiplier hardware limit
 * @pll_half_dis: main clock PLL control
 * @efus_lck: efuse lockdown
 * @zip_info: ZIP information
 * @bar2_sz_conf: when zero, BAR2 size conforms to
 *    PCIe specification
 * @efus_ign: efuse ignore
 * @nozip: ZIP disable
 * @pll_alt_matrix: select alternate PLL matrix
 * @pll_bwadj_denom: select CLKF denominator for
 *    BWADJ value
 * @chip_id: chip ID
 */
union fus_dat1 {
	u64 value;
	struct {
#if (defined(__BIG_ENDIAN_BITFIELD))
		u64 raz_57_63 : 7;
		u64 pll_mul : 3;
		u64 pll_half_dis : 1;
		u64 raz_43_52 : 10;
		u64 efus_lck : 3;
		u64 raz_26_39 : 14;
		u64 zip_info : 5;
		u64 bar2_sz_conf : 1;
		u64 efus_ign : 1;
		u64 nozip : 1;
		u64 raz_11_17 : 7;
		u64 pll_alt_matrix : 1;
		u64 pll_bwadj_denom : 2;
		u64 chip_id : 8;
#else
		u64 chip_id : 8;
		u64 pll_bwadj_denom : 2;
		u64 pll_alt_matrix : 1;
		u64 raz_11_17 : 7;
		u64 nozip : 1;
		u64 efus_ign : 1;
		u64 bar2_sz_conf : 1;
		u64 zip_info : 5;
		u64 raz_26_39 : 14;
		u64 efus_lck : 3;
		u64 raz_43_52 : 10;
		u64 pll_half_dis : 1;
		u64 pll_mul : 3;
		u64 raz_57_63 : 7;
#endif
	};
};

#endif /* __NITROX_CSR_H */
