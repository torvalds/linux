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

#ifndef INTEL_PT_H
#define INTEL_PT_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif


/* Intel(R) Processor Trace (Intel PT) decoder library.
 *
 * This file is logically structured into the following sections:
 *
 * - Version
 * - Errors
 * - Configuration
 * - Packet encoder / decoder
 * - Query decoder
 * - Traced image
 * - Instruction flow decoder
 * - Block decoder
 */



struct pt_encoder;
struct pt_packet_decoder;
struct pt_query_decoder;
struct pt_insn_decoder;
struct pt_block_decoder;



/* A macro to mark functions as exported. */
#ifndef pt_export
#  if defined(__GNUC__)
#    define pt_export __attribute__((visibility("default")))
#  elif defined(_MSC_VER)
#    define pt_export __declspec(dllimport)
#  else
#    error "unknown compiler"
#  endif
#endif



/* Version. */


/** The header version. */
#define LIBIPT_VERSION_MAJOR ${PT_VERSION_MAJOR}
#define LIBIPT_VERSION_MINOR ${PT_VERSION_MINOR}

#define LIBIPT_VERSION ((LIBIPT_VERSION_MAJOR << 8) + LIBIPT_VERSION_MINOR)


/** The library version. */
struct pt_version {
	/** Major version number. */
	uint8_t major;

	/** Minor version number. */
	uint8_t minor;

	/** Reserved bits. */
	uint16_t reserved;

	/** Build number. */
	uint32_t build;

	/** Version extension. */
	const char *ext;
};


/** Return the library version. */
extern pt_export struct pt_version pt_library_version(void);



/* Errors. */



/** Error codes. */
enum pt_error_code {
	/* No error. Everything is OK. */
	pte_ok,

	/* Internal decoder error. */
	pte_internal,

	/* Invalid argument. */
	pte_invalid,

	/* Decoder out of sync. */
	pte_nosync,

	/* Unknown opcode. */
	pte_bad_opc,

	/* Unknown payload. */
	pte_bad_packet,

	/* Unexpected packet context. */
	pte_bad_context,

	/* Decoder reached end of trace stream. */
	pte_eos,

	/* No packet matching the query to be found. */
	pte_bad_query,

	/* Decoder out of memory. */
	pte_nomem,

	/* Bad configuration. */
	pte_bad_config,

	/* There is no IP. */
	pte_noip,

	/* The IP has been suppressed. */
	pte_ip_suppressed,

	/* There is no memory mapped at the requested address. */
	pte_nomap,

	/* An instruction could not be decoded. */
	pte_bad_insn,

	/* No wall-clock time is available. */
	pte_no_time,

	/* No core:bus ratio available. */
	pte_no_cbr,

	/* Bad traced image. */
	pte_bad_image,

	/* A locking error. */
	pte_bad_lock,

	/* The requested feature is not supported. */
	pte_not_supported,

	/* The return address stack is empty. */
	pte_retstack_empty,

	/* A compressed return is not indicated correctly by a taken branch. */
	pte_bad_retcomp,

	/* The current decoder state does not match the state in the trace. */
	pte_bad_status_update,

	/* The trace did not contain an expected enabled event. */
	pte_no_enable,

	/* An event was ignored. */
	pte_event_ignored,

	/* Something overflowed. */
	pte_overflow,

	/* A file handling error. */
	pte_bad_file,

	/* Unknown cpu. */
	pte_bad_cpu
};


/** Decode a function return value into an pt_error_code. */
static inline enum pt_error_code pt_errcode(int status)
{
	return (status >= 0) ? pte_ok : (enum pt_error_code) -status;
}

/** Return a human readable error string. */
extern pt_export const char *pt_errstr(enum pt_error_code);



/* Configuration. */



/** A cpu vendor. */
enum pt_cpu_vendor {
	pcv_unknown,
	pcv_intel
};

/** A cpu identifier. */
struct pt_cpu {
	/** The cpu vendor. */
	enum pt_cpu_vendor vendor;

	/** The cpu family. */
	uint16_t family;

	/** The cpu model. */
	uint8_t model;

	/** The stepping. */
	uint8_t stepping;
};

/** A collection of Intel PT errata. */
struct pt_errata {
	/** BDM70: Intel(R) Processor Trace PSB+ Packets May Contain
	 *         Unexpected Packets.
	 *
	 * Same as: SKD024, SKL021, KBL021.
	 *
	 * Some Intel Processor Trace packets should be issued only between
	 * TIP.PGE and TIP.PGD packets.  Due to this erratum, when a TIP.PGE
	 * packet is generated it may be preceded by a PSB+ that incorrectly
	 * includes FUP and MODE.Exec packets.
	 */
	uint32_t bdm70:1;

	/** BDM64: An Incorrect LBR or Intel(R) Processor Trace Packet May Be
	 *         Recorded Following a Transactional Abort.
	 *
	 * Use of Intel(R) Transactional Synchronization Extensions (Intel(R)
	 * TSX) may result in a transactional abort.  If an abort occurs
	 * immediately following a branch instruction, an incorrect branch
	 * target may be logged in an LBR (Last Branch Record) or in an Intel(R)
	 * Processor Trace (Intel(R) PT) packet before the LBR or Intel PT
	 * packet produced by the abort.
	 */
	uint32_t bdm64:1;

	/** SKD007: Intel(R) PT Buffer Overflow May Result in Incorrect Packets.
	 *
	 * Same as: SKL049, KBL041.
	 *
	 * Under complex micro-architectural conditions, an Intel PT (Processor
	 * Trace) OVF (Overflow) packet may be issued after the first byte of a
	 * multi-byte CYC (Cycle Count) packet, instead of any remaining bytes
	 * of the CYC.
	 */
	uint32_t skd007:1;

	/** SKD022: VM Entry That Clears TraceEn May Generate a FUP.
	 *
	 * Same as: SKL024, KBL023.
	 *
	 * If VM entry clears Intel(R) PT (Intel Processor Trace)
	 * IA32_RTIT_CTL.TraceEn (MSR 570H, bit 0) while PacketEn is 1 then a
	 * FUP (Flow Update Packet) will precede the TIP.PGD (Target IP Packet,
	 * Packet Generation Disable).  VM entry can clear TraceEn if the
	 * VM-entry MSR-load area includes an entry for the IA32_RTIT_CTL MSR.
	 */
	uint32_t skd022:1;

	/** SKD010: Intel(R) PT FUP May be Dropped After OVF.
	 *
	 * Same as: SKD014, SKL033, KBL030.
	 *
	 * Some Intel PT (Intel Processor Trace) OVF (Overflow) packets may not
	 * be followed by a FUP (Flow Update Packet) or TIP.PGE (Target IP
	 * Packet, Packet Generation Enable).
	 */
	uint32_t skd010:1;

	/** SKL014: Intel(R) PT TIP.PGD May Not Have Target IP Payload.
	 *
	 * Same as: KBL014.
	 *
	 * When Intel PT (Intel Processor Trace) is enabled and a direct
	 * unconditional branch clears IA32_RTIT_STATUS.FilterEn (MSR 571H, bit
	 * 0), due to this erratum, the resulting TIP.PGD (Target IP Packet,
	 * Packet Generation Disable) may not have an IP payload with the target
	 * IP.
	 */
	uint32_t skl014:1;

	/** APL12: Intel(R) PT OVF May Be Followed By An Unexpected FUP Packet.
	 *
	 * Certain Intel PT (Processor Trace) packets including FUPs (Flow
	 * Update Packets), should be issued only between TIP.PGE (Target IP
	 * Packet - Packet Generaton Enable) and TIP.PGD (Target IP Packet -
	 * Packet Generation Disable) packets.  When outside a TIP.PGE/TIP.PGD
	 * pair, as a result of IA32_RTIT_STATUS.FilterEn[0] (MSR 571H) being
	 * cleared, an OVF (Overflow) packet may be unexpectedly followed by a
	 * FUP.
	 */
	uint32_t apl12:1;

	/** APL11: Intel(R) PT OVF Pakcet May Be Followed by TIP.PGD Packet
	 *
	 * If Intel PT (Processor Trace) encounters an internal buffer overflow
	 * and generates an OVF (Overflow) packet just as IA32_RTIT_CTL (MSR
	 * 570H) bit 0 (TraceEn) is cleared, or during a far transfer that
	 * causes IA32_RTIT_STATUS.ContextEn[1] (MSR 571H) to be cleared, the
	 * OVF may be followed by a TIP.PGD (Target Instruction Pointer - Packet
	 * Generation Disable) packet.
	 */
	uint32_t apl11:1;

