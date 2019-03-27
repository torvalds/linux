/*
 * Copyright (c) 2013-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PT_OPCODES_H
#define PT_OPCODES_H


/* A one byte opcode. */
enum pt_opcode {
	pt_opc_pad		= 0x00,
	pt_opc_ext		= 0x02,
	pt_opc_psb		= pt_opc_ext,
	pt_opc_tip		= 0x0d,
	pt_opc_tnt_8		= 0x00,
	pt_opc_tip_pge		= 0x11,
	pt_opc_tip_pgd		= 0x01,
	pt_opc_fup		= 0x1d,
	pt_opc_mode		= 0x99,
	pt_opc_tsc		= 0x19,
	pt_opc_mtc		= 0x59,
	pt_opc_cyc		= 0x03,

	/* A free opcode to trigger a decode fault. */
	pt_opc_bad		= 0xd9
};

/* A one byte extension code for ext opcodes. */
enum pt_ext_code {
	pt_ext_psb		= 0x82,
	pt_ext_tnt_64		= 0xa3,
	pt_ext_pip		= 0x43,
	pt_ext_ovf		= 0xf3,
	pt_ext_psbend		= 0x23,
	pt_ext_cbr		= 0x03,
	pt_ext_tma		= 0x73,
	pt_ext_stop		= 0x83,
	pt_ext_vmcs		= 0xc8,
	pt_ext_ext2		= 0xc3,
	pt_ext_exstop		= 0x62,
	pt_ext_exstop_ip	= 0xe2,
	pt_ext_mwait		= 0xc2,
	pt_ext_pwre		= 0x22,
	pt_ext_pwrx		= 0xa2,
	pt_ext_ptw		= 0x12,

	pt_ext_bad		= 0x04
};

/* A one byte extension 2 code for ext2 extension opcodes. */
enum pt_ext2_code {
	pt_ext2_mnt		= 0x88,

	pt_ext2_bad		= 0x00
};

/* A one byte opcode mask. */
enum pt_opcode_mask {
	pt_opm_tip		= 0x1f,
	pt_opm_tnt_8		= 0x01,
	pt_opm_tnt_8_shr	= 1,
	pt_opm_fup		= pt_opm_tip,

	/* The bit mask for the compression bits in the opcode. */
	pt_opm_ipc		= 0xe0,

	/* The shift right value for ipc bits. */
	pt_opm_ipc_shr		= 5,

	/* The bit mask for the compression bits after shifting. */
	pt_opm_ipc_shr_mask	= 0x7,

	/* Shift counts and masks for decoding the cyc packet. */
	pt_opm_cyc              = 0x03,
	pt_opm_cyc_ext          = 0x04,
	pt_opm_cyc_bits         = 0xf8,
	pt_opm_cyc_shr          = 3,
	pt_opm_cycx_ext         = 0x01,
	pt_opm_cycx_shr         = 1,

	/* The bit mask for the IP bit in the exstop packet. */
	pt_opm_exstop_ip	= 0x80,

	/* The PTW opcode. */
	pt_opm_ptw		= 0x1f,

	/* The bit mask for the IP bit in the ptw packet. */
	pt_opm_ptw_ip		= 0x80,

	/* The bit mask and shr value for the payload bytes field in ptw. */
	pt_opm_ptw_pb		= 0x60,
	pt_opm_ptw_pb_shr	= 5,

	/* The bit mask for the payload bytes field in ptw after shifting. */
	pt_opm_ptw_pb_shr_mask	= 0x3
};

