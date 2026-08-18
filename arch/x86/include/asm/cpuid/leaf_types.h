/* SPDX-License-Identifier: MIT */
/* Generator: x86-cpuid-db v3.1 */

/*
 * Auto-generated file.
 * Please submit all updates and bugfixes to https://x86-cpuid.org
 */

#ifndef _ASM_X86_CPUID_LEAF_TYPES
#define _ASM_X86_CPUID_LEAF_TYPES

#include <linux/types.h>

/*
 * Leaf 0x0
 * Maximum standard leaf + CPU vendor string
 */

struct leaf_0x0_0 {
	// eax
	u32	max_std_leaf			: 32; // Highest standard CPUID leaf
	// ebx
	u32	cpu_vendorid_0			: 32; // CPU vendor ID string bytes 0 - 3
	// ecx
	u32	cpu_vendorid_2			: 32; // CPU vendor ID string bytes 8 - 11
	// edx
	u32	cpu_vendorid_1			: 32; // CPU vendor ID string bytes 4 - 7
};

/*
 * Leaf 0x1
 * CPU FMS (Family/Model/Stepping) + standard feature flags
 */

struct leaf_0x1_0 {
	// eax
	u32	stepping			:  4, // Stepping ID
		base_model			:  4, // Base CPU model ID
		base_family_id			:  4, // Base CPU family ID
		cpu_type			:  2, // CPU type
						:  2, // Reserved
		ext_model			:  4, // Extended CPU model ID
		ext_family			:  8, // Extended CPU family ID
						:  4; // Reserved
	// ebx
	u32	brand_id			:  8, // Brand index
		clflush_size			:  8, // CLFLUSH instruction cache line size
		n_logical_cpu			:  8, // Logical CPU count
		local_apic_id			:  8; // Initial local APIC physical ID
	// ecx
	u32	sse3				:  1, // Streaming SIMD Extensions 3 (SSE3)
		pclmulqdq			:  1, // PCLMULQDQ instruction support
		dtes64				:  1, // 64-bit DS save area
		monitor				:  1, // MONITOR/MWAIT support
		dscpl				:  1, // CPL Qualified Debug Store
		vmx				:  1, // Virtual Machine Extensions
		smx				:  1, // Safer Mode Extensions
		est				:  1, // Enhanced Intel SpeedStep
		tm2				:  1, // Thermal Monitor 2
		ssse3				:  1, // Supplemental SSE3
		cntxt_id			:  1, // L1 Context ID
		sdbg				:  1, // Silicon Debug
		fma				:  1, // FMA extensions using YMM state
		cx16				:  1, // CMPXCHG16B instruction support
		xtpr_update			:  1, // xTPR Update Control
		pdcm				:  1, // Perfmon and Debug Capability
						:  1, // Reserved
		pcid				:  1, // Process-context identifiers
		dca				:  1, // Direct Cache Access
		sse4_1				:  1, // SSE4.1
		sse4_2				:  1, // SSE4.2
		x2apic				:  1, // X2APIC support
		movbe				:  1, // MOVBE instruction support
		popcnt				:  1, // POPCNT instruction support
		tsc_deadline_timer		:  1, // APIC timer one-shot operation
		aes				:  1, // AES instructions
		xsave				:  1, // XSAVE (and related instructions) support
		osxsave				:  1, // XSAVE (and related instructions) are enabled by OS
		avx				:  1, // AVX instructions support
		f16c				:  1, // Half-precision floating-point conversion support
		rdrand				:  1, // RDRAND instruction support
		guest_status			:  1; // System is running as guest; (para-)virtualized system
	// edx
	u32	fpu				:  1, // Floating-Point Unit on-chip (x87)
		vme				:  1, // Virtual-8086 Mode Extensions
		de				:  1, // Debugging Extensions
		pse				:  1, // Page Size Extension
		tsc				:  1, // Time Stamp Counter
		msr				:  1, // Model-Specific Registers (RDMSR and WRMSR support)
		pae				:  1, // Physical Address Extensions
		mce				:  1, // Machine Check Exception
		cx8				:  1, // CMPXCHG8B instruction
		apic				:  1, // APIC on-chip
						:  1, // Reserved
		sep				:  1, // SYSENTER, SYSEXIT, and associated MSRs
		mtrr				:  1, // Memory Type Range Registers
		pge				:  1, // Page Global Extensions
		mca				:  1, // Machine Check Architecture
		cmov				:  1, // Conditional Move Instruction
		pat				:  1, // Page Attribute Table
		pse36				:  1, // Page Size Extension (36-bit)
		psn				:  1, // Processor Serial Number
		clflush				:  1, // CLFLUSH instruction
						:  1, // Reserved
		ds				:  1, // Debug Store
		acpi				:  1, // Thermal monitor and clock control
		mmx				:  1, // MMX instructions
		fxsr				:  1, // FXSAVE and FXRSTOR instructions
		sse				:  1, // SSE instructions
		sse2				:  1, // SSE2 instructions
		selfsnoop			:  1, // Self Snoop
		htt				:  1, // Hyper-threading
		tm				:  1, // Thermal Monitor
		ia64				:  1, // Legacy IA-64 (Itanium) support bit, now reserved
		pbe				:  1; // Pending Break Enable
};

/*
 * Leaf 0x2
 * Intel cache and TLB information one-byte descriptors
 */

struct leaf_0x2_0 {
	// eax
	u32	iteration_count			:  8, // Number of times this leaf must be queried
		desc1				:  8, // Descriptor #1
		desc2				:  8, // Descriptor #2
		desc3				:  7, // Descriptor #3
		eax_invalid			:  1; // Descriptors 1-3 are invalid if set
	// ebx
	u32	desc4				:  8, // Descriptor #4
		desc5				:  8, // Descriptor #5
		desc6				:  8, // Descriptor #6
		desc7				:  7, // Descriptor #7
		ebx_invalid			:  1; // Descriptors 4-7 are invalid if set
	// ecx
	u32	desc8				:  8, // Descriptor #8
		desc9				:  8, // Descriptor #9
		desc10				:  8, // Descriptor #10
		desc11				:  7, // Descriptor #11
		ecx_invalid			:  1; // Descriptors 8-11 are invalid if set
	// edx
	u32	desc12				:  8, // Descriptor #12
		desc13				:  8, // Descriptor #13
		desc14				:  8, // Descriptor #14
		desc15				:  7, // Descriptor #15
		edx_invalid			:  1; // Descriptors 12-15 are invalid if set
};

/*
 * Leaf 0x4
 * Intel deterministic cache parameters
 */

struct leaf_0x4_n {
	// eax
	u32	cache_type			:  5, // Cache type field
		cache_level			:  3, // Cache level (1-based)
		cache_self_init			:  1, // Self-initializing cache level
		fully_associative		:  1, // Fully-associative cache
						:  4, // Reserved
		num_threads_sharing		: 12, // Number logical CPUs sharing this cache
		num_cores_on_die		:  6; // Number of cores in the physical package
	// ebx
	u32	cache_linesize			: 12, // System coherency line size (0-based)
		cache_npartitions		: 10, // Physical line partitions (0-based)
		cache_nways			: 10; // Ways of associativity (0-based)
	// ecx
	u32	cache_nsets			: 31, // Cache number of sets (0-based)
						:  1; // Reserved
	// edx
	u32	wbinvd_rll_no_guarantee		:  1, // WBINVD/INVD not guaranteed for Remote Lower-Level caches
		ll_inclusive			:  1, // Cache is inclusive of Lower-Level caches
		complex_indexing		:  1, // Not a direct-mapped cache (complex function)
						: 29; // Reserved
};

#define LEAF_0x4_SUBLEAF_N_FIRST		0
#define LEAF_0x4_SUBLEAF_N_LAST			31

/*
 * Leaf 0x5
 * MONITOR/MWAIT instructions
 */

struct leaf_0x5_0 {
	// eax
	u32	min_mon_size			: 16, // Smallest monitor-line size, in bytes
						: 16; // Reserved
	// ebx
	u32	max_mon_size			: 16, // Largest monitor-line size, in bytes
						: 16; // Reserved
	// ecx
	u32	mwait_ext			:  1, // MONITOR/MWAIT extensions
		mwait_irq_break			:  1, // Interrupts as a break event for MWAIT
						: 30; // Reserved
	// edx
	u32	n_c0_substates			:  4, // Number of C0 sub C-states
		n_c1_substates			:  4, // Number of C1 sub C-states
		n_c2_substates			:  4, // Number of C2 sub C-states
		n_c3_substates			:  4, // Number of C3 sub C-states
		n_c4_substates			:  4, // Number of C4 sub C-states
		n_c5_substates			:  4, // Number of C5 sub C-states
		n_c6_substates			:  4, // Number of C6 sub C-states
		n_c7_substates			:  4; // Number of C7 sub C-states
};

/*
 * Leaf 0x6
 * Thermal and power management
 */

struct leaf_0x6_0 {
	// eax
	u32	digital_temp			:  1, // Digital temperature sensor
		turbo_boost			:  1, // Intel Turbo Boost
		lapic_timer_always_on		:  1, // Always-Running APIC Timer (not affected by p-state)
						:  1, // Reserved
		power_limit_event		:  1, // Power Limit Notification (PLN) event
		ecmd				:  1, // Clock modulation duty cycle extension
		package_thermal			:  1, // Package thermal management
		hwp_base_regs			:  1, // HWP (Hardware P-states) base registers
		hwp_notify			:  1, // HWP notification (IA32_HWP_INTERRUPT MSR)
		hwp_activity_window		:  1, // HWP activity window (IA32_HWP_REQUEST[bits 41:32])
		hwp_energy_perf_pr		:  1, // HWP Energy Performance Preference
		hwp_package_req			:  1, // HWP Package Level Request
						:  1, // Reserved
		hdc_base_regs			:  1, // HDC base registers
		turbo_boost_3_0			:  1, // Intel Turbo Boost Max 3.0
		hwp_capabilities		:  1, // HWP Highest Performance change
		hwp_peci_override		:  1, // HWP PECI override
		hwp_flexible			:  1, // Flexible HWP
		hwp_fast			:  1, // IA32_HWP_REQUEST MSR fast access mode
		hw_feedback			:  1, // HW_FEEDBACK MSRs
		hwp_ignore_idle			:  1, // Ignoring idle logical CPU HWP request is supported
						:  1, // Reserved
		hwp_ctl				:  1, // IA32_HWP_CTL MSR
		thread_director			:  1, // Intel thread director
		therm_interrupt_bit25		:  1, // IA32_THERM_INTERRUPT MSR bit 25
						:  7; // Reserved
	// ebx
	u32	n_therm_thresholds		:  4, // Digital thermometer thresholds
						: 28; // Reserved
	// ecx
	u32	aperf_mperf			:  1, // MPERF/APERF MSRs (effective frequency interface)
						:  2, // Reserved
		energy_perf_bias		:  1, // IA32_ENERGY_PERF_BIAS MSR
						:  4, // Reserved
		hw_feedback_nclasses		:  8, // Number of Intel Thread Director classes
						: 16; // Reserved
	// edx
	u32	perfcap_reporting		:  1, // Performance capability reporting
		encap_reporting			:  1, // Energy efficiency capability reporting
						:  6, // Reserved
		feedback_sz			:  4, // Feedback interface structure size, in 4K pages
						:  4, // Reserved
		this_lcpu_hwfdbk_idx		: 16; // This logical CPU hardware feedback interface index
};

/*
 * Leaf 0x7
 * Extended CPU features
 */