	/* Reserve a few bytes for the future. */
	uint32_t reserved[15];
};

/** A collection of decoder-specific configuration flags. */
struct pt_conf_flags {
	/** The decoder variant. */
	union {
		/** Flags for the block decoder. */
		struct {
			/** End a block after a call instruction. */
			uint32_t end_on_call:1;

			/** Enable tick events for timing updates. */
			uint32_t enable_tick_events:1;

			/** End a block after a jump instruction. */
			uint32_t end_on_jump:1;
		} block;

		/** Flags for the instruction flow decoder. */
		struct {
			/** Enable tick events for timing updates. */
			uint32_t enable_tick_events:1;
		} insn;

		/* Reserve a few bytes for future extensions. */
		uint32_t reserved[4];
	} variant;
};

/** The address filter configuration. */
struct pt_conf_addr_filter {
	/** The address filter configuration.
	 *
	 * This corresponds to the respective fields in IA32_RTIT_CTL MSR.
	 */
	union {
		uint64_t addr_cfg;

		struct {
			uint32_t addr0_cfg:4;
			uint32_t addr1_cfg:4;
			uint32_t addr2_cfg:4;
			uint32_t addr3_cfg:4;
		} ctl;
	} config;

	/** The address ranges configuration.
	 *
	 * This corresponds to the IA32_RTIT_ADDRn_A/B MSRs.
	 */
	uint64_t addr0_a;
	uint64_t addr0_b;
	uint64_t addr1_a;
	uint64_t addr1_b;
	uint64_t addr2_a;
	uint64_t addr2_b;
	uint64_t addr3_a;
	uint64_t addr3_b;

	/* Reserve some space. */
	uint64_t reserved[8];
};

/** An unknown packet. */
struct pt_packet_unknown;

/** An Intel PT decoder configuration.
 */
struct pt_config {
	/** The size of the config structure in bytes. */
	size_t size;

	/** The trace buffer begin address. */
	uint8_t *begin;

	/** The trace buffer end address. */
	uint8_t *end;

	/** An optional callback for handling unknown packets.
	 *
	 * If \@callback is not NULL, it is called for any unknown opcode.
	 */
	struct {
		/** The callback function.
		 *
		 * It shall decode the packet at \@pos into \@unknown.
		 * It shall return the number of bytes read upon success.
		 * It shall return a negative pt_error_code otherwise.
		 * The below context is passed as \@context.
		 */
		int (*callback)(struct pt_packet_unknown *unknown,
				const struct pt_config *config,
				const uint8_t *pos, void *context);

		/** The user-defined context for this configuration. */
		void *context;
	} decode;

	/** The cpu on which Intel PT has been recorded. */
	struct pt_cpu cpu;

	/** The errata to apply when encoding or decoding Intel PT. */
	struct pt_errata errata;

	/* The CTC frequency.
	 *
	 * This is only required if MTC packets have been enabled in
	 * IA32_RTIT_CTRL.MTCEn.
	 */
	uint32_t cpuid_0x15_eax, cpuid_0x15_ebx;

	/* The MTC frequency as defined in IA32_RTIT_CTL.MTCFreq.
	 *
	 * This is only required if MTC packets have been enabled in
	 * IA32_RTIT_CTRL.MTCEn.
	 */
	uint8_t mtc_freq;

	/* The nominal frequency as defined in MSR_PLATFORM_INFO[15:8].
	 *
	 * This is only required if CYC packets have been enabled in
	 * IA32_RTIT_CTRL.CYCEn.
	 *
	 * If zero, timing calibration will only be able to use MTC and CYC
	 * packets.
	 *
	 * If not zero, timing calibration will also be able to use CBR
	 * packets.
	 */
	uint8_t nom_freq;

	/** A collection of decoder-specific flags. */
	struct pt_conf_flags flags;

	/** The address filter configuration. */
	struct pt_conf_addr_filter addr_filter;
};


/** Zero-initialize an Intel PT configuration. */
static inline void pt_config_init(struct pt_config *config)
{
	memset(config, 0, sizeof(*config));

	config->size = sizeof(*config);
}

/** Determine errata for a given cpu.
 *
 * Updates \@errata based on \@cpu.
 *
 * Returns 0 on success, a negative error code otherwise.
 * Returns -pte_invalid if \@errata or \@cpu is NULL.
 * Returns -pte_bad_cpu if \@cpu is not known.
 */
extern pt_export int pt_cpu_errata(struct pt_errata *errata,
				   const struct pt_cpu *cpu);



/* Packet encoder / decoder. */



/** Intel PT packet types. */
enum pt_packet_type {
	/* An invalid packet. */
	ppt_invalid,

	/* A packet decodable by the optional decoder callback. */
	ppt_unknown,

	/* Actual packets supported by this library. */
	ppt_pad,
	ppt_psb,
	ppt_psbend,
	ppt_fup,
	ppt_tip,
	ppt_tip_pge,
	ppt_tip_pgd,
	ppt_tnt_8,
	ppt_tnt_64,
	ppt_mode,
	ppt_pip,
	ppt_vmcs,
	ppt_cbr,
	ppt_tsc,
	ppt_tma,
	ppt_mtc,
	ppt_cyc,
	ppt_stop,
	ppt_ovf,
	ppt_mnt,
	ppt_exstop,
	ppt_mwait,
	ppt_pwre,
	ppt_pwrx,
	ppt_ptw
};

/** The IP compression. */
enum pt_ip_compression {
	/* The bits encode the payload size and the encoding scheme.
	 *
	 * No payload.  The IP has been suppressed.
	 */
	pt_ipc_suppressed	= 0x0,

	/* Payload: 16 bits.  Update last IP. */
	pt_ipc_update_16	= 0x01,

	/* Payload: 32 bits.  Update last IP. */
	pt_ipc_update_32	= 0x02,

	/* Payload: 48 bits.  Sign extend to full address. */
	pt_ipc_sext_48		= 0x03,

	/* Payload: 48 bits.  Update last IP. */
	pt_ipc_update_48	= 0x04,

	/* Payload: 64 bits.  Full address. */
	pt_ipc_full		= 0x06
};

/** An execution mode. */
enum pt_exec_mode {
	ptem_unknown,
	ptem_16bit,
	ptem_32bit,
	ptem_64bit
};

/** Mode packet leaves. */
enum pt_mode_leaf {
	pt_mol_exec		= 0x00,
	pt_mol_tsx		= 0x20
};

/** A TNT-8 or TNT-64 packet. */
struct pt_packet_tnt {
	/** TNT payload bit size. */
	uint8_t bit_size;

	/** TNT payload excluding stop bit. */
	uint64_t payload;
};

/** A packet with IP payload. */
struct pt_packet_ip {
	/** IP compression. */
	enum pt_ip_compression ipc;

	/** Zero-extended payload ip. */
	uint64_t ip;
};

/** A mode.exec packet. */
struct pt_packet_mode_exec {
	/** The mode.exec csl bit. */
	uint32_t csl:1;

	/** The mode.exec csd bit. */
	uint32_t csd:1;
};

static inline enum pt_exec_mode
pt_get_exec_mode(const struct pt_packet_mode_exec *packet)
{
	if (packet->csl)
		return packet->csd ? ptem_unknown : ptem_64bit;
	else
		return packet->csd ? ptem_32bit : ptem_16bit;
}

static inline struct pt_packet_mode_exec
pt_set_exec_mode(enum pt_exec_mode mode)
{
	struct pt_packet_mode_exec packet;

	switch (mode) {
	default:
		packet.csl = 1;
		packet.csd = 1;
		break;

	case ptem_64bit:
		packet.csl = 1;
		packet.csd = 0;
		break;

	case ptem_32bit:
		packet.csl = 0;
		packet.csd = 1;
		break;

	case ptem_16bit:
		packet.csl = 0;
		packet.csd = 0;
		break;
	}

	return packet;
}

/** A mode.tsx packet. */
struct pt_packet_mode_tsx {
	/** The mode.tsx intx bit. */
	uint32_t intx:1;

	/** The mode.tsx abrt bit. */
	uint32_t abrt:1;
};

/** A mode packet. */
struct pt_packet_mode {
	/** Mode leaf. */
	enum pt_mode_leaf leaf;