/* The size of the various opcodes in bytes. */
enum pt_opcode_size {
	pt_opcs_pad		= 1,
	pt_opcs_tip		= 1,
	pt_opcs_tip_pge		= 1,
	pt_opcs_tip_pgd		= 1,
	pt_opcs_fup		= 1,
	pt_opcs_tnt_8		= 1,
	pt_opcs_mode		= 1,
	pt_opcs_tsc		= 1,
	pt_opcs_mtc		= 1,
	pt_opcs_cyc		= 1,
	pt_opcs_psb		= 2,
	pt_opcs_psbend		= 2,
	pt_opcs_ovf		= 2,
	pt_opcs_pip		= 2,
	pt_opcs_tnt_64		= 2,
	pt_opcs_cbr		= 2,
	pt_opcs_tma		= 2,
	pt_opcs_stop		= 2,
	pt_opcs_vmcs		= 2,
	pt_opcs_mnt		= 3,
	pt_opcs_exstop		= 2,
	pt_opcs_mwait		= 2,
	pt_opcs_pwre		= 2,
	pt_opcs_pwrx		= 2,
	pt_opcs_ptw		= 2
};

/* The psb magic payload.
 *
 * The payload is a repeating 2-byte pattern.
 */
enum pt_psb_pattern {
	/* The high and low bytes in the pattern. */
	pt_psb_hi		= pt_opc_psb,
	pt_psb_lo		= pt_ext_psb,

	/* Various combinations of the above parts. */
	pt_psb_lohi		= pt_psb_lo | pt_psb_hi << 8,
	pt_psb_hilo		= pt_psb_hi | pt_psb_lo << 8,

	/* The repeat count of the payload, not including opc and ext. */
	pt_psb_repeat_count	= 7,

	/* The size of the repeated pattern in bytes. */
	pt_psb_repeat_size	= 2
};

/* The payload details. */
enum pt_payload {
	/* The shift counts for post-processing the PIP payload. */
	pt_pl_pip_shr		= 1,
	pt_pl_pip_shl		= 5,

	/* The size of a PIP payload in bytes. */
	pt_pl_pip_size		= 6,

	/* The non-root bit in the first byte of the PIP payload. */
	pt_pl_pip_nr            = 0x01,

	/* The size of a 8bit TNT packet's payload in bits. */
	pt_pl_tnt_8_bits	= 8 - pt_opm_tnt_8_shr,

	/* The size of a 64bit TNT packet's payload in bytes. */
	pt_pl_tnt_64_size	= 6,

	/* The size of a 64bit TNT packet's payload in bits. */
	pt_pl_tnt_64_bits	= 48,

	/* The size of a TSC packet's payload in bytes and in bits. */
	pt_pl_tsc_size		= 7,
	pt_pl_tsc_bit_size	= pt_pl_tsc_size * 8,

	/* The size of a CBR packet's payload in bytes. */
	pt_pl_cbr_size		= 2,

	/* The size of a PSB packet's payload in bytes. */
	pt_pl_psb_size		= pt_psb_repeat_count * pt_psb_repeat_size,

	/* The size of a MODE packet's payload in bytes. */
	pt_pl_mode_size		= 1,

	/* The size of an IP packet's payload with update-16 compression. */
	pt_pl_ip_upd16_size	= 2,

	/* The size of an IP packet's payload with update-32 compression. */
	pt_pl_ip_upd32_size	= 4,

	/* The size of an IP packet's payload with update-48 compression. */
	pt_pl_ip_upd48_size	= 6,

	/* The size of an IP packet's payload with sext-48 compression. */
	pt_pl_ip_sext48_size	= 6,

	/* The size of an IP packet's payload with full-ip compression. */
	pt_pl_ip_full_size	= 8,

	/* Byte locations, sizes, and masks for processing TMA packets. */
	pt_pl_tma_size		= 5,
	pt_pl_tma_ctc_size	= 2,
	pt_pl_tma_ctc_bit_size	= pt_pl_tma_ctc_size * 8,
	pt_pl_tma_ctc_0		= 2,
	pt_pl_tma_ctc_1		= 3,
	pt_pl_tma_ctc_mask	= (1 << pt_pl_tma_ctc_bit_size) - 1,
	pt_pl_tma_fc_size	= 2,
	pt_pl_tma_fc_bit_size	= 9,
	pt_pl_tma_fc_0		= 5,
	pt_pl_tma_fc_1		= 6,
	pt_pl_tma_fc_mask	= (1 << pt_pl_tma_fc_bit_size) - 1,

	/* The size of a MTC packet's payload in bytes and in bits. */
	pt_pl_mtc_size		= 1,
	pt_pl_mtc_bit_size	= pt_pl_mtc_size * 8,