struct leaf_0x7_0 {
	// eax
	u32	leaf7_n_subleaves		: 32; // Number of leaf 0x7 subleaves
	// ebx
	u32	fsgsbase			:  1, // FSBASE/GSBASE read/write
		tsc_adjust			:  1, // IA32_TSC_ADJUST MSR
		sgx				:  1, // Intel SGX (Software Guard Extensions)
		bmi1				:  1, // Bit manipulation extensions group 1
		hle				:  1, // Hardware Lock Elision
		avx2				:  1, // AVX2 instruction set
		fdp_excptn_only			:  1, // FPU Data Pointer updated only on x87 exceptions
		smep				:  1, // Supervisor Mode Execution Protection
		bmi2				:  1, // Bit manipulation extensions group 2
		erms				:  1, // Enhanced REP MOVSB/STOSB
		invpcid				:  1, // INVPCID instruction (Invalidate Processor Context ID)
		rtm				:  1, // Intel restricted transactional memory
		pqm				:  1, // Intel RDT-CMT / AMD Platform-QoS cache monitoring
		zero_fcs_fds			:  1, // Deprecated FPU CS/DS (stored as zero)
		mpx				:  1, // Intel memory protection extensions
		rdt_a				:  1, // Intel RDT / AMD Platform-QoS Enforcement
		avx512f				:  1, // AVX-512 foundation instructions
		avx512dq			:  1, // AVX-512 double/quadword instructions
		rdseed				:  1, // RDSEED instruction
		adx				:  1, // ADCX/ADOX instructions
		smap				:  1, // Supervisor mode access prevention
		avx512ifma			:  1, // AVX-512 integer fused multiply add
						:  1, // Reserved
		clflushopt			:  1, // CLFLUSHOPT instruction
		clwb				:  1, // CLWB instruction
		intel_pt			:  1, // Intel processor trace
		avx512pf			:  1, // AVX-512 prefetch instructions
		avx512er			:  1, // AVX-512 exponent/reciprocal instructions
		avx512cd			:  1, // AVX-512 conflict detection instructions
		sha				:  1, // SHA/SHA256 instructions
		avx512bw			:  1, // AVX-512 byte/word instructions
		avx512vl			:  1; // AVX-512 VL (128/256 vector length) extensions
	// ecx
	u32	prefetchwt1			:  1, // PREFETCHWT1 (Intel Xeon Phi only)
		avx512vbmi			:  1, // AVX-512 Vector byte manipulation instructions
		umip				:  1, // User mode instruction protection
		pku				:  1, // Protection keys for user-space
		ospke				:  1, // OS protection keys enable
		waitpkg				:  1, // WAITPKG instructions
		avx512_vbmi2			:  1, // AVX-512 vector byte manipulation instructions group 2
		cet_ss				:  1, // CET shadow stack features
		gfni				:  1, // Galois field new instructions
		vaes				:  1, // Vector AES instructions
		vpclmulqdq			:  1, // VPCLMULQDQ 256-bit instruction
		avx512_vnni			:  1, // Vector neural network instructions
		avx512_bitalg			:  1, // AVX-512 bitwise algorithms
		tme				:  1, // Intel total memory encryption
		avx512_vpopcntdq		:  1, // AVX-512: POPCNT for vectors of DWORD/QWORD
						:  1, // Reserved
		la57				:  1, // 57-bit linear addresses (five-level paging)
		mawau_val_lm			:  5, // BNDLDX/BNDSTX MAWAU value in 64-bit mode
		rdpid				:  1, // RDPID instruction
		key_locker			:  1, // Intel key locker
		bus_lock_detect			:  1, // OS bus-lock detection
		cldemote			:  1, // CLDEMOTE instruction
						:  1, // Reserved
		movdiri				:  1, // MOVDIRI instruction
		movdir64b			:  1, // MOVDIR64B instruction
		enqcmd				:  1, // Enqueue stores (ENQCMD{,S})
		sgx_lc				:  1, // Intel SGX launch configuration
		pks				:  1; // Protection keys for supervisor-mode pages
	// edx
	u32					:  1, // Reserved
		sgx_keys			:  1, // Intel SGX attestation services
		avx512_4vnniw			:  1, // AVX-512 neural network instructions
		avx512_4fmaps			:  1, // AVX-512 multiply accumulation single precision
		fsrm				:  1, // Fast short REP MOVSB
		uintr				:  1, // User interrupts
						:  2, // Reserved
		avx512_vp2intersect		:  1, // VP2INTERSECT{D,Q} instructions
		srbds_ctrl			:  1, // SRBDS mitigation MSR
		md_clear			:  1, // VERW MD_CLEAR microcode
		rtm_always_abort		:  1, // XBEGIN (RTM transaction) always aborts
						:  1, // Reserved
		tsx_force_abort			:  1, // MSR TSX_FORCE_ABORT, RTM_ABORT bit
		serialize			:  1, // SERIALIZE instruction
		hybrid_cpu			:  1, // The CPU is identified as a 'hybrid part'
		tsxldtrk			:  1, // TSX suspend/resume load address tracking
						:  1, // Reserved
		pconfig				:  1, // PCONFIG instruction
		arch_lbr			:  1, // Intel architectural LBRs
		cet_ibt				:  1, // CET indirect branch tracking
						:  1, // Reserved
		amx_bf16			:  1, // AMX-BF16: tile bfloat16
		avx512_fp16			:  1, // AVX-512 FP16 instructions
		amx_tile			:  1, // AMX-TILE: tile architecture
		amx_int8			:  1, // AMX-INT8: tile 8-bit integer
		spec_ctrl			:  1, // Speculation Control (IBRS/IBPB: indirect branch restrictions)
		intel_stibp			:  1, // Single thread indirect branch predictors
		flush_l1d			:  1, // FLUSH L1D cache: IA32_FLUSH_CMD MSR
		arch_capabilities		:  1, // Intel IA32_ARCH_CAPABILITIES MSR
		core_capabilities		:  1, // IA32_CORE_CAPABILITIES MSR
		spec_ctrl_ssbd			:  1; // Speculative store bypass disable
};

struct leaf_0x7_1 {
	// eax
	u32					:  4, // Reserved
		avx_vnni			:  1, // AVX-VNNI instructions
		avx512_bf16			:  1, // AVX-512 bfloat16 instructions
		lass				:  1, // Linear address space separation
		cmpccxadd			:  1, // CMPccXADD instructions
		arch_perfmon_ext		:  1, // ArchPerfmonExt: leaf 0x23
						:  1, // Reserved
		fzrm				:  1, // Fast zero-length REP MOVSB
		fsrs				:  1, // Fast short REP STOSB
		fsrc				:  1, // Fast Short REP CMPSB/SCASB
						:  4, // Reserved
		fred				:  1, // FRED: Flexible return and event delivery transitions
		lkgs				:  1, // LKGS: Load 'kernel' (userspace) GS
		wrmsrns				:  1, // WRMSRNS instruction (WRMSR-non-serializing)
		nmi_src				:  1, // NMI-source reporting with FRED event data
		amx_fp16			:  1, // AMX-FP16: FP16 tile operations
		hreset				:  1, // HRESET (Thread director history reset)
		avx_ifma			:  1, // Integer fused multiply add
						:  2, // Reserved
		lam				:  1, // Linear address masking
		rd_wr_msrlist			:  1, // RDMSRLIST/WRMSRLIST instructions
						:  4; // Reserved
	// ebx
	u32	intel_ppin			:  1, // Protected processor inventory number (PPIN{,_CTL} MSRs)
						: 31; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					:  4, // Reserved
		avx_vnni_int8			:  1, // AVX-VNNI-INT8 instructions
		avx_ne_convert			:  1, // AVX-NE-CONVERT instructions
						:  2, // Reserved
		amx_complex			:  1, // AMX-COMPLEX instructions (starting from Granite Rapids)
						:  5, // Reserved
		prefetchit_0_1			:  1, // PREFETCHIT0/1 instructions
						:  3, // Reserved
		cet_sss				:  1, // CET supervisor shadow stacks safe to use
						: 13; // Reserved
};

struct leaf_0x7_2 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32	intel_psfd			:  1, // Intel predictive store forward disable
		ipred_ctrl			:  1, // MSR bits IA32_SPEC_CTRL.IPRED_DIS_{U,S}
		rrsba_ctrl			:  1, // MSR bits IA32_SPEC_CTRL.RRSBA_DIS_{U,S}
		ddp_ctrl			:  1, // MSR bit IA32_SPEC_CTRL.DDPD_U
		bhi_ctrl			:  1, // MSR bit IA32_SPEC_CTRL.BHI_DIS_S
		mcdt_no				:  1, // MCDT mitigation not needed
		uclock_disable			:  1, // UC-lock disable
						: 25; // Reserved
};

/*
 * Leaf 0x9
 * Intel DCA (Direct Cache Access)
 */

struct leaf_0x9_0 {
	// eax
	u32	dca_enabled_in_bios		:  1, // DCA is enabled in BIOS
						: 31; // Reserved
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0xa
 * Intel PMU (Performance Monitoring Unit)
 */

struct leaf_0xa_0 {
	// eax
	u32	pmu_version			:  8, // Performance monitoring unit version ID
		num_counters_gp			:  8, // Number of general-purpose PMU counters per logical CPU
		bit_width_gp			:  8, // Bitwidth of PMU general-purpose counters
		events_mask_len			:  8; // Length of CPUID(0xa).EBX bit vector
	// ebx
	u32	no_core_cycle			:  1, // Core cycle event not available
		no_instruction_retired		:  1, // Instruction retired event not available
		no_reference_cycles		:  1, // Reference cycles event not available
		no_llc_reference		:  1, // LLC-reference event not available
		no_llc_misses			:  1, // LLC-misses event not available
		no_br_insn_retired		:  1, // Branch instruction retired event not available
		no_br_misses_retired		:  1, // Branch mispredict retired event not available
		no_topdown_slots		:  1, // Topdown slots event not available
		no_backend_bound		:  1, // Topdown backend bound not available
		no_bad_speculation		:  1, // Topdown bad speculation not available
		no_frontend_bound		:  1, // Topdown frontend bound not available
		no_retiring			:  1, // Topdown retiring not available
		no_lbr_inserts			:  1, // LBR inserts not available
						: 19; // Reserved
	// ecx
	u32	pmu_fcounters_bitmap		: 32; // Fixed-function PMU counters support bitmap
	// edx
	u32	num_counters_fixed		:  5, // Number of fixed PMU counters
		bitwidth_fixed			:  8, // Bitwidth of PMU fixed counters
						:  2, // Reserved
		anythread_deprecation		:  1, // AnyThread mode deprecation
						: 16; // Reserved
};

/*
 * Leaf 0xb
 * CPU extended topology v1
 */

struct leaf_0xb_n {
	// eax
	u32	x2apic_id_shift			:  5, // Bit width of this level (previous levels inclusive)
						: 27; // Reserved
	// ebx
	u32	domain_lcpus_count		: 16, // Logical CPUs count across all instances of this domain
						: 16; // Reserved
	// ecx
	u32	domain_nr			:  8, // This domain level (subleaf ID)
		domain_type			:  8, // This domain type
						: 16; // Reserved
	// edx
	u32	x2apic_id			: 32; // x2APIC ID of current logical CPU
};

#define LEAF_0xb_SUBLEAF_N_FIRST		0
#define LEAF_0xb_SUBLEAF_N_LAST			1

/*
 * Leaf 0xd
 * CPU extended state
 */

struct leaf_0xd_0 {
	// eax
	u32	xcr0_x87			:  1, // XCR0.X87
		xcr0_sse			:  1, // XCR0.SSE
		xcr0_avx			:  1, // XCR0.AVX
		xcr0_mpx_bndregs		:  1, // XCR0.BNDREGS: MPX BND0-BND3 registers
		xcr0_mpx_bndcsr			:  1, // XCR0.BNDCSR: MPX BNDCFGU/BNDSTATUS registers
		xcr0_avx512_opmask		:  1, // XCR0.OPMASK: AVX-512 k0-k7 registers
		xcr0_avx512_zmm_hi256		:  1, // XCR0.ZMM_Hi256: AVX-512 ZMM0->ZMM7/15 registers
		xcr0_avx512_hi16_zmm		:  1, // XCR0.HI16_ZMM: AVX-512 ZMM16->ZMM31 registers
						:  1, // Reserved
		xcr0_pkru			:  1, // XCR0.PKRU: XSAVE PKRU registers
						:  1, // Reserved
		xcr0_cet_u			:  1, // XCR0.CET_U: CET user state
		xcr0_cet_s			:  1, // XCR0.CET_S: CET supervisor state
						:  4, // Reserved
		xcr0_tileconfig			:  1, // XCR0.TILECONFIG: AMX can manage TILECONFIG
		xcr0_tiledata			:  1, // XCR0.TILEDATA: AMX can manage TILEDATA
						: 13; // Reserved
	// ebx
	u32	xsave_sz_xcr0			: 32; // XSAVE/XRSTOR area byte size, for XCR0 enabled features
	// ecx
	u32	xsave_sz_max			: 32; // XSAVE/XRSTOR area max byte size, all CPU features
	// edx
	u32					: 30, // Reserved
		xcr0_lwp			:  1, // AMD XCR0.LWP: Light-weight Profiling
						:  1; // Reserved
};

struct leaf_0xd_1 {
	// eax
	u32	xsaveopt			:  1, // XSAVEOPT instruction
		xsavec				:  1, // XSAVEC instruction
		xgetbv1				:  1, // XGETBV instruction with ECX = 1
		xsaves				:  1, // XSAVES/XRSTORS instructions (and XSS MSR)
		xfd				:  1, // Extended feature disable
						: 27; // Reserved
	// ebx
	u32	xsave_sz_xcr0_xss		: 32; // XSAVES/XSAVEC area byte size, for XCR0|XSS enabled features
	// ecx
	u32					:  8, // Reserved
		xss_pt				:  1, // PT state
						:  1, // Reserved
		xss_pasid			:  1, // PASID state
		xss_cet_u			:  1, // CET user state
		xss_cet_s			:  1, // CET supervisor state
		xss_hdc				:  1, // HDC state
		xss_uintr			:  1, // UINTR state
		xss_lbr				:  1, // LBR state
		xss_hwp				:  1, // HWP state
						: 15; // Reserved
	// edx
	u32					: 32; // Reserved
};

struct leaf_0xd_n {
	// eax
	u32	xsave_sz			: 32; // Subleaf-N feature save area size, in bytes
	// ebx
	u32	xsave_offset			: 32; // Subleaf-N feature save area offset, in bytes
	// ecx
	u32	is_xss_bit			:  1, // Subleaf N describes an XSS bit (otherwise XCR0)
		compacted_xsave_64byte_aligned	:  1, // When compacted, subleaf-N XSAVE area is 64-byte aligned
						: 30; // Reserved
	// edx
	u32					: 32; // Reserved
};

#define LEAF_0xd_SUBLEAF_N_FIRST		2
#define LEAF_0xd_SUBLEAF_N_LAST			63

/*
 * Leaf 0xf
 * Intel RDT / AMD PQoS resource monitoring
 */

struct leaf_0xf_0 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32	core_rmid_max			: 32; // RMID max within this core (0-based)
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					:  1, // Reserved
		llc_qos_mon			:  1, // LLC QoS-monitoring
						: 30; // Reserved
};