	/** Mode bits. */
	union {
		/** Packet: mode.exec. */
		struct pt_packet_mode_exec exec;

		/** Packet: mode.tsx. */
		struct pt_packet_mode_tsx tsx;
	} bits;
};

/** A PIP packet. */
struct pt_packet_pip {
	/** The CR3 value. */
	uint64_t cr3;

	/** The non-root bit. */
	uint32_t nr:1;
};

/** A TSC packet. */
struct pt_packet_tsc {
	/** The TSC value. */
	uint64_t tsc;
};

/** A CBR packet. */
struct pt_packet_cbr {
	/** The core/bus cycle ratio. */
	uint8_t ratio;
};

/** A TMA packet. */
struct pt_packet_tma {
	/** The crystal clock tick counter value. */
	uint16_t ctc;

	/** The fast counter value. */
	uint16_t fc;
};

/** A MTC packet. */
struct pt_packet_mtc {
	/** The crystal clock tick counter value. */
	uint8_t ctc;
};

/** A CYC packet. */
struct pt_packet_cyc {
	/** The cycle counter value. */
	uint64_t value;
};

/** A VMCS packet. */
struct pt_packet_vmcs {
       /* The VMCS Base Address (i.e. the shifted payload). */
	uint64_t base;
};

/** A MNT packet. */
struct pt_packet_mnt {
	/** The raw payload. */
	uint64_t payload;
};

/** A EXSTOP packet. */
struct pt_packet_exstop {
	/** A flag specifying the binding of the packet:
	 *
	 *   set:    binds to the next FUP.
	 *   clear:  standalone.
	 */
	uint32_t ip:1;
};

/** A MWAIT packet. */
struct pt_packet_mwait {
	/** The MWAIT hints (EAX). */
	uint32_t hints;

	/** The MWAIT extensions (ECX). */
	uint32_t ext;
};

/** A PWRE packet. */
struct pt_packet_pwre {
	/** The resolved thread C-state. */
	uint8_t state;

	/** The resolved thread sub C-state. */
	uint8_t sub_state;

	/** A flag indicating whether the C-state entry was initiated by h/w. */
	uint32_t hw:1;
};

/** A PWRX packet. */
struct pt_packet_pwrx {
	/** The core C-state at the time of the wake. */
	uint8_t last;

	/** The deepest core C-state achieved during sleep. */
	uint8_t deepest;

	/** The wake reason:
	 *
	 * - due to external interrupt received.
	 */
	uint32_t interrupt:1;

	/** - due to store to monitored address. */
	uint32_t store:1;

	/** - due to h/w autonomous condition such as HDC. */
	uint32_t autonomous:1;
};

/** A PTW packet. */
struct pt_packet_ptw {
	/** The raw payload. */
	uint64_t payload;

	/** The payload size as encoded in the packet. */
	uint8_t plc;

	/** A flag saying whether a FUP is following PTW that provides
	 * the IP of the corresponding PTWRITE instruction.
	 */
	uint32_t ip:1;
};

static inline int pt_ptw_size(uint8_t plc)
{
	switch (plc) {
	case 0:
		return 4;

	case 1:
		return 8;

	case 2:
	case 3:
		return -pte_bad_packet;
	}

	return -pte_internal;
}

/** An unknown packet decodable by the optional decoder callback. */
struct pt_packet_unknown {
	/** Pointer to the raw packet bytes. */
	const uint8_t *packet;

	/** Optional pointer to a user-defined structure. */
	void *priv;
};

/** An Intel PT packet. */
struct pt_packet {
	/** The type of the packet.
	 *
	 * This also determines the \@payload field.
	 */
	enum pt_packet_type type;

	/** The size of the packet including opcode and payload. */
	uint8_t size;

	/** Packet specific data. */
	union {
		/** Packets: pad, ovf, psb, psbend, stop - no payload. */

		/** Packet: tnt-8, tnt-64. */
		struct pt_packet_tnt tnt;

		/** Packet: tip, fup, tip.pge, tip.pgd. */
		struct pt_packet_ip ip;

		/** Packet: mode. */
		struct pt_packet_mode mode;

		/** Packet: pip. */
		struct pt_packet_pip pip;

		/** Packet: tsc. */
		struct pt_packet_tsc tsc;

		/** Packet: cbr. */
		struct pt_packet_cbr cbr;

		/** Packet: tma. */
		struct pt_packet_tma tma;

		/** Packet: mtc. */
		struct pt_packet_mtc mtc;

		/** Packet: cyc. */
		struct pt_packet_cyc cyc;

		/** Packet: vmcs. */
		struct pt_packet_vmcs vmcs;

		/** Packet: mnt. */
		struct pt_packet_mnt mnt;

		/** Packet: exstop. */
		struct pt_packet_exstop exstop;

		/** Packet: mwait. */
		struct pt_packet_mwait mwait;

		/** Packet: pwre. */
		struct pt_packet_pwre pwre;

		/** Packet: pwrx. */
		struct pt_packet_pwrx pwrx;

		/** Packet: ptw. */
		struct pt_packet_ptw ptw;

		/** Packet: unknown. */
		struct pt_packet_unknown unknown;
	} payload;
};



/* Packet encoder. */



/** Allocate an Intel PT packet encoder.
 *
 * The encoder will work on the buffer defined in \@config, it shall contain
 * raw trace data and remain valid for the lifetime of the encoder.
 *
 * The encoder starts at the beginning of the trace buffer.
 */
extern pt_export struct pt_encoder *
pt_alloc_encoder(const struct pt_config *config);

/** Free an Intel PT packet encoder.
 *
 * The \@encoder must not be used after a successful return.
 */
extern pt_export void pt_free_encoder(struct pt_encoder *encoder);

/** Hard set synchronization point of an Intel PT packet encoder.
 *
 * Synchronize \@encoder to \@offset within the trace buffer.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_eos if the given offset is behind the end of the trace buffer.
 * Returns -pte_invalid if \@encoder is NULL.
 */
extern pt_export int pt_enc_sync_set(struct pt_encoder *encoder,
				     uint64_t offset);

/** Get the current packet encoder position.
 *
 * Fills the current \@encoder position into \@offset.
 *
 * This is useful for reporting errors.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@encoder or \@offset is NULL.
 */
extern pt_export int pt_enc_get_offset(const struct pt_encoder *encoder,
				       uint64_t *offset);

/* Return a pointer to \@encoder's configuration.
 *
 * Returns a non-null pointer on success, NULL if \@encoder is NULL.
 */
extern pt_export const struct pt_config *
pt_enc_get_config(const struct pt_encoder *encoder);

/** Encode an Intel PT packet.
 *
 * Writes \@packet at \@encoder's current position in the Intel PT buffer and
 * advances the \@encoder beyond the written packet.
 *
 * The \@packet.size field is ignored.
 *
 * In case of errors, the \@encoder is not advanced and nothing is written
 * into the Intel PT buffer.
 *
 * Returns the number of bytes written on success, a negative error code
 * otherwise.
 *
 * Returns -pte_bad_opc if \@packet.type is not known.
 * Returns -pte_bad_packet if \@packet's payload is invalid.
 * Returns -pte_eos if \@encoder reached the end of the Intel PT buffer.
 * Returns -pte_invalid if \@encoder or \@packet is NULL.
 */
extern pt_export int pt_enc_next(struct pt_encoder *encoder,
				 const struct pt_packet *packet);



/* Packet decoder. */



/** Allocate an Intel PT packet decoder.
 *
 * The decoder will work on the buffer defined in \@config, it shall contain
 * raw trace data and remain valid for the lifetime of the decoder.
 *
 * The decoder needs to be synchronized before it can be used.
 */
extern pt_export struct pt_packet_decoder *
pt_pkt_alloc_decoder(const struct pt_config *config);

/** Free an Intel PT packet decoder.
 *
 * The \@decoder must not be used after a successful return.
 */
extern pt_export void pt_pkt_free_decoder(struct pt_packet_decoder *decoder);

/** Synchronize an Intel PT packet decoder.
 *
 * Search for the next synchronization point in forward or backward direction.
 *
 * If \@decoder has not been synchronized, yet, the search is started at the
 * beginning of the trace buffer in case of forward synchronization and at the
 * end of the trace buffer in case of backward synchronization.
 *
 * Returns zero or a positive value on success, a negative error code otherwise.
 *
 * Returns -pte_eos if no further synchronization point is found.
 * Returns -pte_invalid if \@decoder is NULL.
 */