	/* A mask for the MTC payload bits. */
	pt_pl_mtc_mask		= (1 << pt_pl_mtc_bit_size) - 1,

	/* The maximal payload size in bytes of a CYC packet. */
	pt_pl_cyc_max_size	= 15,

	/* The size of a VMCS packet's payload in bytes. */
	pt_pl_vmcs_size		= 5,

	/* The shift counts for post-processing the VMCS payload. */
	pt_pl_vmcs_shl		= 12,

	/* The size of a MNT packet's payload in bytes. */
	pt_pl_mnt_size		= 8,

	/* The bit-mask for the IP bit in the EXSTOP opcode extension. */
	pt_pl_exstop_ip_mask	= 0x80,

	/* The size of the hints field in the MWAIT payload in bytes. */
	pt_pl_mwait_hints_size	= 4,

	/* The size of the extensions field in the MWAIT payload in bytes. */
	pt_pl_mwait_ext_size	= 4,

	/* The size of the MWAIT payload in bytes. */
	pt_pl_mwait_size	= pt_pl_mwait_hints_size + pt_pl_mwait_ext_size,

	/* The size of the PWRE payload in bytes. */
	pt_pl_pwre_size		= 2,

	/* The bit-mask for the h/w bit in the PWRE payload. */
	pt_pl_pwre_hw_mask	= 0x8,

	/* The bit-mask for the resolved thread sub C-state in the PWRE
	 * payload.
	 */
	pt_pl_pwre_sub_state_mask	= 0xf00,

	/* The shift right value for the resolved thread sub C-state in the
	 * PWRE payload.
	 */
	pt_pl_pwre_sub_state_shr	= 8,

	/* The bit-mask for the resolved thread C-state in the PWRE payload. */
	pt_pl_pwre_state_mask	= 0xf000,

	/* The shift right value for the resolved thread C-state in the
	 * PWRE payload.
	 */
	pt_pl_pwre_state_shr	= 12,

	/* The size of the PWRX payload in bytes. */
	pt_pl_pwrx_size		= 5,

	/* The bit-mask for the deepest core C-state in the PWRX payload. */
	pt_pl_pwrx_deepest_mask	= 0xf,

	/* The shift right value for the deepest core C-state in the PWRX
	 * payload.
	 */
	pt_pl_pwrx_deepest_shr	= 0,

	/* The bit-mask for the last core C-state in the PWRX payload. */
	pt_pl_pwrx_last_mask	= 0xf0,

	/* The shift right value for the last core C-state in the PWRX
	 * payload.
	 */
	pt_pl_pwrx_last_shr	= 4,

	/* The bit-mask for the wake reason in the PWRX payload. */
	pt_pl_pwrx_wr_mask	= 0xf00,

	/* The shift right value for the wake reason in the PWRX payload. */
	pt_pl_pwrx_wr_shr	= 8,

	/* The bit-mask for the interrupt wake reason in the PWRX payload. */
	pt_pl_pwrx_wr_int	= 0x100,

	/* The bit-mask for the store wake reason in the PWRX payload. */
	pt_pl_pwrx_wr_store	= 0x400,

	/* The bit-mask for the autonomous wake reason in the PWRX payload. */
	pt_pl_pwrx_wr_hw	= 0x800
};

/* Mode packet masks. */
enum pt_mode_mask {
	pt_mom_leaf		= 0xe0,
	pt_mom_leaf_shr		= 5,
	pt_mom_bits		= 0x1f
};

/* Mode packet bits. */
enum pt_mode_bit {
	/* mode.exec */
	pt_mob_exec_csl		= 0x01,
	pt_mob_exec_csd		= 0x02,

	/* mode.tsx */
	pt_mob_tsx_intx		= 0x01,
	pt_mob_tsx_abrt		= 0x02
};