struct leaf_0xf_1 {
	// eax
	u32	l3c_qm_bitwidth			:  8, // L3 QoS-monitoring counter bitwidth (24-based)
		l3c_qm_overflow_bit		:  1, // QM_CTR MSR bit 61 is an overflow bit
		io_rdt_cmt			:  1, // non-CPU agent supporting Intel RDT CMT present
		io_rdt_mbm			:  1, // non-CPU agent supporting Intel RDT MBM present
						: 21; // Reserved
	// ebx
	u32	l3c_qm_conver_factor		: 32; // QM_CTR MSR conversion factor to bytes
	// ecx
	u32	l3c_qm_rmid_max			: 32; // L3 QoS-monitoring max RMID
	// edx
	u32	l3c_qm_occupancy		:  1, // L3 QoS occupancy monitoring
		l3c_qm_mbm_total		:  1, // L3 QoS total bandwidth monitoring
		l3c_qm_mbm_local		:  1, // L3 QoS local bandwidth monitoring
						: 29; // Reserved
};

/*
 * Leaf 0x10
 * Intel RDT / AMD PQoS allocation
 */

struct leaf_0x10_0 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32					:  1, // Reserved
		cat_l3				:  1, // L3 Cache Allocation Technology
		cat_l2				:  1, // L2 Cache Allocation Technology
		mba				:  1, // Memory Bandwidth Allocation
						: 28; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

struct leaf_0x10_n {
	// eax
	u32	cat_cbm_len			:  5, // L3/L2_CAT capacity bitmask length, minus-one notation
						: 27; // Reserved
	// ebx
	u32	cat_units_bitmap		: 32; // L3/L2_CAT allocation units bitmap
	// ecx
	u32					:  1, // Reserved
		l3_cat_cos_infreq_updates	:  1, // L3_CAT COS updates should be infrequent
		cat_cdp_supported		:  1, // L3/L2_CAT Code and Data Prioritization
		cat_sparse_1s			:  1, // L3/L2_CAT non-contiguous 1s value
						: 28; // Reserved
	// edx
	u32	cat_cos_max			: 16, // L3/L2_CAT max Class of Service
						: 16; // Reserved
};

#define LEAF_0x10_SUBLEAF_N_FIRST		1
#define LEAF_0x10_SUBLEAF_N_LAST		2

struct leaf_0x10_3 {
	// eax
	u32	mba_max_delay			: 12, // Max MBA throttling value; minus-one notation
						: 20; // Reserved
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32	mba_per_thread			:  1, // Per-thread MBA controls
						:  1, // Reserved
		mba_delay_linear		:  1, // Delay values are linear
						: 29; // Reserved
	// edx
	u32	mba_cos_max			: 16, // MBA max Class of Service
						: 16; // Reserved
};

/*
 * Leaf 0x12
 * Intel SGX (Software Guard Extensions)
 */

struct leaf_0x12_0 {
	// eax
	u32	sgx1				:  1, // SGX1 leaf functions
		sgx2				:  1, // SGX2 leaf functions
						:  3, // Reserved
		enclv_leaves			:  1, // ENCLV leaves
		encls_leaves			:  1, // ENCLS leaves
		enclu_everifyreport2		:  1, // ENCLU leaf EVERIFYREPORT2
						:  2, // Reserved
		encls_eupdatesvn		:  1, // ENCLS leaf EUPDATESVN
		enclu_edeccssa			:  1, // ENCLU leaf EDECCSSA
						: 20; // Reserved
	// ebx
	u32	miscselect_exinfo		:  1, // SSA.MISC frame: Enclave #PF and #GP reporting
		miscselect_cpinfo		:  1, // SSA.MISC frame: Enclave #CP reporting
						: 30; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32	max_enclave_sz_not64		:  8, // Maximum enclave size in non-64-bit mode (log2)
		max_enclave_sz_64		:  8, // Maximum enclave size in 64-bit mode (log2)
						: 16; // Reserved
};

struct leaf_0x12_1 {
	// eax
	u32	secs_attr_init			:  1, // Enclave initialized by EINIT
		secs_attr_debug			:  1, // Enclave permits debugger read/write
		secs_attr_mode64bit		:  1, // Enclave runs in 64-bit mode
						:  1, // Reserved
		secs_attr_provisionkey		:  1, // Provisioning key
		secs_attr_einittoken_key	:  1, // EINIT token key
		secs_attr_cet			:  1, // CET attributes
		secs_attr_kss			:  1, // Key Separation and Sharing
						:  2, // Reserved
		secs_attr_aexnotify		:  1, // Enclave threads: AEX notifications
						: 21; // Reserved
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32	xfrm_x87			:  1, // Enclave XFRM.X87
		xfrm_sse			:  1, // Enclave XFRM.SSE
		xfrm_avx			:  1, // Enclave XFRM.AVX
		xfrm_mpx_bndregs		:  1, // Enclave XFRM.BNDREGS (MPX BND0-BND3 registers)
		xfrm_mpx_bndcsr			:  1, // Enclave XFRM.BNDCSR (MPX BNDCFGU/BNDSTATUS registers)
		xfrm_avx512_opmask		:  1, // Enclave XFRM.OPMASK (AVX-512 k0-k7 registers)
		xfrm_avx512_zmm_hi256		:  1, // Enclave XFRM.ZMM_Hi256 (AVX-512 ZMM0->ZMM7/15 registers)
		xfrm_avx512_hi16_zmm		:  1, // Enclave XFRM.HI16_ZMM (AVX-512 ZMM16->ZMM31 registers)
						:  1, // Reserved
		xfrm_pkru			:  1, // Enclave XFRM.PKRU (XSAVE PKRU registers)
						:  7, // Reserved
		xfrm_tileconfig			:  1, // Enclave XFRM.TILECONFIG (AMX can manage TILECONFIG)
		xfrm_tiledata			:  1, // Enclave XFRM.TILEDATA (AMX can manage TILEDATA)
						: 13; // Reserved
	// edx
	u32					: 32; // Reserved
};

struct leaf_0x12_n {
	// eax
	u32	subleaf_type			:  4, // Subleaf type
						:  8, // Reserved
		epc_sec_base_addr_0		: 20; // EPC section base address, bits[12:31]
	// ebx
	u32	epc_sec_base_addr_1		: 20, // EPC section base address, bits[32:51]
						: 12; // Reserved
	// ecx
	u32	epc_sec_type			:  4, // EPC section type / property encoding
						:  8, // Reserved
		epc_sec_size_0			: 20; // EPC section size, bits[12:31]
	// edx
	u32	epc_sec_size_1			: 20, // EPC section size, bits[32:51]
						: 12; // Reserved
};

#define LEAF_0x12_SUBLEAF_N_FIRST		2
#define LEAF_0x12_SUBLEAF_N_LAST		31

/*
 * Leaf 0x14
 * Intel Processor Trace
 */

struct leaf_0x14_0 {
	// eax
	u32	pt_max_subleaf			: 32; // Maximum leaf 0x14 subleaf
	// ebx
	u32	cr3_filtering			:  1, // IA32_RTIT_CR3_MATCH is accessible
		psb_cyc				:  1, // Configurable PSB and cycle-accurate mode
		ip_filtering			:  1, // IP/TraceStop filtering; Warm-reset PT MSRs preservation
		mtc_timing			:  1, // MTC timing packet; COFI-based packets suppression
		ptwrite				:  1, // PTWRITE instruction
		power_event_trace		:  1, // Power Event Trace
		psb_pmi_preserve		:  1, // PSB and PMI preservation
		event_trace			:  1, // Event Trace packet generation
		tnt_disable			:  1, // TNT packet generation disable
						: 23; // Reserved
	// ecx
	u32	topa_output			:  1, // ToPA output scheme
		topa_multiple_entries		:  1, // ToPA tables can hold multiple entries
		single_range_output		:  1, // Single-range output
		trace_transport_output		:  1, // Trace Transport subsystem output
						: 27, // Reserved
		ip_payloads_lip			:  1; // IP payloads have LIP values (CS base included)
	// edx
	u32					: 32; // Reserved
};