extern pt_export int pt_pkt_sync_forward(struct pt_packet_decoder *decoder);
extern pt_export int pt_pkt_sync_backward(struct pt_packet_decoder *decoder);

/** Hard set synchronization point of an Intel PT decoder.
 *
 * Synchronize \@decoder to \@offset within the trace buffer.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_eos if the given offset is behind the end of the trace buffer.
 * Returns -pte_invalid if \@decoder is NULL.
 */
extern pt_export int pt_pkt_sync_set(struct pt_packet_decoder *decoder,
				     uint64_t offset);

/** Get the current decoder position.
 *
 * Fills the current \@decoder position into \@offset.
 *
 * This is useful for reporting errors.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@offset is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_pkt_get_offset(const struct pt_packet_decoder *decoder,
				       uint64_t *offset);

/** Get the position of the last synchronization point.
 *
 * Fills the last synchronization position into \@offset.
 *
 * This is useful when splitting a trace stream for parallel decoding.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@offset is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int
pt_pkt_get_sync_offset(const struct pt_packet_decoder *decoder,
		       uint64_t *offset);

/* Return a pointer to \@decoder's configuration.
 *
 * Returns a non-null pointer on success, NULL if \@decoder is NULL.
 */
extern pt_export const struct pt_config *
pt_pkt_get_config(const struct pt_packet_decoder *decoder);

/** Decode the next packet and advance the decoder.
 *
 * Decodes the packet at \@decoder's current position into \@packet and
 * adjusts the \@decoder's position by the number of bytes the packet had
 * consumed.
 *
 * The \@size argument must be set to sizeof(struct pt_packet).
 *
 * Returns the number of bytes consumed on success, a negative error code
 * otherwise.
 *
 * Returns -pte_bad_opc if the packet is unknown.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_eos if \@decoder reached the end of the Intel PT buffer.
 * Returns -pte_invalid if \@decoder or \@packet is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_pkt_next(struct pt_packet_decoder *decoder,
				 struct pt_packet *packet, size_t size);



/* Query decoder. */



/** Decoder status flags. */
enum pt_status_flag {
	/** There is an event pending. */
	pts_event_pending	= 1 << 0,

	/** The address has been suppressed. */
	pts_ip_suppressed	= 1 << 1,

	/** There is no more trace data available. */
	pts_eos			= 1 << 2
};

/** Event types. */
enum pt_event_type {
	/* Tracing has been enabled/disabled. */
	ptev_enabled,
	ptev_disabled,

	/* Tracing has been disabled asynchronously. */
	ptev_async_disabled,

	/* An asynchronous branch, e.g. interrupt. */
	ptev_async_branch,

	/* A synchronous paging event. */
	ptev_paging,

	/* An asynchronous paging event. */
	ptev_async_paging,

	/* Trace overflow. */
	ptev_overflow,

	/* An execution mode change. */
	ptev_exec_mode,

	/* A transactional execution state change. */
	ptev_tsx,

	/* Trace Stop. */
	ptev_stop,

	/* A synchronous vmcs event. */
	ptev_vmcs,

	/* An asynchronous vmcs event. */
	ptev_async_vmcs,

	/* Execution has stopped. */
	ptev_exstop,

	/* An MWAIT operation completed. */
	ptev_mwait,

	/* A power state was entered. */
	ptev_pwre,

	/* A power state was exited. */
	ptev_pwrx,

	/* A PTWRITE event. */
	ptev_ptwrite,

	/* A timing event. */
	ptev_tick,

	/* A core:bus ratio event. */
	ptev_cbr,

	/* A maintenance event. */
	ptev_mnt
};

/** An event. */
struct pt_event {
	/** The type of the event. */
	enum pt_event_type type;

	/** A flag indicating that the event IP has been suppressed. */
	uint32_t ip_suppressed:1;

	/** A flag indicating that the event is for status update. */
	uint32_t status_update:1;

	/** A flag indicating that the event has timing information. */
	uint32_t has_tsc:1;

	/** The time stamp count of the event.
	 *
	 * This field is only valid if \@has_tsc is set.
	 */
	uint64_t tsc;

	/** The number of lost mtc and cyc packets.
	 *
	 * This gives an idea about the quality of the \@tsc.  The more packets
	 * were dropped, the less precise timing is.
	 */
	uint32_t lost_mtc;
	uint32_t lost_cyc;

	/* Reserved space for future extensions. */
	uint64_t reserved[2];

	/** Event specific data. */
	union {
		/** Event: enabled. */
		struct {
			/** The address at which tracing resumes. */
			uint64_t ip;

			/** A flag indicating that tracing resumes from the IP
			 * at which tracing had been disabled before.
			 */
			uint32_t resumed:1;
		} enabled;

		/** Event: disabled. */
		struct {
			/** The destination of the first branch inside a
			 * filtered area.
			 *
			 * This field is not valid if \@ip_suppressed is set.
			 */
			uint64_t ip;

			/* The exact source ip needs to be determined using
			 * disassembly and the filter configuration.
			 */
		} disabled;

		/** Event: async disabled. */
		struct {
			/** The source address of the asynchronous branch that
			 * disabled tracing.
			 */
			uint64_t at;

			/** The destination of the first branch inside a
			 * filtered area.
			 *
			 * This field is not valid if \@ip_suppressed is set.
			 */
			uint64_t ip;
		} async_disabled;

		/** Event: async branch. */
		struct {
			/** The branch source address. */
			uint64_t from;

			/** The branch destination address.
			 *
			 * This field is not valid if \@ip_suppressed is set.
			 */
			uint64_t to;
		} async_branch;

		/** Event: paging. */
		struct {
			/** The updated CR3 value.
			 *
			 * The lower 5 bit have been zeroed out.
			 * The upper bits have been zeroed out depending on the
			 * maximum possible address.
			 */
			uint64_t cr3;

			/** A flag indicating whether the cpu is operating in
			 * vmx non-root (guest) mode.
			 */
			uint32_t non_root:1;

			/* The address at which the event is effective is
			 * obvious from the disassembly.
			 */
		} paging;

		/** Event: async paging. */
		struct {
			/** The updated CR3 value.
			 *
			 * The lower 5 bit have been zeroed out.
			 * The upper bits have been zeroed out depending on the
			 * maximum possible address.
			 */
			uint64_t cr3;

			/** A flag indicating whether the cpu is operating in
			 * vmx non-root (guest) mode.
			 */
			uint32_t non_root:1;

			/** The address at which the event is effective. */
			uint64_t ip;
		} async_paging;

		/** Event: overflow. */
		struct {
			/** The address at which tracing resumes after overflow.
			 *
			 * This field is not valid, if ip_suppressed is set.
			 * In this case, the overflow resolved while tracing
			 * was disabled.
			 */
			uint64_t ip;
		} overflow;

		/** Event: exec mode. */
		struct {
			/** The execution mode. */
			enum pt_exec_mode mode;

			/** The address at which the event is effective. */
			uint64_t ip;
		} exec_mode;

		/** Event: tsx. */
		struct {
			/** The address at which the event is effective.
			 *
			 * This field is not valid if \@ip_suppressed is set.
			 */
			uint64_t ip;

			/** A flag indicating speculative execution mode. */
			uint32_t speculative:1;

			/** A flag indicating speculative execution aborts. */
			uint32_t aborted:1;
		} tsx;

		/** Event: vmcs. */
		struct {
			/** The VMCS base address.
			 *
			 * The address is zero-extended with the lower 12 bits
			 * all zero.
			 */
			uint64_t base;

			/* The new VMCS base address should be stored and
			 * applied on subsequent VM entries.
			 */
		} vmcs;

		/** Event: async vmcs. */
		struct {
			/** The VMCS base address.
			 *
			 * The address is zero-extended with the lower 12 bits
			 * all zero.
			 */
			uint64_t base;

			/** The address at which the event is effective. */
			uint64_t ip;

			/* An async paging event that binds to the same IP
			 * will always succeed this async vmcs event.
			 */
		} async_vmcs;

		/** Event: execution stopped. */
		struct {
			/** The address at which execution has stopped.  This is
			 * the last instruction that did not complete.
			 *
			 * This field is not valid, if \@ip_suppressed is set.
			 */
			uint64_t ip;
		} exstop;

		/** Event: mwait. */
		struct {
			/** The address of the instruction causing the mwait.
			 *
			 * This field is not valid, if \@ip_suppressed is set.
			 */
			uint64_t ip;