/* The size of the various packets in bytes. */
enum pt_packet_size {
	ptps_pad		= pt_opcs_pad,
	ptps_tnt_8		= pt_opcs_tnt_8,
	ptps_mode		= pt_opcs_mode + pt_pl_mode_size,
	ptps_tsc		= pt_opcs_tsc + pt_pl_tsc_size,
	ptps_mtc		= pt_opcs_mtc + pt_pl_mtc_size,
	ptps_psb		= pt_opcs_psb + pt_pl_psb_size,
	ptps_psbend		= pt_opcs_psbend,
	ptps_ovf		= pt_opcs_ovf,
	ptps_pip		= pt_opcs_pip + pt_pl_pip_size,
	ptps_tnt_64		= pt_opcs_tnt_64 + pt_pl_tnt_64_size,
	ptps_cbr		= pt_opcs_cbr + pt_pl_cbr_size,
	ptps_tip_supp		= pt_opcs_tip,
	ptps_tip_upd16		= pt_opcs_tip + pt_pl_ip_upd16_size,
	ptps_tip_upd32		= pt_opcs_tip + pt_pl_ip_upd32_size,
	ptps_tip_upd48		= pt_opcs_tip + pt_pl_ip_upd48_size,
	ptps_tip_sext48		= pt_opcs_tip + pt_pl_ip_sext48_size,
	ptps_tip_full		= pt_opcs_tip + pt_pl_ip_full_size,
	ptps_tip_pge_supp	= pt_opcs_tip_pge,
	ptps_tip_pge_upd16	= pt_opcs_tip_pge + pt_pl_ip_upd16_size,
	ptps_tip_pge_upd32	= pt_opcs_tip_pge + pt_pl_ip_upd32_size,
	ptps_tip_pge_upd48	= pt_opcs_tip_pge + pt_pl_ip_upd48_size,
	ptps_tip_pge_sext48	= pt_opcs_tip_pge + pt_pl_ip_sext48_size,
	ptps_tip_pge_full	= pt_opcs_tip_pge + pt_pl_ip_full_size,
	ptps_tip_pgd_supp	= pt_opcs_tip_pgd,
	ptps_tip_pgd_upd16	= pt_opcs_tip_pgd + pt_pl_ip_upd16_size,
	ptps_tip_pgd_upd32	= pt_opcs_tip_pgd + pt_pl_ip_upd32_size,
	ptps_tip_pgd_upd48	= pt_opcs_tip_pgd + pt_pl_ip_upd48_size,
	ptps_tip_pgd_sext48	= pt_opcs_tip_pgd + pt_pl_ip_sext48_size,
	ptps_tip_pgd_full	= pt_opcs_tip_pgd + pt_pl_ip_full_size,
	ptps_fup_supp		= pt_opcs_fup,
	ptps_fup_upd16		= pt_opcs_fup + pt_pl_ip_upd16_size,
	ptps_fup_upd32		= pt_opcs_fup + pt_pl_ip_upd32_size,
	ptps_fup_upd48		= pt_opcs_fup + pt_pl_ip_upd48_size,
	ptps_fup_sext48		= pt_opcs_fup + pt_pl_ip_sext48_size,
	ptps_fup_full		= pt_opcs_fup + pt_pl_ip_full_size,
	ptps_tma		= pt_opcs_tma + pt_pl_tma_size,
	ptps_stop		= pt_opcs_stop,
	ptps_vmcs		= pt_opcs_vmcs + pt_pl_vmcs_size,
	ptps_mnt		= pt_opcs_mnt + pt_pl_mnt_size,
	ptps_exstop		= pt_opcs_exstop,
	ptps_mwait		= pt_opcs_mwait + pt_pl_mwait_size,
	ptps_pwre		= pt_opcs_pwre + pt_pl_pwre_size,
	ptps_pwrx		= pt_opcs_pwrx + pt_pl_pwrx_size,
	ptps_ptw_32		= pt_opcs_ptw + 4,
	ptps_ptw_64		= pt_opcs_ptw + 8
};

/* Supported address range configurations. */
enum pt_addr_cfg {
	pt_addr_cfg_disabled	= 0,
	pt_addr_cfg_filter	= 1,
	pt_addr_cfg_stop	= 2
};

#endif /* PT_OPCODES_H */