struct leaf_0x14_1 {
	// eax
	u32	num_address_ranges		:  3, // Number of configurable address ranges
						: 13, // Reserved
		mtc_periods_bmp			: 16; // MTC period encodings bitmap
	// ebx
	u32	cycle_thresholds_bmp		: 16, // Cycle Threshold encodings bitmap
		psb_periods_bmp			: 16; // Configurable PSB frequency encodings bitmap
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x15
 * Intel TSC (Time Stamp Counter)
 */

struct leaf_0x15_0 {
	// eax
	u32	tsc_denominator			: 32; // Denominator of the TSC/'core crystal clock' ratio
	// ebx
	u32	tsc_numerator			: 32; // Numerator of the TSC/'core crystal clock' ratio
	// ecx
	u32	cpu_crystal_hz			: 32; // Core crystal clock nominal frequency, in Hz
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x16
 * Intel processor frequency
 */

struct leaf_0x16_0 {
	// eax
	u32	cpu_base_mhz			: 16, // Processor base frequency, in MHz
						: 16; // Reserved
	// ebx
	u32	cpu_max_mhz			: 16, // Processor max frequency, in MHz
						: 16; // Reserved
	// ecx
	u32	bus_mhz				: 16, // Bus reference frequency, in MHz
						: 16; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x17
 * Intel SoC vendor attributes
 */

struct leaf_0x17_0 {
	// eax
	u32	soc_max_subleaf			: 32; // Maximum leaf 0x17 subleaf
	// ebx
	u32	soc_vendor_id			: 16, // SoC vendor ID
		is_vendor_scheme		:  1, // Assigned by industry enumeration scheme (not Intel)
						: 15; // Reserved
	// ecx
	u32	soc_proj_id			: 32; // SoC project ID, assigned by vendor
	// edx
	u32	soc_stepping_id			: 32; // SoC project stepping ID, assigned by vendor
};

struct leaf_0x17_n {
	// eax
	u32	vendor_brand_a			: 32; // Vendor Brand ID string, bytes subleaf_nr * (0 -> 3)
	// ebx
	u32	vendor_brand_b			: 32; // Vendor Brand ID string, bytes subleaf_nr * (4 -> 7)
	// ecx
	u32	vendor_brand_c			: 32; // Vendor Brand ID string, bytes subleaf_nr * (8 -> 11)
	// edx
	u32	vendor_brand_d			: 32; // Vendor Brand ID string, bytes subleaf_nr * (12 -> 15)
};

#define LEAF_0x17_SUBLEAF_N_FIRST		1
#define LEAF_0x17_SUBLEAF_N_LAST		3

/*
 * Leaf 0x18
 * Intel deterministic address translation (TLB) parameters
 */

struct leaf_0x18_n {
	// eax
	u32	tlb_max_subleaf			: 32; // Maximum leaf 0x18 subleaf
	// ebx
	u32	tlb_4k_page			:  1, // TLB supports 4KB-page entries
		tlb_2m_page			:  1, // TLB supports 2MB-page entries
		tlb_4m_page			:  1, // TLB supports 4MB-page entries
		tlb_1g_page			:  1, // TLB supports 1GB-page entries
						:  4, // Reserved
		hard_partitioning		:  3, // Partitioning between logical CPUs
						:  5, // Reserved
		n_way_associative		: 16; // Ways of associativity
	// ecx
	u32	n_sets				: 32; // Number of sets
	// edx
	u32	tlb_type			:  5, // Translation cache type (TLB type)
		tlb_cache_level			:  3, // Translation cache level (1-based)
		is_fully_associative		:  1, // Fully-associative
						:  5, // Reserved
		tlb_max_addressable_ids		: 12, // Max number of addressable IDs - 1
						:  6; // Reserved
};

#define LEAF_0x18_SUBLEAF_N_FIRST		0
#define LEAF_0x18_SUBLEAF_N_LAST		31

/*
 * Leaf 0x19
 * Intel key locker
 */

struct leaf_0x19_0 {
	// eax
	u32	kl_cpl0_only			:  1, // CPL0-only key locker restriction
		kl_no_encrypt			:  1, // No-encrypt key locker restriction
		kl_no_decrypt			:  1, // No-decrypt key locker restriction
						: 29; // Reserved
	// ebx
	u32	aes_keylocker			:  1, // AES key locker instructions
						:  1, // Reserved
		aes_keylocker_wide		:  1, // AES wide key locker instructions
						:  1, // Reserved
		kl_msr_iwkey			:  1, // Key locker MSRs and IWKEY backups
						: 27; // Reserved
	// ecx
	u32	loadiwkey_no_backup		:  1, // LOADIWKEY NoBackup parameter
		iwkey_rand			:  1, // IWKEY randomization
						: 30; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x1a
 * Intel hybrid CPUs identification (e.g. Atom, Core)
 */

struct leaf_0x1a_0 {
	// eax
	u32	core_native_model		: 24, // This core's native model ID
		core_type			:  8; // This core's type
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x1b
 * Intel PCONFIG (Platform configuration)
 */

struct leaf_0x1b_n {
	// eax
	u32	pconfig_subleaf_type		: 12, // CPUID 0x1b subleaf type
						: 20; // Reserved
	// ebx
	u32	pconfig_target_id_x		: 32; // A supported PCONFIG target ID
	// ecx
	u32	pconfig_target_id_y		: 32; // A supported PCONFIG target ID
	// edx
	u32	pconfig_target_id_z		: 32; // A supported PCONFIG target ID
};

#define LEAF_0x1b_SUBLEAF_N_FIRST		0
#define LEAF_0x1b_SUBLEAF_N_LAST		31

/*
 * Leaf 0x1c
 * Intel LBR (Last Branch Record)
 */

struct leaf_0x1c_0 {
	// eax
	u32	lbr_depth_mask			:  8, // Max LBR stack depth bitmask
						: 22, // Reserved
		lbr_deep_c_reset		:  1, // LBRs may be cleared on MWAIT C-state > C1
		lbr_ip_is_lip			:  1; // LBR IP contain Last IP (otherwise effective IP)
	// ebx
	u32	lbr_cpl				:  1, // CPL filtering
		lbr_branch_filter		:  1, // Branch filtering
		lbr_call_stack			:  1, // Call-stack mode
						: 29; // Reserved
	// ecx
	u32	lbr_mispredict			:  1, // Branch misprediction bit
		lbr_timed_lbr			:  1, // Timed LBRs (CPU cycles since last LBR entry)
		lbr_branch_type			:  1, // Branch type field
						: 13, // Reserved
		lbr_events_gpc_bmp		:  4, // PMU-events logging support
						: 12; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x1d
 * Intel AMX (Advanced Matrix Extensions) tile information
 */

struct leaf_0x1d_0 {
	// eax
	u32	amx_max_palette			: 32; // Highest palette ID / subleaf ID
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

struct leaf_0x1d_1 {
	// eax
	u32	amx_palette_size		: 16, // AMX palette total tiles size, in bytes
		amx_tile_size			: 16; // AMX single tile's size, in bytes
	// ebx
	u32	amx_tile_row_size		: 16, // AMX tile single row's size, in bytes
		amx_palette_nr_tiles		: 16; // AMX palette number of tiles
	// ecx
	u32	amx_tile_nr_rows		: 16, // AMX tile max number of rows
						: 16; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x1e
 * Intel TMUL (Tile-matrix Multiply)
 */

struct leaf_0x1e_0 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32	tmul_maxk			:  8, // TMUL unit maximum height, K (rows or columns)
		tmul_maxn			: 16, // TMUL unit maximum SIMD dimension, N (column bytes)
						:  8; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x1f
 * Intel extended topology v2
 */

struct leaf_0x1f_n {
	// eax
	u32	x2apic_id_shift			:  5, // Bit width of this level (previous levels inclusive)
						: 27; // Reserved
	// ebx
	u32	domain_lcpus_count		: 16, // Logical CPUs count across all instances of this domain
						: 16; // Reserved
	// ecx
	u32	domain_level			:  8, // This domain level (subleaf ID)
		domain_type			:  8, // This domain type
						: 16; // Reserved
	// edx
	u32	x2apic_id			: 32; // x2APIC ID of current logical CPU
};

#define LEAF_0x1f_SUBLEAF_N_FIRST		0
#define LEAF_0x1f_SUBLEAF_N_LAST		5

/*
 * Leaf 0x20
 * Intel HRESET (History Reset)
 */

struct leaf_0x20_0 {
	// eax
	u32	hreset_nr_subleaves		: 32; // CPUID 0x20 max subleaf + 1
	// ebx
	u32	hreset_thread_director		:  1, // Intel thread director HRESET
						: 31; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x21
 * Intel TD (Trust Domain)
 */

struct leaf_0x21_0 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32	tdx_vendorid_0			: 32; // TDX vendor ID string bytes 0 - 3
	// ecx
	u32	tdx_vendorid_2			: 32; // TDX vendor ID string bytes 8 - 11
	// edx
	u32	tdx_vendorid_1			: 32; // TDX vendor ID string bytes 4 - 7
};

/*
 * Leaf 0x23
 * Intel Architectural Performance Monitoring Extended (ArchPerfmonExt)
 */

struct leaf_0x23_0 {
	// eax
	u32	subleaf_0			:  1, // Subleaf 0, this subleaf
		counters_subleaf		:  1, // Subleaf 1, PMU counter bitmaps
		acr_subleaf			:  1, // Subleaf 2, Auto Counter Reload bitmaps
		events_subleaf			:  1, // Subleaf 3, PMU event bitmaps
		pebs_caps_subleaf		:  1, // Subleaf 4, PEBS capabilities
		pebs_subleaf			:  1, // Subleaf 5, Arch PEBS bitmaps
						: 26; // Reserved
	// ebx
	u32	unitmask2			:  1, // IA32_PERFEVTSELx MSRs UnitMask2 bit
		eq				:  1, // IA32_PERFEVTSELx MSRs EQ bit
		rdpmc_user_disable		:  1, // RDPMC userspace disable
						: 29; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

struct leaf_0x23_1 {
	// eax
	u32	gp_counters			: 32; // Bitmap of general-purpose PMU counters
	// ebx
	u32	fixed_counters			: 32; // Bitmap of fixed PMU counters
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

struct leaf_0x23_2 {
	// eax
	u32	acr_gp_reload			: 32; // Bitmap of general-purpose counters that can be reloaded
	// ebx
	u32	acr_fixed_reload		: 32; // Bitmap of fixed counters that can be reloaded
	// ecx
	u32	acr_gp_trigger			: 32; // Bitmap of general-purpose counters that can trigger reloads
	// edx
	u32	acr_fixed_trigger		: 32; // Bitmap of fixed counters that can trigger reloads
};

struct leaf_0x23_3 {
	// eax
	u32	core_cycles_evt			:  1, // Core cycles event
		insn_retired_evt		:  1, // Instructions retired event
		ref_cycles_evt			:  1, // Reference cycles event
		llc_refs_evt			:  1, // Last-level cache references event
		llc_misses_evt			:  1, // Last-level cache misses event
		br_insn_ret_evt			:  1, // Branch instruction retired event
		br_mispr_evt			:  1, // Branch mispredict retired event
		td_slots_evt			:  1, // Topdown slots event
		td_backend_bound_evt		:  1, // Topdown backend bound event
		td_bad_spec_evt			:  1, // Topdown bad speculation event
		td_frontend_bound_evt		:  1, // Topdown frontend bound event
		td_retiring_evt			:  1, // Topdown retiring event
						: 20; // Reserved
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

struct leaf_0x23_4 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32					:  3, // Reserved
		allow_in_record			:  1, // ALLOW_IN_RECORD bit in MSRs
		counters_gp			:  1, // Counters group sub-group general-purpose counters
		counters_fixed			:  1, // Counters group sub-group fixed-function counters
		counters_metrics		:  1, // Counters group sub-group performance metrics
						:  1, // Reserved
		lbr				:  2, // LBR group
						:  6, // Reserved
		xer				:  8, // XER group
						:  5, // Reserved
		gpr				:  1, // GPR group
		aux				:  1, // AUX group
						:  1; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

struct leaf_0x23_5 {
	// eax
	u32	pebs_gp				: 32; // Architectural PEBS general-purpose counters
	// ebx
	u32	pebs_pdist_gp			: 32; // Architectural PEBS PDIST general-purpose counters
	// ecx
	u32	pebs_fixed			: 32; // Architectural PEBS fixed counters
	// edx
	u32	pebs_pdist_fixed		: 32; // Architectural PEBS PDIST fixed counters
};

/*
 * Leaf 0x40000000
 * Maximum hypervisor leaf + hypervisor vendor string
 */

struct leaf_0x40000000_0 {
	// eax
	u32	max_hyp_leaf			: 32; // Maximum hypervisor leaf
	// ebx
	u32	hypervisor_id_0			: 32; // Hypervisor ID string bytes 0 - 3
	// ecx
	u32	hypervisor_id_1			: 32; // Hypervisor ID string bytes 4 - 7
	// edx
	u32	hypervisor_id_2			: 32; // Hypervisor ID string bytes 8 - 11
};

/*
 * Leaf 0x4c780001
 * Linux-defined synthetic feature flags
 */

struct leaf_0x4c780001_0 {
	// eax
	u32	cxmmx				:  1, // Cyrix MMX extensions
		k6_mtrr				:  1, // AMD K6 nonstandard MTRRs
		cyrix_arr			:  1, // Cyrix ARRs (= MTRRs)
		centaur_mcr			:  1, // Centaur MCRs (= MTRRs)
		k8				:  1, // Opteron, Athlon64
		zen5				:  1, // CPU based on Zen5 micro-architecture
		zen6				:  1, // CPU based on Zen6 micro-architecture
						:  1, // Reserved
		constant_tsc			:  1, // TSC ticks at a constant rate
		up				:  1, // SMP kernel running on UP
		art				:  1, // Always running timer (ART)
		arch_perfmon			:  1, // Intel Architectural PerfMon
		pebs				:  1, // Precise-Event Based Sampling
		bts				:  1, // Branch Trace Store
		syscall32			:  1, // SYSCALL in IA32 userspace
		sysenter32			:  1, // SYSENTER in IA32 userspace
		rep_good			:  1, // REP microcode works well
		amd_lbr_v2			:  1, // AMD Last Branch Record Extension version 2
		clear_cpu_buf			:  1, // Clear CPU buffers using VERW
		acc_power			:  1, // AMD Accumulated Power Mechanism
		nopl				:  1, // The NOPL instructions
		always				:  1, // Always-present feature
		xtopology			:  1, // CPU topology enumeration extensions
		tsc_reliable			:  1, // TSC is known to be reliable
		nonstop_tsc			:  1, // TSC does not stop in C states
		cpuid				:  1, // CPU has the CPUID instruction
		extd_apicid			:  1, // Extended APIC ID (8 bits)
		amd_dcm				:  1, // AMD multi-node processor
		aperfmperf			:  1, // APERF/MPERF MSRs: P-State hardware coordination feedback
		rapl				:  1, // AMD/Hygon RAPL interface
		nonstop_tsc_s3			:  1, // TSC does not stop in S3 state
		tsc_known_freq			:  1; // TSC has known frequency
	// ebx
	u32	ring3mwait			:  1, // Ring 3 MONITOR/MWAIT instructions
		cpuid_fault			:  1, // Intel CPUID faulting
		cpb				:  1, // AMD Core Performance Boost
		epb				:  1, // IA32_ENERGY_PERF_BIAS support
		cat_l3				:  1, // Cache Allocation Technology L3
		cat_l2				:  1, // Cache Allocation Technology L2
		cdp_l3				:  1, // Code and Data Prioritization L3
		tdx_host_platform		:  1, // Platform supports being a TDX host
		hw_pstate			:  1, // AMD Hardware P-state control
		proc_feedback			:  1, // AMD Processor Feedback Interface
		xcompacted			:  1, // Use compacted XSTATE (XSAVES or XSAVEC)
		pti				:  1, // Kernel Page Table Isolation enabled
		kernel_ibrs			:  1, // Set/clear IBRS on kernel entry/exit
		rsb_vmexit			:  1, // Fill RSB on VM-Exit
		intel_ppin			:  1, // Intel Processor Inventory Number
		cdp_l2				:  1, // Code and Data Prioritization L2
		msr_spec_ctrl			:  1, // MSR SPEC_CTRL is implemented
		ssbd				:  1, // Speculative Store Bypass Disable
		mba				:  1, // Memory Bandwidth Allocation
		rsb_ctxsw			:  1, // Fill RSB on context switches
		perfmon_v2			:  1, // AMD Performance Monitoring Version 2
						:  1, // Reserved
		use_ibrs_fw			:  1, // Use IBRS during runtime firmware calls
		ss_bypass_disable		:  1, // Disable Speculative Store Bypass
		ls_cfg_ssbd			:  1, // AMD SSBD implementation via LS_CFG MSR
		ibrs				:  1, // Indirect Branch Restricted Speculation
		ibpb				:  1, // Indirect Branch Prediction Barrier (without RSB flush guarantee)
		stibp				:  1, // Single Thread Indirect Branch Predictors
		zen				:  1, // Generic flag for all Zen and newer
		l1tf_pteinv			:  1, // L1TF workaround PTE inversion
		ibrs_enhanced			:  1, // Enhanced IBRS
		msr_ia32_feat_ctl		:  1; // MSR IA32_FEAT_CTL configured
	// ecx
	u32	tpr_shadow			:  1, // Intel TPR Shadow
		flexpriority			:  1, // Intel FlexPriority
		ept				:  1, // Intel Extended Page Table
		vpid				:  1, // Intel Virtual Processor ID
		coherency_sfw_no		:  1, // SNP cache coherency software workaround not needed
						: 10, // Reserved
		vmmcall				:  1, // Prefer VMMCALL to VMCALL
		xenpv				:  1, // Xen paravirtual guest
		ept_ad				:  1, // Intel Extended Page Table access-dirty bit
		vmcall				:  1, // Hypervisor supports the VMCALL instruction
		vmw_vmmcall			:  1, // VMware prefers the VMMCALL instruction
		pvunlock			:  1, // PV unlock function
		vcpupreempt			:  1, // PV vcpu_is_preempted function
		tdx_guest			:  1, // Intel Trust Domain Extensions Guest
						:  9; // Reserved
	// edx
	u32	cqm_llc				:  1, // LLC QoS
		cqm_occup_llc			:  1, // LLC occupancy monitoring
		cqm_mbm_total			:  1, // LLC Total MBM monitoring
		cqm_mbm_local			:  1, // LLC Local MBM monitoring
		fence_swapgs_user		:  1, // LFENCE in user entry SWAPGS path
		fence_swapgs_kernel		:  1, // LFENCE in kernel entry SWAPGS path
		split_lock_detect		:  1, // #AC for split lock
		per_thread_mba			:  1, // Per-thread Memory Bandwidth Allocation
		sgx1				:  1, // SGX Basic
		sgx2				:  1, // SGX Enclave Dynamic Memory Management (EDMM)
		entry_ibpb			:  1, // Issue an IBPB on kernel entry
		rrsba_ctrl			:  1, // RET prediction control
		retpoline			:  1, // Generic Retpoline mitigation for Spectre variant 2
		retpoline_lfence		:  1, // Use LFENCE for Spectre variant 2
		rethunk				:  1, // Use Return THUNK
		unret				:  1, // AMD BTB untrain return
		use_ibpb_fw			:  1, // Use IBPB during runtime firmware calls
		rsb_vmexit_lite			:  1, // Fill RSB on VM exit when EIBRS is enabled
		sgx_edeccssa			:  1, // SGX EDECCSSA user leaf function
		call_depth			:  1, // Call depth tracking for RSB stuffing
		msr_tsx_ctrl			:  1, // MSR IA32_TSX_CTRL (Intel) implemented
		smba				:  1, // Slow Memory Bandwidth Allocation
		bmec				:  1, // Bandwidth Monitoring Event Configuration
		user_shstk			:  1, // Shadow stack support for user mode applications
		srso				:  1, // AMD BTB untrain RETs
		srso_alias			:  1, // AMD BTB untrain RETs through aliasing
		ibpb_on_vmexit			:  1, // Issue an IBPB only on VMEXIT
		apic_msrs_fence			:  1, // IA32_TSC_DEADLINE and X2APIC MSRs need fencing
		zen2				:  1, // CPU based on Zen2 microarchitecture
		zen3				:  1, // CPU based on Zen3 microarchitecture
		zen4				:  1, // CPU based on Zen4 microarchitecture
		zen1				:  1; // CPU based on Zen1 microarchitecture
};

struct leaf_0x4c780001_1 {
	// eax
	u32	overflow_recov			:  1, // MCA overflow recovery support
		succor				:  1, // Uncorrectable error containment and recovery
						:  1, // Reserved
		smca				:  1, // Scalable MCA
						: 28; // Reserved
	// ebx
	u32	amd_lbr_pmc_freeze		:  1, // AMD LBR and PMC Freeze
		clear_bhb_loop			:  1, // Clear branch history at SYSCALL entry using SW loop
		bhi_ctrl			:  1, // BHI_DIS_S HW control available
		clear_bhb_hw			:  1, // BHI_DIS_S HW control enabled
		clear_bhb_vmexit		:  1, // Clear branch history at VMEXIT using SW loop
		amd_fast_cppc			:  1, // AMD fast Collaborative Processor Performance Control
		amd_htr_cores			:  1, // Heterogeneous Core Topology
		amd_workload_class		:  1, // Workload Classification
		prefer_ymm			:  1, // Avoid ZMM registers due to downclocking
		apx				:  1, // Advanced Performance Extensions
		indirect_thunk_its		:  1, // Use thunk for indirect branches in lower half of cache line
		tsa_sq_no			:  1, // AMD CPU not vulnerable to TSA-SQ
		tsa_l1_no			:  1, // AMD CPU not vulnerable to TSA-L1
		clear_cpu_buf_vm		:  1, // Clear CPU buffers using VERW before VMRUN
		ibpb_exit_to_user		:  1, // Use IBPB on exit-to-userspace, see VMSCAPE bug
						: 17; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x4c780002
 * Linux-defined synthetic CPU bug flags
 */

struct leaf_0x4c780002_0 {
	// eax
	u32	f00f				:  1, // Intel F00F
		fdiv				:  1, // FPU FDIV
		coma				:  1, // Cyrix 6x86 coma
		amd_tlb_mmatch			:  1, // AMD Erratum 383
		amd_apic_c1e			:  1, // AMD Erratum 400
		bug_11ap			:  1, // Bad local APIC aka 11AP
		fxsave_leak			:  1, // FXSAVE leaks FOP/FIP/FOP
		clflush_monitor			:  1, // AAI65, CLFLUSH required before MONITOR
		sysret_ss_attrs			:  1, // SYSRET does not fix up SS attributes
		espfix				:  1, // IRET to 16-bit SS corrupts ESP/RSP high bits (x86-32)
		null_seg			:  1, // Setting a selector to NULL preserves the base
		swapgs_fence			:  1, // SWAPGS without input dep on GS
		monitor				:  1, // IPI required to wake up remote CPU
		amd_e400			:  1, // CPU is among the affected by Erratum 400
		cpu_meltdown			:  1, // CPU affected by meltdown; needs kernel page table isolation
		spectre_v1			:  1, // CPU affected by Spectre variant 1 with conditional branches
		spectre_v2			:  1, // CPU affected by Spectre variant 2 with indirect branches
		spec_store_bypass		:  1, // CPU affected by speculative store bypass attack
		l1tf				:  1, // CPU affected by L1 Terminal Fault
		mds				:  1, // CPU affected by Microarchitectural data sampling
		msbds_only			:  1, // Microarchitectural data sampling: CPU only affected by the MSBDS variant
		swapgs				:  1, // CPU affected by speculation through SWAPGS
		taa				:  1, // CPU is affected by TSX Async Abort (TAA)
		itlb_multihit			:  1, // CPU may incur MCE during certain page attribute changes
		srbds				:  1, // CPU may leak RNG bits if not mitigated
		mmio_stale_data			:  1, // CPU affected by Processor MMIO Stale Data vulnerabilities
						:  1, // Reserved
		retbleed			:  1, // CPU affected by Retbleed
		eibrs_pbrsb			:  1, // EIBRS is vulnerable to Post Barrier RSB Predictions
		smt_rsb				:  1, // CPU vulnerable to Cross-Thread Return Address Predictions
		gds				:  1, // CPU affected by Gather Data Sampling
		tdx_pw_mce			:  1; // CPU may incur #MC if non-TD software does partial write to TDX private memory
	// ebx
	u32	srso				:  1, // AMD SRSO bug
		div0				:  1, // AMD DIV0 speculation bug
		rfds				:  1, // CPU vulnerable to Register File Data Sampling
		bhi				:  1, // CPU affected by Branch History Injection
		ibpb_no_ret			:  1, // IBPB omits return target predictions
		spectre_v2_user			:  1, // CPU affected by Spectre variant 2 between user processes
		old_microcode			:  1, // CPU has old microcode; it must be vulnerable to something
		its				:  1, // CPU affected by Indirect Target Selection
		its_native_only			:  1, // CPU affected by ITS; VMX is not affected
		tsa				:  1, // CPU affected by Transient Scheduler Attacks
		vmscape				:  1, // CPU affected by VMSCAPE attacks from guests
						: 21; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x80000000
 * Maximum extended leaf + CPU vendor string
 */

struct leaf_0x80000000_0 {
	// eax
	u32	max_ext_leaf			: 32; // Maximum extended CPUID leaf
	// ebx
	u32	cpu_vendorid_0			: 32; // Vendor ID string bytes 0 - 3
	// ecx
	u32	cpu_vendorid_2			: 32; // Vendor ID string bytes 8 - 11
	// edx
	u32	cpu_vendorid_1			: 32; // Vendor ID string bytes 4 - 7
};

/*
 * Leaf 0x80000001
 * Extended CPU features
 */

struct leaf_0x80000001_0 {
	// eax
	u32	e_stepping_id			:  4, // Stepping ID
		e_base_model			:  4, // Base processor model
		e_base_family			:  4, // Base processor family
		e_base_type			:  2, // Base processor type (Transmeta)
						:  2, // Reserved
		e_ext_model			:  4, // Extended processor model
		e_ext_family			:  8, // Extended processor family
						:  4; // Reserved
	// ebx
	u32	brand_id			: 16, // Brand ID
						: 12, // Reserved
		pkg_type			:  4; // Package type
	// ecx
	u32	lahf_lm				:  1, // LAHF and SAHF in 64-bit mode
		cmp_legacy			:  1, // Multi-processing legacy mode (No HT)
		svm				:  1, // Secure Virtual Machine
		extapic				:  1, // Extended APIC space
		cr8_legacy			:  1, // LOCK MOV CR0 means MOV CR8
		lzcnt_abm			:  1, // LZCNT advanced bit manipulation
		sse4a				:  1, // SSE4A support
		misaligned_sse			:  1, // Misaligned SSE mode
		_3dnow_prefetch			:  1, // 3DNow PREFETCH/PREFETCHW support
		osvw				:  1, // OS visible workaround
		ibs				:  1, // Instruction based sampling
		xop				:  1, // XOP: extended operation (AVX instructions)
		skinit				:  1, // SKINIT/STGI support
		wdt				:  1, // Watchdog timer support
						:  1, // Reserved
		lwp				:  1, // Lightweight profiling
		fma4				:  1, // 4-operand FMA instruction
		tce				:  1, // Translation cache extension
						:  1, // Reserved
		nodeid_msr			:  1, // NodeId MSR (0xc001100c)
						:  1, // Reserved
		tbm				:  1, // Trailing bit manipulations
		topoext				:  1, // Topology Extensions (leaf 0x8000001d)
		perfctr_core			:  1, // Core performance counter extensions
		perfctr_nb			:  1, // NB/DF performance counter extensions
						:  1, // Reserved
		data_bp_ext			:  1, // Data access breakpoint extension
		perf_tsc			:  1, // Performance time-stamp counter
		perfctr_llc			:  1, // LLC (L3) performance counter extensions
		mwaitx				:  1, // MWAITX/MONITORX support
		addr_mask_ext			:  1, // Breakpoint address mask extension (to bit 31)
						:  1; // Reserved
	// edx
	u32	e_fpu				:  1, // Floating-Point Unit on-chip (x87)
		e_vme				:  1, // Virtual-8086 Mode Extensions
		e_de				:  1, // Debugging Extensions
		e_pse				:  1, // Page Size Extension
		e_tsc				:  1, // Time Stamp Counter
		e_msr				:  1, // Model-Specific Registers (RDMSR and WRMSR support)
		pae				:  1, // Physical Address Extensions
		mce				:  1, // Machine Check Exception
		cx8				:  1, // CMPXCHG8B instruction
		apic				:  1, // APIC on-chip
						:  1, // Reserved
		syscall				:  1, // SYSCALL and SYSRET instructions
		mtrr				:  1, // Memory Type Range Registers
		pge				:  1, // Page Global Extensions
		mca				:  1, // Machine Check Architecture
		cmov				:  1, // Conditional Move Instruction
		pat				:  1, // Page Attribute Table
		pse36				:  1, // Page Size Extension (36-bit)
						:  1, // Reserved
		obsolete_mp_bit			:  1, // Out-of-spec AMD Multiprocessing bit
		nx				:  1, // No-execute page protection
						:  1, // Reserved
		mmxext				:  1, // AMD MMX extensions
		e_mmx				:  1, // MMX instructions
		e_fxsr				:  1, // FXSAVE and FXRSTOR instructions
		fxsr_opt			:  1, // FXSAVE and FXRSTOR optimizations
		page1gb				:  1, // 1-GB large page support
		rdtscp				:  1, // RDTSCP instruction
						:  1, // Reserved
		lm				:  1, // Long mode (x86-64, 64-bit support)
		_3dnowext			:  1, // AMD 3DNow extensions
		_3dnow				:  1; // 3DNow instructions
};

/*
 * Leaf 0x80000002
 * CPU brand ID string, bytes 0 - 15
 */

struct leaf_0x80000002_0 {
	// eax
	u32	cpu_brandid_0			: 32; // CPU brand ID string, bytes 0 - 3
	// ebx
	u32	cpu_brandid_1			: 32; // CPU brand ID string, bytes 4 - 7
	// ecx
	u32	cpu_brandid_2			: 32; // CPU brand ID string, bytes 8 - 11
	// edx
	u32	cpu_brandid_3			: 32; // CPU brand ID string, bytes 12 - 15
};

/*
 * Leaf 0x80000003
 * CPU brand ID string, bytes 16 - 31
 */

struct leaf_0x80000003_0 {
	// eax
	u32	cpu_brandid_4			: 32; // CPU brand ID string bytes, 16 - 19
	// ebx
	u32	cpu_brandid_5			: 32; // CPU brand ID string bytes, 20 - 23
	// ecx
	u32	cpu_brandid_6			: 32; // CPU brand ID string bytes, 24 - 27
	// edx
	u32	cpu_brandid_7			: 32; // CPU brand ID string bytes, 28 - 31
};

/*
 * Leaf 0x80000004
 * CPU brand ID string, bytes 32 - 47
 */

struct leaf_0x80000004_0 {
	// eax
	u32	cpu_brandid_8			: 32; // CPU brand ID string, bytes 32 - 35
	// ebx
	u32	cpu_brandid_9			: 32; // CPU brand ID string, bytes 36 - 39
	// ecx
	u32	cpu_brandid_10			: 32; // CPU brand ID string, bytes 40 - 43
	// edx
	u32	cpu_brandid_11			: 32; // CPU brand ID string, bytes 44 - 47
};

/*
 * Leaf 0x80000005
 * AMD/Transmeta L1 cache and TLB
 */

struct leaf_0x80000005_0 {
	// eax
	u32	l1_itlb_2m_4m_nentries		:  8, // L1 ITLB #entries, 2M and 4M pages
		l1_itlb_2m_4m_assoc		:  8, // L1 ITLB associativity, 2M and 4M pages
		l1_dtlb_2m_4m_nentries		:  8, // L1 DTLB #entries, 2M and 4M pages
		l1_dtlb_2m_4m_assoc		:  8; // L1 DTLB associativity, 2M and 4M pages
	// ebx
	u32	l1_itlb_4k_nentries		:  8, // L1 ITLB #entries, 4K pages
		l1_itlb_4k_assoc		:  8, // L1 ITLB associativity, 4K pages
		l1_dtlb_4k_nentries		:  8, // L1 DTLB #entries, 4K pages
		l1_dtlb_4k_assoc		:  8; // L1 DTLB associativity, 4K pages
	// ecx
	u32	l1_dcache_line_size		:  8, // L1 dcache line size, in bytes
		l1_dcache_nlines		:  8, // L1 dcache lines per tag
		l1_dcache_assoc			:  8, // L1 dcache associativity
		l1_dcache_size_kb		:  8; // L1 dcache size, in KB
	// edx
	u32	l1_icache_line_size		:  8, // L1 icache line size, in bytes
		l1_icache_nlines		:  8, // L1 icache lines per tag
		l1_icache_assoc			:  8, // L1 icache associativity
		l1_icache_size_kb		:  8; // L1 icache size, in KB
};

/*
 * Leaf 0x80000006
 * (Mostly AMD) L2/L3 cache and TLB
 */

struct leaf_0x80000006_0 {
	// eax
	u32	l2_itlb_2m_4m_nentries		: 12, // L2 iTLB #entries, 2M and 4M pages
		l2_itlb_2m_4m_assoc		:  4, // L2 iTLB associativity, 2M and 4M pages
		l2_dtlb_2m_4m_nentries		: 12, // L2 dTLB #entries, 2M and 4M pages
		l2_dtlb_2m_4m_assoc		:  4; // L2 dTLB associativity, 2M and 4M pages
	// ebx
	u32	l2_itlb_4k_nentries		: 12, // L2 iTLB #entries, 4K pages
		l2_itlb_4k_assoc		:  4, // L2 iTLB associativity, 4K pages
		l2_dtlb_4k_nentries		: 12, // L2 dTLB #entries, 4K pages
		l2_dtlb_4k_assoc		:  4; // L2 dTLB associativity, 4K pages
	// ecx
	u32	l2_line_size			:  8, // L2 cache line size, in bytes
		l2_nlines			:  4, // L2 cache number of lines per tag
		l2_assoc			:  4, // L2 cache associativity
		l2_size_kb			: 16; // L2 cache size, in KB
	// edx
	u32	l3_line_size			:  8, // L3 cache line size, in bytes
		l3_nlines			:  4, // L3 cache number of lines per tag
		l3_assoc			:  4, // L3 cache associativity
						:  2, // Reserved
		l3_size_range			: 14; // L3 cache size range
};

/*
 * Leaf 0x80000007
 * CPU power management (mostly AMD) and AMD RAS
 */

struct leaf_0x80000007_0 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32	mca_overflow_recovery		:  1, // MCA overflow conditions not fatal
		succor				:  1, // Software containment of uncorrectable errors
		hw_assert			:  1, // Hardware assert MSRs
		scalable_mca			:  1, // Scalable MCA (MCAX MSRs)
						: 28; // Reserved
	// ecx
	u32	cpu_pwr_sample_ratio		: 32; // CPU power sample time ratio
	// edx
	u32	digital_temp			:  1, // Digital temperature sensor
		powernow_freq_id		:  1, // PowerNOW! frequency scaling
		powernow_volt_id		:  1, // PowerNOW! voltage scaling
		thermal_trip			:  1, // THERMTRIP (Thermal Trip)
		hw_thermal_control		:  1, // Hardware thermal control
		sw_thermal_control		:  1, // Software thermal control
		_100mhz_steps			:  1, // 100 MHz multiplier control
		hw_pstate			:  1, // Hardware P-state control
		constant_tsc			:  1, // TSC ticks at constant rate across all P and C states
		core_perf_boost			:  1, // Core performance boost
		eff_freq_ro			:  1, // Read-only effective frequency interface
		proc_feedback			:  1, // Processor feedback interface (deprecated)
		proc_power_reporting		:  1, // Processor power reporting interface
		connected_standby		:  1, // CPU Connected Standby support
		rapl_interface			:  1, // Runtime Average Power Limit interface
						: 17; // Reserved
};

/*
 * Leaf 0x80000008
 * CPU capacity parameters and extended feature flags (mostly AMD)
 */

struct leaf_0x80000008_0 {
	// eax
	u32	phys_addr_bits			:  8, // Max physical address bits
		virt_addr_bits			:  8, // Max virtual address bits
		guest_phys_addr_bits		:  8, // Max nested-paging guest physical address bits
						:  8; // Reserved
	// ebx
	u32	clzero				:  1, // CLZERO instruction
		insn_retired_perf		:  1, // Instruction retired counter MSR
		xsave_err_ptr			:  1, // XSAVE/XRSTOR always saves/restores FPU error pointers
		invlpgb				:  1, // INVLPGB broadcasts a TLB invalidate
		rdpru				:  1, // RDPRU (Read Processor Register at User level)
						:  1, // Reserved
		mba				:  1, // Memory Bandwidth Allocation (AMD bit)
						:  1, // Reserved
		mcommit				:  1, // MCOMMIT instruction
		wbnoinvd			:  1, // WBNOINVD instruction
						:  2, // Reserved
		ibpb				:  1, // Indirect Branch Prediction Barrier
		wbinvd_int			:  1, // Interruptible WBINVD/WBNOINVD
		ibrs				:  1, // Indirect Branch Restricted Speculation
		stibp				:  1, // Single Thread Indirect Branch Prediction mode
		ibrs_always_on			:  1, // IBRS always-on preferred
		stibp_always_on			:  1, // STIBP always-on preferred
		ibrs_fast			:  1, // IBRS is preferred over software solution
		ibrs_same_mode			:  1, // IBRS provides same mode protection
		no_efer_lmsle			:  1, // Long-Mode Segment Limit Enable unsupported
		tlb_flush_nested		:  1, // INVLPGB RAX[5] bit can be set
						:  1, // Reserved
		amd_ppin			:  1, // Protected Processor Inventory Number
		amd_ssbd			:  1, // Speculative Store Bypass Disable
		virt_ssbd			:  1, // virtualized SSBD (Speculative Store Bypass Disable)
		amd_ssb_no			:  1, // SSBD is not needed (fixed in hardware)
		cppc				:  1, // Collaborative Processor Performance Control
		amd_psfd			:  1, // Predictive Store Forward Disable
		btc_no				:  1, // CPU not affected by Branch Type Confusion
		ibpb_ret			:  1, // IBPB clears RSB/RAS too
		branch_sampling			:  1; // Branch Sampling
	// ecx
	u32	cpu_nthreads			:  8, // Number of physical threads - 1
						:  4, // Reserved
		apicid_coreid_len		:  4, // Number of thread core ID bits (shift) in APIC ID
		perf_tsc_len			:  2, // Performance time-stamp counter size
						: 14; // Reserved
	// edx
	u32	invlpgb_max_pages		: 16, // INVLPGB maximum page count
		rdpru_max_reg_id		: 16; // RDPRU max register ID (ECX input)
};

/*
 * Leaf 0x8000000a
 * AMD SVM (Secure Virtual Machine)
 */

struct leaf_0x8000000a_0 {
	// eax
	u32	svm_version			:  8, // SVM revision number
						: 24; // Reserved
	// ebx
	u32	svm_nasid			: 32; // Number of address space identifiers (ASID)
	// ecx
	u32					:  4, // Reserved
		pml				:  1, // Page Modification Logging (PML)
						: 27; // Reserved
	// edx
	u32	nested_pt			:  1, // Nested paging
		lbr_virt			:  1, // LBR virtualization
		svm_lock			:  1, // SVM lock
		nrip_save			:  1, // NRIP save support on #VMEXIT
		tsc_rate_msr			:  1, // MSR-based TSC rate control
		vmcb_clean			:  1, // VMCB clean bits support
		flush_by_asid			:  1, // Flush by ASID + Extended VMCB TLB_Control
		decode_assists			:  1, // Decode Assists support
						:  2, // Reserved
		pause_filter			:  1, // Pause intercept filter
						:  1, // Reserved
		pf_threshold			:  1, // Pause filter threshold
		avic				:  1, // Advanced virtual interrupt controller
						:  1, // Reserved
		v_vmsave_vmload			:  1, // Virtual VMSAVE/VMLOAD (nested virtualization)
		v_gif				:  1, // Virtualize the Global Interrupt Flag
		gmet				:  1, // Guest mode execution trap
		x2avic				:  1, // Virtual x2APIC
		sss_check			:  1, // Supervisor Shadow Stack restrictions
		v_spec_ctrl			:  1, // Virtual SPEC_CTRL
		ro_gpt				:  1, // Read-Only guest page table support
						:  1, // Reserved
		h_mce_override			:  1, // Host MCE override
		tlbsync_int			:  1, // TLBSYNC intercept + INVLPGB/TLBSYNC in VMCB
		nmi_virt			:  1, // NMI virtualization
		ibs_virt			:  1, // IBS Virtualization
		ext_lvt_off_chg			:  1, // Extended LVT offset fault change
		svme_addr_chk			:  1, // Guest SVME address check
						:  3; // Reserved
};

/*
 * Leaf 0x80000019
 * AMD TLB characteristics for 1GB pages
 */

struct leaf_0x80000019_0 {
	// eax
	u32	l1_itlb_1g_nentries		: 12, // L1 iTLB #entries, 1G pages
		l1_itlb_1g_assoc		:  4, // L1 iTLB associativity, 1G pages
		l1_dtlb_1g_nentries		: 12, // L1 dTLB #entries, 1G pages
		l1_dtlb_1g_assoc		:  4; // L1 dTLB associativity, 1G pages
	// ebx
	u32	l2_itlb_1g_nentries		: 12, // L2 iTLB #entries, 1G pages
		l2_itlb_1g_assoc		:  4, // L2 iTLB associativity, 1G pages
		l2_dtlb_1g_nentries		: 12, // L2 dTLB #entries, 1G pages
		l2_dtlb_1g_assoc		:  4; // L2 dTLB associativity, 1G pages
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x8000001a
 * AMD instruction optimizations
 */

struct leaf_0x8000001a_0 {
	// eax
	u32	fp_128				:  1, // Internal FP/SIMD exec data path is 128-bits wide
		movu_preferred			:  1, // SSE: MOVU* better than MOVL*/MOVH*
		fp_256				:  1, // Internal FP/SSE exec data path is 256-bits wide
						: 29; // Reserved
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x8000001b
 * AMD IBS (Instruction-Based Sampling)
 */

struct leaf_0x8000001b_0 {
	// eax
	u32	ibs_flags			:  1, // IBS feature flags
		ibs_fetch_sampling		:  1, // IBS fetch sampling
		ibs_op_sampling			:  1, // IBS execution sampling
		ibs_rdwr_op_counter		:  1, // IBS read/write of op counter
		ibs_op_count			:  1, // IBS OP counting mode
		ibs_branch_target		:  1, // IBS branch target address reporting
		ibs_op_counters_ext		:  1, // IBS IbsOpCurCnt/IbsOpMaxCnt extend by 7 bits
		ibs_rip_invalid_chk		:  1, // IBS invalid RIP indication
		ibs_op_branch_fuse		:  1, // IBS fused branch micro-op indication
		ibs_fetch_ctl_ext		:  1, // IBS Fetch Control Extended MSR
		ibs_op_data_4			:  1, // IBS op data 4 MSR
		ibs_l3_miss_filter		:  1, // IBS L3-miss filtering (Zen4+)
						: 20; // Reserved
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x8000001c
 * AMD LWP (Lightweight Profiling)
 */

struct leaf_0x8000001c_0 {
	// eax
	u32	os_lwp_avail			:  1, // OS: LWP is available to application programs
		os_lwpval			:  1, // OS: LWPVAL instruction
		os_lwp_ire			:  1, // OS: Instructions Retired Event
		os_lwp_bre			:  1, // OS: Branch Retired Event
		os_lwp_dme			:  1, // OS: Dcache Miss Event
		os_lwp_cnh			:  1, // OS: CPU Clocks Not Halted event
		os_lwp_rnh			:  1, // OS: CPU Reference clocks Not Halted event
						: 22, // Reserved
		os_lwp_cont			:  1, // OS: LWP sampling in continuous mode
		os_lwp_ptsc			:  1, // OS: Performance Time Stamp Counter in event records
		os_lwp_int			:  1; // OS: Interrupt on threshold overflow
	// ebx
	u32	lwp_lwpcb_sz			:  8, // Control Block size, in quadwords
		lwp_event_sz			:  8, // Event record size, in bytes
		lwp_max_events			:  8, // Max EventID supported
		lwp_event_offset		:  8; // Control Block events area offset
	// ecx
	u32	lwp_latency_max			:  5, // Cache latency counters number of bits
		lwp_data_addr			:  1, // Cache miss events report data cache address
		lwp_latency_rnd			:  3, // Cache latency rounding amount
		lwp_version			:  7, // LWP version
		lwp_buf_min_sz			:  8, // LWP event ring buffer min size, 32 event record units
						:  4, // Reserved
		lwp_branch_predict		:  1, // Branches Retired events can be filtered
		lwp_ip_filtering		:  1, // IP filtering (IPI, IPF, BaseIP, and LimitIP @ LWPCP)
		lwp_cache_levels		:  1, // Cache-related events: filter by cache level
		lwp_cache_latency		:  1; // Cache-related events: filter by latency
	// edx
	u32	hw_lwp_avail			:  1, // HW: LWP available
		hw_lwpval			:  1, // HW: LWPVAL available
		hw_lwp_ire			:  1, // HW: Instructions Retired Event
		hw_lwp_bre			:  1, // HW: Branch Retired Event
		hw_lwp_dme			:  1, // HW: Dcache Miss Event
		hw_lwp_cnh			:  1, // HW: Clocks Not Halted event
		hw_lwp_rnh			:  1, // HW: Reference clocks Not Halted event
						: 22, // Reserved
		hw_lwp_cont			:  1, // HW: LWP sampling in continuous mode
		hw_lwp_ptsc			:  1, // HW: Performance Time Stamp Counter in event records
		hw_lwp_int			:  1; // HW: Interrupt on threshold overflow
};

/*
 * Leaf 0x8000001d
 * AMD deterministic cache parameters
 */

struct leaf_0x8000001d_n {
	// eax
	u32	cache_type			:  5, // Cache type field
		cache_level			:  3, // Cache level (1-based)
		cache_self_init			:  1, // Self-initializing cache level
		fully_associative		:  1, // Fully-associative cache
						:  4, // Reserved
		num_threads_sharing		: 12, // Number of logical CPUs sharing cache
						:  6; // Reserved
	// ebx
	u32	cache_linesize			: 12, // System coherency line size (0-based)
		cache_npartitions		: 10, // Physical line partitions (0-based)
		cache_nways			: 10; // Ways of associativity (0-based)
	// ecx
	u32	cache_nsets			: 31, // Cache number of sets (0-based)
						:  1; // Reserved
	// edx
	u32	wbinvd_rll_no_guarantee		:  1, // WBINVD/INVD not guaranteed for Remote Lower-Level caches
		ll_inclusive			:  1, // Cache is inclusive of Lower-Level caches
						: 30; // Reserved
};

#define LEAF_0x8000001d_SUBLEAF_N_FIRST		0
#define LEAF_0x8000001d_SUBLEAF_N_LAST		31

/*
 * Leaf 0x8000001e
 * AMD CPU topology
 */

struct leaf_0x8000001e_0 {
	// eax
	u32	ext_apic_id			: 32; // Extended APIC ID
	// ebx
	u32	core_id				:  8, // Unique per-socket logical core unit ID
		core_nthreads			:  8, // #Threads per core (zero-based)
						: 16; // Reserved
	// ecx
	u32	node_id				:  8, // Node (die) ID of invoking logical CPU
		nnodes_per_socket		:  3, // #nodes in invoking logical CPU's package/socket
						: 21; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x8000001f
 * AMD encrypted memory capabilities (SME/SEV)
 */

struct leaf_0x8000001f_0 {
	// eax
	u32	sme				:  1, // Secure Memory Encryption
		sev				:  1, // Secure Encrypted Virtualization
		vm_page_flush			:  1, // VM Page Flush MSR
		sev_encrypted_state		:  1, // SEV Encrypted State
		sev_nested_paging		:  1, // SEV secure nested paging
		vm_permission_levels		:  1, // VMPL
		rpmquery			:  1, // RPMQUERY instruction
		vmpl_sss			:  1, // VMPL supervisor shadow stack
		secure_tsc			:  1, // Secure TSC
		virt_tsc_aux			:  1, // Hardware virtualizes TSC_AUX
		sme_coherent			:  1, // Cache coherency enforcement across encryption domains
		req_64bit_hypervisor		:  1, // SEV guest mandates 64-bit hypervisor
		restricted_injection		:  1, // Restricted Injection supported
		alternate_injection		:  1, // Alternate Injection supported
		debug_swap			:  1, // SEV-ES: Full debug state swap
		disallow_host_ibs		:  1, // SEV-ES: Disallowing IBS use by the host
		virt_transparent_enc		:  1, // Virtual Transparent Encryption
		vmgexit_parameter		:  1, // SEV_FEATURES: VmgexitParameter
		virt_tom_msr			:  1, // Virtual TOM MSR
		virt_ibs			:  1, // SEV-ES guests: IBS state virtualization
						:  4, // Reserved
		vmsa_reg_protection		:  1, // VMSA register protection
		smt_protection			:  1, // SMT protection
						:  2, // Reserved
		svsm_page_msr			:  1, // SVSM communication page MSR
		nested_virt_snp_msr		:  1, // VIRT_RMPUPDATE/VIRT_PSMASH MSRs
						:  2; // Reserved
	// ebx
	u32	pte_cbit_pos			:  6, // PTE bit number to enable memory encryption
		phys_addr_reduction_nbits	:  6, // Reduction of phys address space in bits
		vmpl_count			:  4, // Number of VM permission levels (VMPL)
						: 16; // Reserved
	// ecx
	u32	enc_guests_max			: 32; // Max number of simultaneous encrypted guests
	// edx
	u32	min_sev_asid_no_sev_es		: 32; // Minimum ASID for SEV-enabled SEV-ES-disabled guest
};

/*
 * Leaf 0x80000020
 * AMD PQoS (Platform QoS) extended features
 */

struct leaf_0x80000020_0 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32					:  1, // Reserved
		mba				:  1, // Memory Bandwidth Allocation support
		smba				:  1, // Slow Memory Bandwidth Allocation support
		bmec				:  1, // Bandwidth Monitoring Event Configuration support
		l3rr				:  1, // L3 Range Reservation support
		abmc				:  1, // Assignable Bandwidth Monitoring Counters
		sdciae				:  1, // Smart Data Cache Injection (SDCI) Allocation Enforcement
						: 25; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

struct leaf_0x80000020_1 {
	// eax
	u32	mba_limit_len			: 32; // MBA enforcement limit size
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32	mba_cos_max			: 32; // MBA max Class of Service number (zero-based)
};

struct leaf_0x80000020_2 {
	// eax
	u32	smba_limit_len			: 32; // SMBA enforcement limit size
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32	smba_cos_max			: 32; // SMBA max Class of Service number (zero-based)
};

struct leaf_0x80000020_3 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32	bmec_num_events			:  8, // BMEC number of bandwidth events available
						: 24; // Reserved
	// ecx
	u32	bmec_local_reads		:  1, // Local NUMA reads can be tracked
		bmec_remote_reads		:  1, // Remote NUMA reads can be tracked
		bmec_local_nontemp_wr		:  1, // Local NUMA non-temporal writes can be tracked
		bmec_remote_nontemp_wr		:  1, // Remote NUMA non-temporal writes can be tracked
		bmec_local_slow_mem_rd		:  1, // Local NUMA slow-memory reads can be tracked
		bmec_remote_slow_mem_rd		:  1, // Remote NUMA slow-memory reads can be tracked
		bmec_all_dirty_victims		:  1, // Dirty QoS victims to all types of memory can be tracked
						: 25; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x80000021
 * AMD extended CPU features 2
 */

struct leaf_0x80000021_0 {
	// eax
	u32	no_nested_data_bp		:  1, // No nested data breakpoints
		fsgs_non_serializing		:  1, // WRMSR to {FS,GS,KERNEL_GS}_BASE is non-serializing
		lfence_serializing		:  1, // LFENCE always serializing / synchronizes RDTSC
		smm_page_cfg_lock		:  1, // SMM paging configuration lock
						:  2, // Reserved
		null_sel_clr_base		:  1, // Null selector clears base
		upper_addr_ignore		:  1, // EFER MSR Upper Address Ignore
		auto_ibrs			:  1, // EFER MSR Automatic IBRS
		no_smm_ctl_msr			:  1, // SMM_CTL MSR not available
		fsrs				:  1, // Fast Short REP STOSB
		fsrc				:  1, // Fast Short REP CMPSB
						:  1, // Reserved
		prefetch_ctl_msr		:  1, // Prefetch control MSR
						:  2, // Reserved
		opcode_reclaim			:  1, // Reserves opcode space
		user_cpuid_disable		:  1, // #GP when executing CPUID at CPL > 0
		epsf				:  1, // Enhanced Predictive Store Forwarding
						:  3, // Reserved
		wl_feedback			:  1, // Workload-based heuristic feedback to OS
						:  1, // Reserved
		eraps				:  1, // Enhanced Return Address Predictor Security
						:  2, // Reserved
		sbpb				:  1, // Selective Branch Predictor Barrier
		ibpb_brtype			:  1, // Branch predictions flushed from CPU branch predictor
		srso_no				:  1, // No SRSO vulnerability
		srso_uk_no			:  1, // No SRSO at user-kernel boundary
		srso_msr_fix			:  1; // MSR BP_CFG[BpSpecReduce] SRSO mitigation
	// ebx
	u32	microcode_patch_size		: 16, // Microcode patch size, in 16-byte units
		rap_size			:  8, // Return Address Predictor size
						:  8; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x80000022
 * AMD extended performance monitoring
 */

struct leaf_0x80000022_0 {
	// eax
	u32	perfmon_v2			:  1, // Performance monitoring v2
		lbr_v2				:  1, // Last Branch Record v2 extensions (LBR Stack)
		lbr_pmc_freeze			:  1, // Freezing core performance counters / LBR Stack
						: 29; // Reserved
	// ebx
	u32	n_pmc_core			:  4, // Number of core performance counters
		lbr_v2_stack_size		:  6, // Number of LBR stack entries
		n_pmc_northbridge		:  6, // Number of northbridge performance counters
		n_pmc_umc			:  6, // Number of UMC performance counters
						: 10; // Reserved
	// ecx
	u32	active_umc_bitmask		: 32; // Active UMCs bitmask
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x80000023
 * AMD multi-key encrypted memory
 */

struct leaf_0x80000023_0 {
	// eax
	u32	mem_hmk_mode			:  1, // MEM-HMK encryption mode
						: 31; // Reserved
	// ebx
	u32	mem_hmk_avail_keys		: 16, // Total number of available encryption keys
						: 16; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x80000026
 * AMD extended CPU topology
 */

struct leaf_0x80000026_n {
	// eax
	u32	x2apic_id_shift			:  5, // Bit width of this level (previous levels inclusive)
						: 24, // Reserved
		core_has_pwreff_ranking		:  1, // This core has a power efficiency ranking
		domain_has_hybrid_cores		:  1, // This domain level has hybrid (E, P) cores
		domain_core_count_asymm		:  1; // The 'Core' domain has asymmetric cores count
	// ebx
	u32	domain_lcpus_count		: 16, // Number of logical CPUs at this domain instance
		core_pwreff_ranking		:  8, // This core's static power efficiency ranking
		core_native_model_id		:  4, // This core's native model ID
		core_type			:  4; // This core's type
	// ecx
	u32	domain_level			:  8, // This domain level (subleaf ID)
		domain_type			:  8, // This domain type
						: 16; // Reserved
	// edx
	u32	x2apic_id			: 32; // x2APIC ID of current logical CPU
};

#define LEAF_0x80000026_SUBLEAF_N_FIRST		0
#define LEAF_0x80000026_SUBLEAF_N_LAST		3

/*
 * Leaf 0x80860000
 * Maximum Transmeta leaf + CPU vendor string
 */

struct leaf_0x80860000_0 {
	// eax
	u32	max_tra_leaf			: 32; // Maximum Transmeta leaf
	// ebx
	u32	cpu_vendorid_0			: 32; // Transmeta vendor ID string bytes 0 - 3
	// ecx
	u32	cpu_vendorid_2			: 32; // Transmeta vendor ID string bytes 8 - 11
	// edx
	u32	cpu_vendorid_1			: 32; // Transmeta vendor ID string bytes 4 - 7
};

/*
 * Leaf 0x80860001
 * Transmeta extended CPU features
 */

struct leaf_0x80860001_0 {
	// eax
	u32	stepping			:  4, // Stepping ID
		base_model			:  4, // Base CPU model ID
		base_family_id			:  4, // Base CPU family ID
		cpu_type			:  2, // CPU type
						: 18; // Reserved
	// ebx
	u32	cpu_rev_mask_minor		:  8, // CPU revision ID, mask minor
		cpu_rev_mask_major		:  8, // CPU revision ID, mask major
		cpu_rev_minor			:  8, // CPU revision ID, minor
		cpu_rev_major			:  8; // CPU revision ID, major
	// ecx
	u32	cpu_base_mhz			: 32; // CPU nominal frequency, in MHz
	// edx
	u32	recovery			:  1, // Recovery CMS is active (after bad flush)
		longrun				:  1, // LongRun power management capabilities
						:  1, // Reserved
		lrti				:  1, // LongRun Table Interface
						: 28; // Reserved
};

/*
 * Leaf 0x80860002
 * Transmeta CMS (Code Morphing Software)
 */

struct leaf_0x80860002_0 {
	// eax
	u32	cpu_rev_id			: 32; // CPU revision ID
	// ebx
	u32	cms_rev_mask_2			:  8, // CMS revision ID, mask component 2
		cms_rev_mask_1			:  8, // CMS revision ID, mask component 1
		cms_rev_minor			:  8, // CMS revision ID, minor
		cms_rev_major			:  8; // CMS revision ID, major
	// ecx
	u32	cms_rev_mask_3			: 32; // CMS revision ID, mask component 3
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0x80860003
 * Transmeta CPU information string, bytes 0 - 15
 */

struct leaf_0x80860003_0 {
	// eax
	u32	cpu_info_0			: 32; // CPU info string bytes 0 - 3
	// ebx
	u32	cpu_info_1			: 32; // CPU info string bytes 4 - 7
	// ecx
	u32	cpu_info_2			: 32; // CPU info string bytes 8 - 11
	// edx
	u32	cpu_info_3			: 32; // CPU info string bytes 12 - 15
};

/*
 * Leaf 0x80860004
 * Transmeta CPU information string, bytes 16 - 31
 */

struct leaf_0x80860004_0 {
	// eax
	u32	cpu_info_4			: 32; // CPU info string bytes 16 - 19
	// ebx
	u32	cpu_info_5			: 32; // CPU info string bytes 20 - 23
	// ecx
	u32	cpu_info_6			: 32; // CPU info string bytes 24 - 27
	// edx
	u32	cpu_info_7			: 32; // CPU info string bytes 28 - 31
};

/*
 * Leaf 0x80860005
 * Transmeta CPU information string, bytes 32 - 47
 */

struct leaf_0x80860005_0 {
	// eax
	u32	cpu_info_8			: 32; // CPU info string bytes 32 - 35
	// ebx
	u32	cpu_info_9			: 32; // CPU info string bytes 36 - 39
	// ecx
	u32	cpu_info_10			: 32; // CPU info string bytes 40 - 43
	// edx
	u32	cpu_info_11			: 32; // CPU info string bytes 44 - 47
};

/*
 * Leaf 0x80860006
 * Transmeta CPU information string, bytes 48 - 63
 */

struct leaf_0x80860006_0 {
	// eax
	u32	cpu_info_12			: 32; // CPU info string bytes 48 - 51
	// ebx
	u32	cpu_info_13			: 32; // CPU info string bytes 52 - 55
	// ecx
	u32	cpu_info_14			: 32; // CPU info string bytes 56 - 59
	// edx
	u32	cpu_info_15			: 32; // CPU info string bytes 60 - 63
};

/*
 * Leaf 0x80860007
 * Transmeta live CPU information
 */

struct leaf_0x80860007_0 {
	// eax
	u32	cpu_cur_mhz			: 32; // Current CPU frequency, in MHz
	// ebx
	u32	cpu_cur_voltage			: 32; // Current CPU voltage, in millivolts
	// ecx
	u32	cpu_cur_perf_pctg		: 32; // Current CPU performance percentage, 0 - 100
	// edx
	u32	cpu_cur_gate_delay		: 32; // Current CPU gate delay, in femtoseconds
};

/*
 * Leaf 0xc0000000
 * Maximum Centaur/Zhaoxin leaf
 */

struct leaf_0xc0000000_0 {
	// eax
	u32	max_cntr_leaf			: 32; // Maximum Centaur/Zhaoxin leaf
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32					: 32; // Reserved
};

/*
 * Leaf 0xc0000001
 * Centaur/Zhaoxin extended CPU features
 */

struct leaf_0xc0000001_0 {
	// eax
	u32					: 32; // Reserved
	// ebx
	u32					: 32; // Reserved
	// ecx
	u32					: 32; // Reserved
	// edx
	u32	ccs_sm2				:  1, // CCS SM2 instructions
		ccs_sm2_en			:  1, // CCS SM2 enabled
		rng				:  1, // Random Number Generator
		rng_en				:  1, // RNG enabled
		ccs_sm3_sm4			:  1, // CCS SM3 and SM4 instructions
		ccs_sm3_sm4_en			:  1, // CCS SM3/SM4 enabled
		ace				:  1, // Advanced Cryptography Engine
		ace_en				:  1, // ACE enabled
		ace2				:  1, // Advanced Cryptography Engine v2
		ace2_en				:  1, // ACE v2 enabled
		phe				:  1, // PadLock Hash Engine
		phe_en				:  1, // PHE enabled
		pmm				:  1, // PadLock Montgomery Multiplier
		pmm_en				:  1, // PMM enabled
						:  2, // Reserved
		parallax			:  1, // Parallax auto adjust processor voltage
		parallax_en			:  1, // Parallax enabled
						:  2, // Reserved
		tm3				:  1, // Thermal Monitor v3
		tm3_en				:  1, // TM v3 enabled
						:  3, // Reserved
		phe2				:  1, // PadLock Hash Engine v2 (SHA384/SHA512)
		phe2_en				:  1, // PHE v2 enabled
		rsa				:  1, // RSA instructions (XMODEXP/MONTMUL2)
		rsa_en				:  1, // RSA instructions enabled
						:  3; // Reserved
};

#endif /* _ASM_X86_CPUID_LEAF_TYPES */