			/** The mwait hints (eax).
			 *
			 * Reserved bits are undefined.
			 */
			uint32_t hints;

			/** The mwait extensions (ecx).
			 *
			 * Reserved bits are undefined.
			 */
			uint32_t ext;
		} mwait;

		/** Event: power state entry. */
		struct {
			/** The resolved thread C-state. */
			uint8_t state;

			/** The resolved thread sub C-state. */
			uint8_t sub_state;

			/** A flag indicating whether the C-state entry was
			 * initiated by h/w.
			 */
			uint32_t hw:1;
		} pwre;

		/** Event: power state exit. */
		struct {
			/** The core C-state at the time of the wake. */
			uint8_t last;

			/** The deepest core C-state achieved during sleep. */
			uint8_t deepest;

			/** The wake reason:
			 *
			 * - due to external interrupt received.
			 */
			uint32_t interrupt:1;

			/** - due to store to monitored address. */
			uint32_t store:1;

			/** - due to h/w autonomous condition such as HDC. */
			uint32_t autonomous:1;
		} pwrx;

		/** Event: ptwrite. */
		struct {
			/** The address of the ptwrite instruction.
			 *
			 * This field is not valid, if \@ip_suppressed is set.
			 *
			 * In this case, the address is obvious from the
			 * disassembly.
			 */
			uint64_t ip;

			/** The size of the below \@payload in bytes. */
			uint8_t size;

			/** The ptwrite payload. */
			uint64_t payload;
		} ptwrite;

		/** Event: tick. */
		struct {
			/** The instruction address near which the tick occured.
			 *
			 * A timestamp can sometimes be attributed directly to
			 * an instruction (e.g. to an indirect branch that
			 * receives CYC + TIP) and sometimes not (e.g. MTC).
			 *
			 * This field is not valid, if \@ip_suppressed is set.
			 */
			uint64_t ip;
		} tick;

		/** Event: cbr. */
		struct {
			/** The core:bus ratio. */
			uint16_t ratio;
		} cbr;

		/** Event: mnt. */
		struct {
			/** The raw payload. */
			uint64_t payload;
		} mnt;
	} variant;
};


/** Allocate an Intel PT query decoder.
 *
 * The decoder will work on the buffer defined in \@config, it shall contain
 * raw trace data and remain valid for the lifetime of the decoder.
 *
 * The decoder needs to be synchronized before it can be used.
 */
extern pt_export struct pt_query_decoder *
pt_qry_alloc_decoder(const struct pt_config *config);

/** Free an Intel PT query decoder.
 *
 * The \@decoder must not be used after a successful return.
 */
extern pt_export void pt_qry_free_decoder(struct pt_query_decoder *decoder);

/** Synchronize an Intel PT query decoder.
 *
 * Search for the next synchronization point in forward or backward direction.
 *
 * If \@decoder has not been synchronized, yet, the search is started at the
 * beginning of the trace buffer in case of forward synchronization and at the
 * end of the trace buffer in case of backward synchronization.
 *
 * If \@ip is not NULL, set it to last ip.
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 *
 * Returns -pte_bad_opc if an unknown packet is encountered.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_eos if no further synchronization point is found.
 * Returns -pte_invalid if \@decoder is NULL.
 */
extern pt_export int pt_qry_sync_forward(struct pt_query_decoder *decoder,
					 uint64_t *ip);
extern pt_export int pt_qry_sync_backward(struct pt_query_decoder *decoder,
					 uint64_t *ip);

/** Manually synchronize an Intel PT query decoder.
 *
 * Synchronize \@decoder on the syncpoint at \@offset.  There must be a PSB
 * packet at \@offset.
 *
 * If \@ip is not NULL, set it to last ip.
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 *
 * Returns -pte_bad_opc if an unknown packet is encountered.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_eos if \@offset lies outside of \@decoder's trace buffer.
 * Returns -pte_eos if \@decoder reaches the end of its trace buffer.
 * Returns -pte_invalid if \@decoder is NULL.
 * Returns -pte_nosync if there is no syncpoint at \@offset.
 */
extern pt_export int pt_qry_sync_set(struct pt_query_decoder *decoder,
				     uint64_t *ip, uint64_t offset);

/** Get the current decoder position.
 *
 * Fills the current \@decoder position into \@offset.
 *
 * This is useful for reporting errors.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@offset is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_qry_get_offset(const struct pt_query_decoder *decoder,
				       uint64_t *offset);

/** Get the position of the last synchronization point.
 *
 * Fills the last synchronization position into \@offset.
 *
 * This is useful for splitting a trace stream for parallel decoding.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@offset is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int
pt_qry_get_sync_offset(const struct pt_query_decoder *decoder,
		       uint64_t *offset);

/* Return a pointer to \@decoder's configuration.
 *
 * Returns a non-null pointer on success, NULL if \@decoder is NULL.
 */
extern pt_export const struct pt_config *
pt_qry_get_config(const struct pt_query_decoder *decoder);

/** Query whether the next unconditional branch has been taken.
 *
 * On success, provides 1 (taken) or 0 (not taken) in \@taken for the next
 * conditional branch and updates \@decoder.
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 *
 * Returns -pte_bad_opc if an unknown packet is encountered.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_bad_query if no conditional branch is found.
 * Returns -pte_eos if decoding reached the end of the Intel PT buffer.
 * Returns -pte_invalid if \@decoder or \@taken is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_qry_cond_branch(struct pt_query_decoder *decoder,
					int *taken);

/** Get the next indirect branch destination.
 *
 * On success, provides the linear destination address of the next indirect
 * branch in \@ip and updates \@decoder.
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 *
 * Returns -pte_bad_opc if an unknown packet is encountered.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_bad_query if no indirect branch is found.
 * Returns -pte_eos if decoding reached the end of the Intel PT buffer.
 * Returns -pte_invalid if \@decoder or \@ip is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_qry_indirect_branch(struct pt_query_decoder *decoder,
					    uint64_t *ip);

/** Query the next pending event.
 *
 * On success, provides the next event \@event and updates \@decoder.
 *
 * The \@size argument must be set to sizeof(struct pt_event).
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 *
 * Returns -pte_bad_opc if an unknown packet is encountered.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_bad_query if no event is found.
 * Returns -pte_eos if decoding reached the end of the Intel PT buffer.
 * Returns -pte_invalid if \@decoder or \@event is NULL.
 * Returns -pte_invalid if \@size is too small.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_qry_event(struct pt_query_decoder *decoder,
				  struct pt_event *event, size_t size);

/** Query the current time.
 *
 * On success, provides the time at the last query in \@time.
 *
 * The time is similar to what a rdtsc instruction would return.  Depending
 * on the configuration, the time may not be fully accurate.  If TSC is not
 * enabled, the time is relative to the last synchronization and can't be used
 * to correlate with other TSC-based time sources.  In this case, -pte_no_time
 * is returned and the relative time is provided in \@time.
 *
 * Some timing-related packets may need to be dropped (mostly due to missing
 * calibration or incomplete configuration).  To get an idea about the quality
 * of the estimated time, we record the number of dropped MTC and CYC packets.
 *
 * If \@lost_mtc is not NULL, set it to the number of lost MTC packets.
 * If \@lost_cyc is not NULL, set it to the number of lost CYC packets.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@time is NULL.
 * Returns -pte_no_time if there has not been a TSC packet.
 */
extern pt_export int pt_qry_time(struct pt_query_decoder *decoder,
				 uint64_t *time, uint32_t *lost_mtc,
				 uint32_t *lost_cyc);

/** Return the current core bus ratio.
 *
 * On success, provides the current core:bus ratio in \@cbr.  The ratio is
 * defined as core cycles per bus clock cycle.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@cbr is NULL.
 * Returns -pte_no_cbr if there has not been a CBR packet.
 */
extern pt_export int pt_qry_core_bus_ratio(struct pt_query_decoder *decoder,
					   uint32_t *cbr);



/* Traced image. */



/** An Intel PT address space identifier.
 *
 * This identifies a particular address space when adding file sections or
 * when reading memory.
 */
struct pt_asid {
	/** The size of this object - set to sizeof(struct pt_asid). */
	size_t size;

	/** The CR3 value. */
	uint64_t cr3;

	/** The VMCS Base address. */
	uint64_t vmcs;
};

/** An unknown CR3 value to be used for pt_asid objects. */
static const uint64_t pt_asid_no_cr3 = 0xffffffffffffffffull;

