/*
 * LMC Registers, see chapter 2.5
 *
 * These are RSL Type registers and are accessed indirectly across the
 * I/O bus, so accesses are slowish.  Not that it matters.  Any size load is
 * ok but stores must be 64-bit.
 */
#define LMC_BASE		0x0001180088000000
#define LMC_SIZE		0xb8

#define LMC_MEM_CFG0		0x0000000000000000
#define LMC_MEM_CFG1		0x0000000000000008
#define LMC_CTL			0x0000000000000010
#define LMC_DDR2_CTL		0x0000000000000018
#define LMC_FADR		0x0000000000000020
#define   LMC_FADR_FDIMM
#define   LMC_FADR_FBUNK
#define   LMC_FADR_FBANK
#define   LMC_FADR_FROW
#define   LMC_FADR_FCOL
#define LMC_COMP_CTL		0x0000000000000028
#define LMC_WODT_CTL		0x0000000000000030
#define LMC_ECC_SYND		0x0000000000000038
#define LMC_IFB_CNT_LO		0x0000000000000048
#define LMC_IFB_CNT_HI		0x0000000000000050
#define LMC_OPS_CNT_LO		0x0000000000000058
#define LMC_OPS_CNT_HI		0x0000000000000060
#define LMC_DCLK_CNT_LO		0x0000000000000068
#define LMC_DCLK_CNT_HI		0x0000000000000070
#define LMC_DELAY_CFG		0x0000000000000088
#define LMC_CTL1		0x0000000000000090
#define LMC_DUAL_MEM_CONFIG	0x0000000000000098
#define LMC_RODT_COMP_CTL	0x00000000000000A0
#define LMC_PLL_CTL		0x00000000000000A8
#define LMC_PLL_STATUS		0x00000000000000B0

union lmc_mem_cfg0 {
	uint64_t u64;
	struct {
		uint64_t reserved_32_63:32;
		uint64_t reset:1;
		uint64_t silo_qc:1;
		uint64_t bunk_ena:1;
		uint64_t ded_err:4;
		uint64_t sec_err:4;
		uint64_t intr_ded_ena:1;
		uint64_t intr_sec_ena:1;
		uint64_t reserved_15_18:4;
		uint64_t ref_int:5;
		uint64_t pbank_lsb:4;
		uint64_t row_lsb:3;
		uint64_t ecc_ena:1;
		uint64_t init_start:1;
	};
};

union lmc_fadr {
	uint64_t u64;
	struct {
		uint64_t reserved_32_63:32;
		uint64_t fdimm:2;
		uint64_t fbunk:1;
		uint64_t fbank:3;
		uint64_t frow:14;
		uint64_t fcol:12;
	};
};

union lmc_ecc_synd {
	uint64_t u64;
	struct {
		uint64_t reserved_32_63:32;
		uint64_t mrdsyn3:8;
		uint64_t mrdsyn2:8;
		uint64_t mrdsyn1:8;
		uint64_t mrdsyn0:8;
	};
};