/** An unknown VMCS Base value to be used for pt_asid objects. */
static const uint64_t pt_asid_no_vmcs = 0xffffffffffffffffull;

/** Initialize an address space identifier. */
static inline void pt_asid_init(struct pt_asid *asid)
{
	asid->size = sizeof(*asid);
	asid->cr3 = pt_asid_no_cr3;
	asid->vmcs = pt_asid_no_vmcs;
}


/** A cache of traced image sections. */
struct pt_image_section_cache;

/** Allocate a traced memory image section cache.
 *
 * An optional \@name may be given to the cache.  The name string is copied.
 *
 * Returns a new traced memory image section cache on success, NULL otherwise.
 */
extern pt_export struct pt_image_section_cache *
pt_iscache_alloc(const char *name);

/** Free a traced memory image section cache.
 *
 * The \@iscache must have been allocated with pt_iscache_alloc().
 * The \@iscache must not be used after a successful return.
 */
extern pt_export void pt_iscache_free(struct pt_image_section_cache *iscache);

/** Set the image section cache limit.
 *
 * Set the limit for a section cache in bytes.  A non-zero limit will keep the
 * least recently used sections mapped until the limit is reached.  A limit of
 * zero disables caching.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 * Returns -pte_invalid if \@iscache is NULL.
 */
extern pt_export int
pt_iscache_set_limit(struct pt_image_section_cache *iscache, uint64_t limit);

/** Get the image section cache name.
 *
 * Returns a pointer to \@iscache's name or NULL if there is no name.
 */
extern pt_export const char *
pt_iscache_name(const struct pt_image_section_cache *iscache);

/** Add a new file section to the traced memory image section cache.
 *
 * Adds a new section consisting of \@size bytes starting at \@offset in
 * \@filename loaded at the virtual address \@vaddr if \@iscache does not
 * already contain such a section.
 *
 * Returns an image section identifier (isid) uniquely identifying that section
 * in \@iscache.
 *
 * The section is silently truncated to match the size of \@filename.
 *
 * Returns a positive isid on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@iscache or \@filename is NULL.
 * Returns -pte_invalid if \@offset is too big.
 */
extern pt_export int pt_iscache_add_file(struct pt_image_section_cache *iscache,
					 const char *filename, uint64_t offset,
					 uint64_t size, uint64_t vaddr);

/** Read memory from a cached file section
 *
 * Reads \@size bytes of memory starting at virtual address \@vaddr in the
 * section identified by \@isid in \@iscache into \@buffer.
 *
 * The caller is responsible for allocating a \@buffer of at least \@size bytes.
 *
 * The read request may be truncated if it crosses section boundaries or if
 * \@size is getting too big.  We support reading at least 4Kbyte in one chunk
 * unless the read would cross a section boundary.
 *
 * Returns the number of bytes read on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@iscache or \@buffer is NULL.
 * Returns -pte_invalid if \@size is zero.
 * Returns -pte_nomap if \@vaddr is not contained in section \@isid.
 * Returns -pte_bad_image if \@iscache does not contain \@isid.
 */
extern pt_export int pt_iscache_read(struct pt_image_section_cache *iscache,
				     uint8_t *buffer, uint64_t size, int isid,
				     uint64_t vaddr);

/** The traced memory image. */
struct pt_image;


/** Allocate a traced memory image.
 *
 * An optional \@name may be given to the image.  The name string is copied.
 *
 * Returns a new traced memory image on success, NULL otherwise.
 */
extern pt_export struct pt_image *pt_image_alloc(const char *name);

/** Free a traced memory image.
 *
 * The \@image must have been allocated with pt_image_alloc().
 * The \@image must not be used after a successful return.
 */
extern pt_export void pt_image_free(struct pt_image *image);

/** Get the image name.
 *
 * Returns a pointer to \@image's name or NULL if there is no name.
 */
extern pt_export const char *pt_image_name(const struct pt_image *image);

/** Add a new file section to the traced memory image.
 *
 * Adds \@size bytes starting at \@offset in \@filename. The section is
 * loaded at the virtual address \@vaddr in the address space \@asid.
 *
 * The \@asid may be NULL or (partially) invalid.  In that case only the valid
 * fields are considered when comparing with other address-spaces.  Use this
 * when tracing a single process or when adding sections to all processes.
 *
 * The section is silently truncated to match the size of \@filename.
 *
 * Existing sections that would overlap with the new section will be shrunk
 * or split.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@image or \@filename is NULL.
 * Returns -pte_invalid if \@offset is too big.
 */
extern pt_export int pt_image_add_file(struct pt_image *image,
				       const char *filename, uint64_t offset,
				       uint64_t size,
				       const struct pt_asid *asid,
				       uint64_t vaddr);

/** Add a section from an image section cache.
 *
 * Add the section from \@iscache identified by \@isid in address space \@asid.
 *
 * Existing sections that would overlap with the new section will be shrunk
 * or split.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_invalid if \@image or \@iscache is NULL.
 * Returns -pte_bad_image if \@iscache does not contain \@isid.
 */
extern pt_export int pt_image_add_cached(struct pt_image *image,
					 struct pt_image_section_cache *iscache,
					 int isid, const struct pt_asid *asid);

/** Copy an image.
 *
 * Adds all sections from \@src to \@image.  Sections that could not be added
 * will be ignored.
 *
 * Returns the number of ignored sections on success, a negative error code
 * otherwise.
 *
 * Returns -pte_invalid if \@image or \@src is NULL.
 */
extern pt_export int pt_image_copy(struct pt_image *image,
				   const struct pt_image *src);

/** Remove all sections loaded from a file.
 *
 * Removes all sections loaded from \@filename from the address space \@asid.
 * Specify the same \@asid that was used for adding sections from \@filename.
 *
 * Returns the number of removed sections on success, a negative error code
 * otherwise.
 *
 * Returns -pte_invalid if \@image or \@filename is NULL.
 */
extern pt_export int pt_image_remove_by_filename(struct pt_image *image,
						 const char *filename,
						 const struct pt_asid *asid);

/** Remove all sections loaded into an address space.
 *
 * Removes all sections loaded into \@asid.  Specify the same \@asid that was
 * used for adding sections.
 *
 * Returns the number of removed sections on success, a negative error code
 * otherwise.
 *
 * Returns -pte_invalid if \@image is NULL.
 */
extern pt_export int pt_image_remove_by_asid(struct pt_image *image,
					     const struct pt_asid *asid);

/** A read memory callback function.
 *
 * It shall read \@size bytes of memory from address space \@asid starting
 * at \@ip into \@buffer.
 *
 * It shall return the number of bytes read on success.
 * It shall return a negative pt_error_code otherwise.
 */
typedef int (read_memory_callback_t)(uint8_t *buffer, size_t size,
				     const struct pt_asid *asid,
				     uint64_t ip, void *context);

/** Set the memory callback for the traced memory image.
 *
 * Sets \@callback for reading memory.  The callback is used for addresses
 * that are not found in file sections.  The \@context argument is passed
 * to \@callback on each use.
 *
 * There can only be one callback at any time.  A subsequent call will replace
 * the previous callback.  If \@callback is NULL, the callback is removed.
 *
 * Returns -pte_invalid if \@image is NULL.
 */
extern pt_export int pt_image_set_callback(struct pt_image *image,
					   read_memory_callback_t *callback,
					   void *context);



/* Instruction flow decoder. */



/** The instruction class.
 *
 * We provide only a very coarse classification suitable for reconstructing
 * the execution flow.
 */
enum pt_insn_class {
	/* The instruction could not be classified. */
	ptic_error,

	/* The instruction is something not listed below. */
	ptic_other,

	/* The instruction is a near (function) call. */
	ptic_call,

	/* The instruction is a near (function) return. */
	ptic_return,

	/* The instruction is a near unconditional jump. */
	ptic_jump,

	/* The instruction is a near conditional jump. */
	ptic_cond_jump,

	/* The instruction is a call-like far transfer.
	 * E.g. SYSCALL, SYSENTER, or FAR CALL.
	 */
	ptic_far_call,

	/* The instruction is a return-like far transfer.
	 * E.g. SYSRET, SYSEXIT, IRET, or FAR RET.
	 */
	ptic_far_return,

	/* The instruction is a jump-like far transfer.
	 * E.g. FAR JMP.
	 */
	ptic_far_jump,

	/* The instruction is a PTWRITE. */
	ptic_ptwrite
};

/** The maximal size of an instruction. */
enum {
	pt_max_insn_size	= 15
};

/** A single traced instruction. */
struct pt_insn {
	/** The virtual address in its process. */
	uint64_t ip;

	/** The image section identifier for the section containing this
	 * instruction.
	 *
	 * A value of zero means that the section did not have an identifier.
	 * The section was not added via an image section cache or the memory
	 * was read via the read memory callback.
	 */
	int isid;

	/** The execution mode. */
	enum pt_exec_mode mode;

	/** A coarse classification. */
	enum pt_insn_class iclass;

	/** The raw bytes. */
	uint8_t raw[pt_max_insn_size];

	/** The size in bytes. */
	uint8_t size;

	/** A collection of flags giving additional information:
	 *
	 * - the instruction was executed speculatively.
	 */
	uint32_t speculative:1;

	/** - this instruction is truncated in its image section.
	 *
	 *    It starts in the image section identified by \@isid and continues
	 *    in one or more other sections.
	 */
	uint32_t truncated:1;
};


/** Allocate an Intel PT instruction flow decoder.
 *
 * The decoder will work on the buffer defined in \@config, it shall contain
 * raw trace data and remain valid for the lifetime of the decoder.
 *
 * The decoder needs to be synchronized before it can be used.
 */
extern pt_export struct pt_insn_decoder *
pt_insn_alloc_decoder(const struct pt_config *config);

/** Free an Intel PT instruction flow decoder.
 *
 * This will destroy the decoder's default image.
 *
 * The \@decoder must not be used after a successful return.
 */
extern pt_export void pt_insn_free_decoder(struct pt_insn_decoder *decoder);

/** Synchronize an Intel PT instruction flow decoder.
 *
 * Search for the next synchronization point in forward or backward direction.
 *
 * If \@decoder has not been synchronized, yet, the search is started at the
 * beginning of the trace buffer in case of forward synchronization and at the
 * end of the trace buffer in case of backward synchronization.
 *
 * Returns zero or a positive value on success, a negative error code otherwise.
 *
 * Returns -pte_bad_opc if an unknown packet is encountered.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_eos if no further synchronization point is found.
 * Returns -pte_invalid if \@decoder is NULL.
 */
extern pt_export int pt_insn_sync_forward(struct pt_insn_decoder *decoder);
extern pt_export int pt_insn_sync_backward(struct pt_insn_decoder *decoder);

/** Manually synchronize an Intel PT instruction flow decoder.
 *
 * Synchronize \@decoder on the syncpoint at \@offset.  There must be a PSB
 * packet at \@offset.
 *
 * Returns zero or a positive value on success, a negative error code otherwise.
 *
 * Returns -pte_bad_opc if an unknown packet is encountered.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_eos if \@offset lies outside of \@decoder's trace buffer.
 * Returns -pte_eos if \@decoder reaches the end of its trace buffer.
 * Returns -pte_invalid if \@decoder is NULL.
 * Returns -pte_nosync if there is no syncpoint at \@offset.
 */
extern pt_export int pt_insn_sync_set(struct pt_insn_decoder *decoder,
				      uint64_t offset);

/** Get the current decoder position.
 *
 * Fills the current \@decoder position into \@offset.
 *
 * This is useful for reporting errors.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@offset is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_insn_get_offset(const struct pt_insn_decoder *decoder,
					uint64_t *offset);

/** Get the position of the last synchronization point.
 *
 * Fills the last synchronization position into \@offset.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@offset is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int
pt_insn_get_sync_offset(const struct pt_insn_decoder *decoder,
			uint64_t *offset);

/** Get the traced image.
 *
 * The returned image may be modified as long as no decoder that uses this
 * image is running.
 *
 * Returns a pointer to the traced image the decoder uses for reading memory.
 * Returns NULL if \@decoder is NULL.
 */
extern pt_export struct pt_image *
pt_insn_get_image(struct pt_insn_decoder *decoder);

/** Set the traced image.
 *
 * Sets the image that \@decoder uses for reading memory to \@image.  If \@image
 * is NULL, sets the image to \@decoder's default image.
 *
 * Only one image can be active at any time.
 *
 * Returns zero on success, a negative error code otherwise.
 * Return -pte_invalid if \@decoder is NULL.
 */
extern pt_export int pt_insn_set_image(struct pt_insn_decoder *decoder,
				       struct pt_image *image);

/* Return a pointer to \@decoder's configuration.
 *
 * Returns a non-null pointer on success, NULL if \@decoder is NULL.
 */
extern pt_export const struct pt_config *
pt_insn_get_config(const struct pt_insn_decoder *decoder);

/** Return the current time.
 *
 * On success, provides the time at the last preceding timing packet in \@time.
 *
 * The time is similar to what a rdtsc instruction would return.  Depending
 * on the configuration, the time may not be fully accurate.  If TSC is not
 * enabled, the time is relative to the last synchronization and can't be used
 * to correlate with other TSC-based time sources.  In this case, -pte_no_time
 * is returned and the relative time is provided in \@time.
 *
 * Some timing-related packets may need to be dropped (mostly due to missing
 * calibration or incomplete configuration).  To get an idea about the quality
 * of the estimated time, we record the number of dropped MTC and CYC packets.
 *
 * If \@lost_mtc is not NULL, set it to the number of lost MTC packets.
 * If \@lost_cyc is not NULL, set it to the number of lost CYC packets.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@time is NULL.
 * Returns -pte_no_time if there has not been a TSC packet.
 */
extern pt_export int pt_insn_time(struct pt_insn_decoder *decoder,
				  uint64_t *time, uint32_t *lost_mtc,
				  uint32_t *lost_cyc);

/** Return the current core bus ratio.
 *
 * On success, provides the current core:bus ratio in \@cbr.  The ratio is
 * defined as core cycles per bus clock cycle.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@cbr is NULL.
 * Returns -pte_no_cbr if there has not been a CBR packet.
 */
extern pt_export int pt_insn_core_bus_ratio(struct pt_insn_decoder *decoder,
					    uint32_t *cbr);

/** Return the current address space identifier.
 *
 * On success, provides the current address space identifier in \@asid.
 *
 * The \@size argument must be set to sizeof(struct pt_asid).  At most \@size
 * bytes will be copied and \@asid->size will be set to the actual size of the
 * provided address space identifier.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@asid is NULL.
 */
extern pt_export int pt_insn_asid(const struct pt_insn_decoder *decoder,
				  struct pt_asid *asid, size_t size);

/** Determine the next instruction.
 *
 * On success, provides the next instruction in execution order in \@insn.
 *
 * The \@size argument must be set to sizeof(struct pt_insn).
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 *
 * Returns pts_eos to indicate the end of the trace stream.  Subsequent calls
 * to pt_insn_next() will continue to return pts_eos until trace is required
 * to determine the next instruction.
 *
 * Returns -pte_bad_context if the decoder encountered an unexpected packet.
 * Returns -pte_bad_opc if the decoder encountered unknown packets.
 * Returns -pte_bad_packet if the decoder encountered unknown packet payloads.
 * Returns -pte_bad_query if the decoder got out of sync.
 * Returns -pte_eos if decoding reached the end of the Intel PT buffer.
 * Returns -pte_invalid if \@decoder or \@insn is NULL.
 * Returns -pte_nomap if the memory at the instruction address can't be read.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_insn_next(struct pt_insn_decoder *decoder,
				  struct pt_insn *insn, size_t size);

/** Get the next pending event.
 *
 * On success, provides the next event in \@event and updates \@decoder.
 *
 * The \@size argument must be set to sizeof(struct pt_event).
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 *
 * Returns -pte_bad_query if there is no event.
 * Returns -pte_invalid if \@decoder or \@event is NULL.
 * Returns -pte_invalid if \@size is too small.
 */
extern pt_export int pt_insn_event(struct pt_insn_decoder *decoder,
				   struct pt_event *event, size_t size);



/* Block decoder. */



/** A block of instructions.
 *
 * Instructions in this block are executed sequentially but are not necessarily
 * contiguous in memory.  Users are expected to follow direct branches.
 */
struct pt_block {
	/** The IP of the first instruction in this block. */
	uint64_t ip;

	/** The IP of the last instruction in this block.
	 *
	 * This can be used for error-detection.
	 */
	uint64_t end_ip;

	/** The image section that contains the instructions in this block.
	 *
	 * A value of zero means that the section did not have an identifier.
	 * The section was not added via an image section cache or the memory
	 * was read via the read memory callback.
	 */
	int isid;

	/** The execution mode for all instructions in this block. */
	enum pt_exec_mode mode;

	/** The instruction class for the last instruction in this block.
	 *
	 * This field may be set to ptic_error to indicate that the instruction
	 * class is not available.  The block decoder may choose to not provide
	 * the instruction class in some cases for performance reasons.
	 */
	enum pt_insn_class iclass;

	/** The number of instructions in this block. */
	uint16_t ninsn;

	/** The raw bytes of the last instruction in this block in case the
	 * instruction does not fit entirely into this block's section.
	 *
	 * This field is only valid if \@truncated is set.
	 */
	uint8_t raw[pt_max_insn_size];

	/** The size of the last instruction in this block in bytes.
	 *
	 * This field is only valid if \@truncated is set.
	 */
	uint8_t size;

	/** A collection of flags giving additional information about the
	 * instructions in this block.
	 *
	 * - all instructions in this block were executed speculatively.
	 */
	uint32_t speculative:1;

	/** - the last instruction in this block is truncated.
	 *
	 *    It starts in this block's section but continues in one or more
	 *    other sections depending on how fragmented the memory image is.
	 *
	 *    The raw bytes for the last instruction are provided in \@raw and
	 *    its size in \@size in this case.
	 */
	uint32_t truncated:1;
};

/** Allocate an Intel PT block decoder.
 *
 * The decoder will work on the buffer defined in \@config, it shall contain
 * raw trace data and remain valid for the lifetime of the decoder.
 *
 * The decoder needs to be synchronized before it can be used.
 */
extern pt_export struct pt_block_decoder *
pt_blk_alloc_decoder(const struct pt_config *config);

/** Free an Intel PT block decoder.
 *
 * This will destroy the decoder's default image.
 *
 * The \@decoder must not be used after a successful return.
 */
extern pt_export void pt_blk_free_decoder(struct pt_block_decoder *decoder);

/** Synchronize an Intel PT block decoder.
 *
 * Search for the next synchronization point in forward or backward direction.
 *
 * If \@decoder has not been synchronized, yet, the search is started at the
 * beginning of the trace buffer in case of forward synchronization and at the
 * end of the trace buffer in case of backward synchronization.
 *
 * Returns zero or a positive value on success, a negative error code otherwise.
 *
 * Returns -pte_bad_opc if an unknown packet is encountered.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_eos if no further synchronization point is found.
 * Returns -pte_invalid if \@decoder is NULL.
 */
extern pt_export int pt_blk_sync_forward(struct pt_block_decoder *decoder);
extern pt_export int pt_blk_sync_backward(struct pt_block_decoder *decoder);

/** Manually synchronize an Intel PT block decoder.
 *
 * Synchronize \@decoder on the syncpoint at \@offset.  There must be a PSB
 * packet at \@offset.
 *
 * Returns zero or a positive value on success, a negative error code otherwise.
 *
 * Returns -pte_bad_opc if an unknown packet is encountered.
 * Returns -pte_bad_packet if an unknown packet payload is encountered.
 * Returns -pte_eos if \@offset lies outside of \@decoder's trace buffer.
 * Returns -pte_eos if \@decoder reaches the end of its trace buffer.
 * Returns -pte_invalid if \@decoder is NULL.
 * Returns -pte_nosync if there is no syncpoint at \@offset.
 */
extern pt_export int pt_blk_sync_set(struct pt_block_decoder *decoder,
				     uint64_t offset);

/** Get the current decoder position.
 *
 * Fills the current \@decoder position into \@offset.
 *
 * This is useful for reporting errors.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@offset is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_blk_get_offset(const struct pt_block_decoder *decoder,
				       uint64_t *offset);

/** Get the position of the last synchronization point.
 *
 * Fills the last synchronization position into \@offset.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@offset is NULL.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int
pt_blk_get_sync_offset(const struct pt_block_decoder *decoder,
		       uint64_t *offset);

/** Get the traced image.
 *
 * The returned image may be modified as long as \@decoder is not running.
 *
 * Returns a pointer to the traced image \@decoder uses for reading memory.
 * Returns NULL if \@decoder is NULL.
 */
extern pt_export struct pt_image *
pt_blk_get_image(struct pt_block_decoder *decoder);

/** Set the traced image.
 *
 * Sets the image that \@decoder uses for reading memory to \@image.  If \@image
 * is NULL, sets the image to \@decoder's default image.
 *
 * Only one image can be active at any time.
 *
 * Returns zero on success, a negative error code otherwise.
 * Return -pte_invalid if \@decoder is NULL.
 */
extern pt_export int pt_blk_set_image(struct pt_block_decoder *decoder,
				      struct pt_image *image);

/* Return a pointer to \@decoder's configuration.
 *
 * Returns a non-null pointer on success, NULL if \@decoder is NULL.
 */
extern pt_export const struct pt_config *
pt_blk_get_config(const struct pt_block_decoder *decoder);

/** Return the current time.
 *
 * On success, provides the time at the last preceding timing packet in \@time.
 *
 * The time is similar to what a rdtsc instruction would return.  Depending
 * on the configuration, the time may not be fully accurate.  If TSC is not
 * enabled, the time is relative to the last synchronization and can't be used
 * to correlate with other TSC-based time sources.  In this case, -pte_no_time
 * is returned and the relative time is provided in \@time.
 *
 * Some timing-related packets may need to be dropped (mostly due to missing
 * calibration or incomplete configuration).  To get an idea about the quality
 * of the estimated time, we record the number of dropped MTC and CYC packets.
 *
 * If \@lost_mtc is not NULL, set it to the number of lost MTC packets.
 * If \@lost_cyc is not NULL, set it to the number of lost CYC packets.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@time is NULL.
 * Returns -pte_no_time if there has not been a TSC packet.
 */
extern pt_export int pt_blk_time(struct pt_block_decoder *decoder,
				 uint64_t *time, uint32_t *lost_mtc,
				 uint32_t *lost_cyc);

/** Return the current core bus ratio.
 *
 * On success, provides the current core:bus ratio in \@cbr.  The ratio is
 * defined as core cycles per bus clock cycle.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@cbr is NULL.
 * Returns -pte_no_cbr if there has not been a CBR packet.
 */
extern pt_export int pt_blk_core_bus_ratio(struct pt_block_decoder *decoder,
					   uint32_t *cbr);

/** Return the current address space identifier.
 *
 * On success, provides the current address space identifier in \@asid.
 *
 * The \@size argument must be set to sizeof(struct pt_asid).  At most \@size
 * bytes will be copied and \@asid->size will be set to the actual size of the
 * provided address space identifier.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_invalid if \@decoder or \@asid is NULL.
 */
extern pt_export int pt_blk_asid(const struct pt_block_decoder *decoder,
				 struct pt_asid *asid, size_t size);

/** Determine the next block of instructions.
 *
 * On success, provides the next block of instructions in execution order in
 * \@block.
 *
 * The \@size argument must be set to sizeof(struct pt_block).
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 *
 * Returns pts_eos to indicate the end of the trace stream.  Subsequent calls
 * to pt_block_next() will continue to return pts_eos until trace is required
 * to determine the next instruction.
 *
 * Returns -pte_bad_context if the decoder encountered an unexpected packet.
 * Returns -pte_bad_opc if the decoder encountered unknown packets.
 * Returns -pte_bad_packet if the decoder encountered unknown packet payloads.
 * Returns -pte_bad_query if the decoder got out of sync.
 * Returns -pte_eos if decoding reached the end of the Intel PT buffer.
 * Returns -pte_invalid if \@decoder or \@block is NULL.
 * Returns -pte_nomap if the memory at the instruction address can't be read.
 * Returns -pte_nosync if \@decoder is out of sync.
 */
extern pt_export int pt_blk_next(struct pt_block_decoder *decoder,
				 struct pt_block *block, size_t size);

/** Get the next pending event.
 *
 * On success, provides the next event in \@event and updates \@decoder.
 *
 * The \@size argument must be set to sizeof(struct pt_event).
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 *
 * Returns -pte_bad_query if there is no event.
 * Returns -pte_invalid if \@decoder or \@event is NULL.
 * Returns -pte_invalid if \@size is too small.
 */
extern pt_export int pt_blk_event(struct pt_block_decoder *decoder,
				  struct pt_event *event, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* INTEL_PT_H */
