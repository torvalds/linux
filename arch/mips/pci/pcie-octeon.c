/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007, 2008 Cavium Networks
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/cvmx-npei-defs.h>
#include <asm/octeon/cvmx-pciercx-defs.h>
#include <asm/octeon/cvmx-pescx-defs.h>
#include <asm/octeon/cvmx-pexp-defs.h>
#include <asm/octeon/cvmx-helper-errata.h>
#include <asm/octeon/pci-octeon.h>

union cvmx_pcie_address {
	uint64_t u64;
	struct {
		uint64_t upper:2;	/* Normally 2 for XKPHYS */
		uint64_t reserved_49_61:13;	/* Must be zero */
		uint64_t io:1;	/* 1 for IO space access */
		uint64_t did:5;	/* PCIe DID = 3 */
		uint64_t subdid:3;	/* PCIe SubDID = 1 */
		uint64_t reserved_36_39:4;	/* Must be zero */
		uint64_t es:2;	/* Endian swap = 1 */
		uint64_t port:2;	/* PCIe port 0,1 */
		uint64_t reserved_29_31:3;	/* Must be zero */
		/*
		 * Selects the type of the configuration request (0 = type 0,
		 * 1 = type 1).
		 */
		uint64_t ty:1;
		/* Target bus number sent in the ID in the request. */
		uint64_t bus:8;
		/*
		 * Target device number sent in the ID in the
		 * request. Note that Dev must be zero for type 0
		 * configuration requests.
		 */
		uint64_t dev:5;
		/* Target function number sent in the ID in the request. */
		uint64_t func:3;
		/*
		 * Selects a register in the configuration space of
		 * the target.
		 */
		uint64_t reg:12;
	} config;
	struct {
		uint64_t upper:2;	/* Normally 2 for XKPHYS */
		uint64_t reserved_49_61:13;	/* Must be zero */
		uint64_t io:1;	/* 1 for IO space access */
		uint64_t did:5;	/* PCIe DID = 3 */
		uint64_t subdid:3;	/* PCIe SubDID = 2 */
		uint64_t reserved_36_39:4;	/* Must be zero */
		uint64_t es:2;	/* Endian swap = 1 */
		uint64_t port:2;	/* PCIe port 0,1 */
		uint64_t address:32;	/* PCIe IO address */
	} io;
	struct {
		uint64_t upper:2;	/* Normally 2 for XKPHYS */
		uint64_t reserved_49_61:13;	/* Must be zero */
		uint64_t io:1;	/* 1 for IO space access */
		uint64_t did:5;	/* PCIe DID = 3 */
		uint64_t subdid:3;	/* PCIe SubDID = 3-6 */
		uint64_t reserved_36_39:4;	/* Must be zero */
		uint64_t address:36;	/* PCIe Mem address */
	} mem;
};

#include <dma-coherence.h>

/**
 * Return the Core virtual base address for PCIe IO access. IOs are
 * read/written as an offset from this address.
 *
 * @pcie_port: PCIe port the IO is for
 *
 * Returns 64bit Octeon IO base address for read/write
 */
static inline uint64_t cvmx_pcie_get_io_base_address(int pcie_port)
{
	union cvmx_pcie_address pcie_addr;
	pcie_addr.u64 = 0;
	pcie_addr.io.upper = 0;
	pcie_addr.io.io = 1;
	pcie_addr.io.did = 3;
	pcie_addr.io.subdid = 2;
	pcie_addr.io.es = 1;
	pcie_addr.io.port = pcie_port;
	return pcie_addr.u64;
}

/**
 * Size of the IO address region returned at address
 * cvmx_pcie_get_io_base_address()
 *
 * @pcie_port: PCIe port the IO is for
 *
 * Returns Size of the IO window
 */
static inline uint64_t cvmx_pcie_get_io_size(int pcie_port)
{
	return 1ull << 32;
}

/**
 * Return the Core virtual base address for PCIe MEM access. Memory is
 * read/written as an offset from this address.
 *
 * @pcie_port: PCIe port the IO is for
 *
 * Returns 64bit Octeon IO base address for read/write
 */
static inline uint64_t cvmx_pcie_get_mem_base_address(int pcie_port)
{
	union cvmx_pcie_address pcie_addr;
	pcie_addr.u64 = 0;
	pcie_addr.mem.upper = 0;
	pcie_addr.mem.io = 1;
	pcie_addr.mem.did = 3;
	pcie_addr.mem.subdid = 3 + pcie_port;
	return pcie_addr.u64;
}

/**
 * Size of the Mem address region returned at address
 * cvmx_pcie_get_mem_base_address()
 *
 * @pcie_port: PCIe port the IO is for
 *
 * Returns Size of the Mem window
 */
static inline uint64_t cvmx_pcie_get_mem_size(int pcie_port)
{
	return 1ull << 36;
}

/**
 * Read a PCIe config space register indirectly. This is used for
 * registers of the form PCIEEP_CFG??? and PCIERC?_CFG???.
 *
 * @pcie_port:  PCIe port to read from
 * @cfg_offset: Address to read
 *
 * Returns Value read
 */
static uint32_t cvmx_pcie_cfgx_read(int pcie_port, uint32_t cfg_offset)
{
	union cvmx_pescx_cfg_rd pescx_cfg_rd;
	pescx_cfg_rd.u64 = 0;
	pescx_cfg_rd.s.addr = cfg_offset;
	cvmx_write_csr(CVMX_PESCX_CFG_RD(pcie_port), pescx_cfg_rd.u64);
	pescx_cfg_rd.u64 = cvmx_read_csr(CVMX_PESCX_CFG_RD(pcie_port));
	return pescx_cfg_rd.s.data;
}

/**
 * Write a PCIe config space register indirectly. This is used for
 * registers of the form PCIEEP_CFG??? and PCIERC?_CFG???.
 *
 * @pcie_port:  PCIe port to write to
 * @cfg_offset: Address to write
 * @val:        Value to write
 */
static void cvmx_pcie_cfgx_write(int pcie_port, uint32_t cfg_offset,
				 uint32_t val)
{
	union cvmx_pescx_cfg_wr pescx_cfg_wr;
	pescx_cfg_wr.u64 = 0;
	pescx_cfg_wr.s.addr = cfg_offset;
	pescx_cfg_wr.s.data = val;
	cvmx_write_csr(CVMX_PESCX_CFG_WR(pcie_port), pescx_cfg_wr.u64);
}

/**
 * Build a PCIe config space request address for a device
 *
 * @pcie_port: PCIe port to access
 * @bus:       Sub bus
 * @dev:       Device ID
 * @fn:        Device sub function
 * @reg:       Register to access
 *
 * Returns 64bit Octeon IO address
 */
static inline uint64_t __cvmx_pcie_build_config_addr(int pcie_port, int bus,
						     int dev, int fn, int reg)
{
	union cvmx_pcie_address pcie_addr;
	union cvmx_pciercx_cfg006 pciercx_cfg006;

	pciercx_cfg006.u32 =
	    cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG006(pcie_port));
	if ((bus <= pciercx_cfg006.s.pbnum) && (dev != 0))
		return 0;

	pcie_addr.u64 = 0;
	pcie_addr.config.upper = 2;
	pcie_addr.config.io = 1;
	pcie_addr.config.did = 3;
	pcie_addr.config.subdid = 1;
	pcie_addr.config.es = 1;
	pcie_addr.config.port = pcie_port;
	pcie_addr.config.ty = (bus > pciercx_cfg006.s.pbnum);
	pcie_addr.config.bus = bus;
	pcie_addr.config.dev = dev;
	pcie_addr.config.func = fn;
	pcie_addr.config.reg = reg;
	return pcie_addr.u64;
}

/**
 * Read 8bits from a Device's config space
 *
 * @pcie_port: PCIe port the device is on
 * @bus:       Sub bus
 * @dev:       Device ID
 * @fn:        Device sub function
 * @reg:       Register to access
 *
 * Returns Result of the read
 */
static uint8_t cvmx_pcie_config_read8(int pcie_port, int bus, int dev,
				      int fn, int reg)
{
	uint64_t address =
	    __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
	if (address)
		return cvmx_read64_uint8(address);
	else
		return 0xff;
}

/**
 * Read 16bits from a Device's config space
 *
 * @pcie_port: PCIe port the device is on
 * @bus:       Sub bus
 * @dev:       Device ID
 * @fn:        Device sub function
 * @reg:       Register to access
 *
 * Returns Result of the read
 */
static uint16_t cvmx_pcie_config_read16(int pcie_port, int bus, int dev,
					int fn, int reg)
{
	uint64_t address =
	    __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
	if (address)
		return le16_to_cpu(cvmx_read64_uint16(address));
	else
		return 0xffff;
}

/**
 * Read 32bits from a Device's config space
 *
 * @pcie_port: PCIe port the device is on
 * @bus:       Sub bus
 * @dev:       Device ID
 * @fn:        Device sub function
 * @reg:       Register to access
 *
 * Returns Result of the read
 */
static uint32_t cvmx_pcie_config_read32(int pcie_port, int bus, int dev,
					int fn, int reg)
{
	uint64_t address =
	    __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
	if (address)
		return le32_to_cpu(cvmx_read64_uint32(address));
	else
		return 0xffffffff;
}

/**
 * Write 8bits to a Device's config space
 *
 * @pcie_port: PCIe port the device is on
 * @bus:       Sub bus
 * @dev:       Device ID
 * @fn:        Device sub function
 * @reg:       Register to access
 * @val:       Value to write
 */
static void cvmx_pcie_config_write8(int pcie_port, int bus, int dev, int fn,
				    int reg, uint8_t val)
{
	uint64_t address =
	    __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
	if (address)
		cvmx_write64_uint8(address, val);
}

/**
 * Write 16bits to a Device's config space
 *
 * @pcie_port: PCIe port the device is on
 * @bus:       Sub bus
 * @dev:       Device ID
 * @fn:        Device sub function
 * @reg:       Register to access
 * @val:       Value to write
 */
static void cvmx_pcie_config_write16(int pcie_port, int bus, int dev, int fn,
				     int reg, uint16_t val)
{
	uint64_t address =
	    __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
	if (address)
		cvmx_write64_uint16(address, cpu_to_le16(val));
}

/**
 * Write 32bits to a Device's config space
 *
 * @pcie_port: PCIe port the device is on
 * @bus:       Sub bus
 * @dev:       Device ID
 * @fn:        Device sub function
 * @reg:       Register to access
 * @val:       Value to write
 */
static void cvmx_pcie_config_write32(int pcie_port, int bus, int dev, int fn,
				     int reg, uint32_t val)
{
	uint64_t address =
	    __cvmx_pcie_build_config_addr(pcie_port, bus, dev, fn, reg);
	if (address)
		cvmx_write64_uint32(address, cpu_to_le32(val));
}

/**
 * Initialize the RC config space CSRs
 *
 * @pcie_port: PCIe port to initialize
 */
static void __cvmx_pcie_rc_initialize_config_space(int pcie_port)
{
	union cvmx_pciercx_cfg030 pciercx_cfg030;
	union cvmx_npei_ctl_status2 npei_ctl_status2;
	union cvmx_pciercx_cfg070 pciercx_cfg070;
	union cvmx_pciercx_cfg001 pciercx_cfg001;
	union cvmx_pciercx_cfg032 pciercx_cfg032;
	union cvmx_pciercx_cfg006 pciercx_cfg006;
	union cvmx_pciercx_cfg008 pciercx_cfg008;
	union cvmx_pciercx_cfg009 pciercx_cfg009;
	union cvmx_pciercx_cfg010 pciercx_cfg010;
	union cvmx_pciercx_cfg011 pciercx_cfg011;
	union cvmx_pciercx_cfg035 pciercx_cfg035;
	union cvmx_pciercx_cfg075 pciercx_cfg075;
	union cvmx_pciercx_cfg034 pciercx_cfg034;

	/* Max Payload Size (PCIE*_CFG030[MPS]) */
	/* Max Read Request Size (PCIE*_CFG030[MRRS]) */
	/* Relaxed-order, no-snoop enables (PCIE*_CFG030[RO_EN,NS_EN] */
	/* Error Message Enables (PCIE*_CFG030[CE_EN,NFE_EN,FE_EN,UR_EN]) */
	pciercx_cfg030.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG030(pcie_port));
	/*
	 * Max payload size = 128 bytes for best Octeon DMA
	 * performance.
	 */
	pciercx_cfg030.s.mps = 0;
	/*
	 * Max read request size = 128 bytes for best Octeon DMA
	 * performance.
	 */
	pciercx_cfg030.s.mrrs = 0;
	/* Enable relaxed ordering. */
	pciercx_cfg030.s.ro_en = 1;
	/* Enable no snoop. */
	pciercx_cfg030.s.ns_en = 1;
	/* Correctable error reporting enable. */
	pciercx_cfg030.s.ce_en = 1;
	/* Non-fatal error reporting enable. */
	pciercx_cfg030.s.nfe_en = 1;
	/* Fatal error reporting enable. */
	pciercx_cfg030.s.fe_en = 1;
	/* Unsupported request reporting enable. */
	pciercx_cfg030.s.ur_en = 1;
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG030(pcie_port),
			     pciercx_cfg030.u32);

	/*
	 * Max Payload Size (NPEI_CTL_STATUS2[MPS]) must match
	 * PCIE*_CFG030[MPS]
	 *
	 * Max Read Request Size (NPEI_CTL_STATUS2[MRRS]) must not
	 * exceed PCIE*_CFG030[MRRS].
	 */
	npei_ctl_status2.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_STATUS2);
	/* Max payload size = 128 bytes for best Octeon DMA performance */
	npei_ctl_status2.s.mps = 0;
	/* Max read request size = 128 bytes for best Octeon DMA performance */
	npei_ctl_status2.s.mrrs = 0;
	if (pcie_port)
		npei_ctl_status2.s.c1_b1_s = 3; /* Port1 BAR1 Size 256MB */
	else
		npei_ctl_status2.s.c0_b1_s = 3; /* Port0 BAR1 Size 256MB */
	cvmx_write_csr(CVMX_PEXP_NPEI_CTL_STATUS2, npei_ctl_status2.u64);

	/* ECRC Generation (PCIE*_CFG070[GE,CE]) */
	pciercx_cfg070.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG070(pcie_port));
	pciercx_cfg070.s.ge = 1;	/* ECRC generation enable. */
	pciercx_cfg070.s.ce = 1;	/* ECRC check enable. */
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG070(pcie_port),
			     pciercx_cfg070.u32);

	/*
	 * Access Enables (PCIE*_CFG001[MSAE,ME]) ME and MSAE should
	 * always be set.
	 *
	 * Interrupt Disable (PCIE*_CFG001[I_DIS]) System Error
	 * Message Enable (PCIE*_CFG001[SEE])
	 */
	pciercx_cfg001.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG001(pcie_port));
	pciercx_cfg001.s.msae = 1;	/* Memory space enable. */
	pciercx_cfg001.s.me = 1;	/* Bus master enable. */
	pciercx_cfg001.s.i_dis = 1;	/* INTx assertion disable. */
	pciercx_cfg001.s.see = 1;	/* SERR# enable */
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG001(pcie_port),
			pciercx_cfg001.u32);

	/* Advanced Error Recovery Message Enables */
	/* (PCIE*_CFG066,PCIE*_CFG067,PCIE*_CFG069) */
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG066(pcie_port), 0);
	/* Use CVMX_PCIERCX_CFG067 hardware default */
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG069(pcie_port), 0);

	/* Active State Power Management (PCIE*_CFG032[ASLPC]) */
	pciercx_cfg032.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG032(pcie_port));
	pciercx_cfg032.s.aslpc = 0;	/* Active state Link PM control. */
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG032(pcie_port),
			     pciercx_cfg032.u32);

	/* Entrance Latencies (PCIE*_CFG451[L0EL,L1EL]) */

	/*
	 * Link Width Mode (PCIERCn_CFG452[LME]) - Set during
	 * cvmx_pcie_rc_initialize_link()
	 *
	 * Primary Bus Number (PCIERCn_CFG006[PBNUM])
	 *
	 * We set the primary bus number to 1 so IDT bridges are
	 * happy. They don't like zero.
	 */
	pciercx_cfg006.u32 = 0;
	pciercx_cfg006.s.pbnum = 1;
	pciercx_cfg006.s.sbnum = 1;
	pciercx_cfg006.s.subbnum = 1;
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG006(pcie_port),
			     pciercx_cfg006.u32);

	/*
	 * Memory-mapped I/O BAR (PCIERCn_CFG008)
	 * Most applications should disable the memory-mapped I/O BAR by
	 * setting PCIERCn_CFG008[ML_ADDR] < PCIERCn_CFG008[MB_ADDR]
	 */
	pciercx_cfg008.u32 = 0;
	pciercx_cfg008.s.mb_addr = 0x100;
	pciercx_cfg008.s.ml_addr = 0;
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG008(pcie_port),
			     pciercx_cfg008.u32);

	/*
	 * Prefetchable BAR (PCIERCn_CFG009,PCIERCn_CFG010,PCIERCn_CFG011)
	 * Most applications should disable the prefetchable BAR by setting
	 * PCIERCn_CFG011[UMEM_LIMIT],PCIERCn_CFG009[LMEM_LIMIT] <
	 * PCIERCn_CFG010[UMEM_BASE],PCIERCn_CFG009[LMEM_BASE]
	 */
	pciercx_cfg009.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG009(pcie_port));
	pciercx_cfg010.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG010(pcie_port));
	pciercx_cfg011.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG011(pcie_port));
	pciercx_cfg009.s.lmem_base = 0x100;
	pciercx_cfg009.s.lmem_limit = 0;
	pciercx_cfg010.s.umem_base = 0x100;
	pciercx_cfg011.s.umem_limit = 0;
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG009(pcie_port),
			     pciercx_cfg009.u32);
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG010(pcie_port),
			     pciercx_cfg010.u32);
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG011(pcie_port),
			     pciercx_cfg011.u32);

	/*
	 * System Error Interrupt Enables (PCIERCn_CFG035[SECEE,SEFEE,SENFEE])
	 * PME Interrupt Enables (PCIERCn_CFG035[PMEIE])
	 */
	pciercx_cfg035.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG035(pcie_port));
	/* System error on correctable error enable. */
	pciercx_cfg035.s.secee = 1;
	/* System error on fatal error enable. */
	pciercx_cfg035.s.sefee = 1;
	/* System error on non-fatal error enable. */
	pciercx_cfg035.s.senfee = 1;
	/* PME interrupt enable. */
	pciercx_cfg035.s.pmeie = 1;
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG035(pcie_port),
			     pciercx_cfg035.u32);

	/*
	 * Advanced Error Recovery Interrupt Enables
	 * (PCIERCn_CFG075[CERE,NFERE,FERE])
	 */
	pciercx_cfg075.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG075(pcie_port));
	/* Correctable error reporting enable. */
	pciercx_cfg075.s.cere = 1;
	/* Non-fatal error reporting enable. */
	pciercx_cfg075.s.nfere = 1;
	/* Fatal error reporting enable. */
	pciercx_cfg075.s.fere = 1;
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG075(pcie_port),
			     pciercx_cfg075.u32);

	/* HP Interrupt Enables (PCIERCn_CFG034[HPINT_EN],
	 * PCIERCn_CFG034[DLLS_EN,CCINT_EN])
	 */
	pciercx_cfg034.u32 =
		cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG034(pcie_port));
	/* Hot-plug interrupt enable. */
	pciercx_cfg034.s.hpint_en = 1;
	/* Data Link Layer state changed enable */
	pciercx_cfg034.s.dlls_en = 1;
	/* Command completed interrupt enable. */
	pciercx_cfg034.s.ccint_en = 1;
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG034(pcie_port),
			     pciercx_cfg034.u32);
}

/**
 * Initialize a host mode PCIe link. This function takes a PCIe
 * port from reset to a link up state. Software can then begin
 * configuring the rest of the link.
 *
 * @pcie_port: PCIe port to initialize
 *
 * Returns Zero on success
 */
static int __cvmx_pcie_rc_initialize_link(int pcie_port)
{
	uint64_t start_cycle;
	union cvmx_pescx_ctl_status pescx_ctl_status;
	union cvmx_pciercx_cfg452 pciercx_cfg452;
	union cvmx_pciercx_cfg032 pciercx_cfg032;
	union cvmx_pciercx_cfg448 pciercx_cfg448;

	/* Set the lane width */
	pciercx_cfg452.u32 =
	    cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG452(pcie_port));
	pescx_ctl_status.u64 = cvmx_read_csr(CVMX_PESCX_CTL_STATUS(pcie_port));
	if (pescx_ctl_status.s.qlm_cfg == 0) {
		/* We're in 8 lane (56XX) or 4 lane (54XX) mode */
		pciercx_cfg452.s.lme = 0xf;
	} else {
		/* We're in 4 lane (56XX) or 2 lane (52XX) mode */
		pciercx_cfg452.s.lme = 0x7;
	}
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG452(pcie_port),
			     pciercx_cfg452.u32);

	/*
	 * CN52XX pass 1.x has an errata where length mismatches on UR
	 * responses can cause bus errors on 64bit memory
	 * reads. Turning off length error checking fixes this.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X)) {
		union cvmx_pciercx_cfg455 pciercx_cfg455;
		pciercx_cfg455.u32 =
		    cvmx_pcie_cfgx_read(pcie_port,
					CVMX_PCIERCX_CFG455(pcie_port));
		pciercx_cfg455.s.m_cpl_len_err = 1;
		cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG455(pcie_port),
				     pciercx_cfg455.u32);
	}

	/* Lane swap needs to be manually enabled for CN52XX */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX) && (pcie_port == 1)) {
		pescx_ctl_status.s.lane_swp = 1;
		cvmx_write_csr(CVMX_PESCX_CTL_STATUS(pcie_port),
			       pescx_ctl_status.u64);
	}

	/* Bring up the link */
	pescx_ctl_status.u64 = cvmx_read_csr(CVMX_PESCX_CTL_STATUS(pcie_port));
	pescx_ctl_status.s.lnk_enb = 1;
	cvmx_write_csr(CVMX_PESCX_CTL_STATUS(pcie_port), pescx_ctl_status.u64);

	/*
	 * CN52XX pass 1.0: Due to a bug in 2nd order CDR, it needs to
	 * be disabled.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_0))
		__cvmx_helper_errata_qlm_disable_2nd_order_cdr(0);

	/* Wait for the link to come up */
	cvmx_dprintf("PCIe: Waiting for port %d link\n", pcie_port);
	start_cycle = cvmx_get_cycle();
	do {
		if (cvmx_get_cycle() - start_cycle >
		    2 * cvmx_sysinfo_get()->cpu_clock_hz) {
			cvmx_dprintf("PCIe: Port %d link timeout\n",
				     pcie_port);
			return -1;
		}
		cvmx_wait(10000);
		pciercx_cfg032.u32 =
		    cvmx_pcie_cfgx_read(pcie_port,
					CVMX_PCIERCX_CFG032(pcie_port));
	} while (pciercx_cfg032.s.dlla == 0);

	/* Display the link status */
	cvmx_dprintf("PCIe: Port %d link active, %d lanes\n", pcie_port,
		     pciercx_cfg032.s.nlw);

	/*
	 * Update the Replay Time Limit. Empirically, some PCIe
	 * devices take a little longer to respond than expected under
	 * load. As a workaround for this we configure the Replay Time
	 * Limit to the value expected for a 512 byte MPS instead of
	 * our actual 256 byte MPS. The numbers below are directly
	 * from the PCIe spec table 3-4.
	 */
	pciercx_cfg448.u32 =
	    cvmx_pcie_cfgx_read(pcie_port, CVMX_PCIERCX_CFG448(pcie_port));
	switch (pciercx_cfg032.s.nlw) {
	case 1:		/* 1 lane */
		pciercx_cfg448.s.rtl = 1677;
		break;
	case 2:		/* 2 lanes */
		pciercx_cfg448.s.rtl = 867;
		break;
	case 4:		/* 4 lanes */
		pciercx_cfg448.s.rtl = 462;
		break;
	case 8:		/* 8 lanes */
		pciercx_cfg448.s.rtl = 258;
		break;
	}
	cvmx_pcie_cfgx_write(pcie_port, CVMX_PCIERCX_CFG448(pcie_port),
			     pciercx_cfg448.u32);

	return 0;
}

/**
 * Initialize a PCIe port for use in host(RC) mode. It doesn't
 * enumerate the bus.
 *
 * @pcie_port: PCIe port to initialize
 *
 * Returns Zero on success
 */
static int cvmx_pcie_rc_initialize(int pcie_port)
{
	int i;
	int base;
	u64 addr_swizzle;
	union cvmx_ciu_soft_prst ciu_soft_prst;
	union cvmx_pescx_bist_status pescx_bist_status;
	union cvmx_pescx_bist_status2 pescx_bist_status2;
	union cvmx_npei_ctl_status npei_ctl_status;
	union cvmx_npei_mem_access_ctl npei_mem_access_ctl;
	union cvmx_npei_mem_access_subidx mem_access_subid;
	union cvmx_npei_dbg_data npei_dbg_data;
	union cvmx_pescx_ctl_status2 pescx_ctl_status2;
	union cvmx_npei_bar1_indexx bar1_index;

	/*
	 * Make sure we aren't trying to setup a target mode interface
	 * in host mode.
	 */
	npei_ctl_status.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_STATUS);
	if ((pcie_port == 0) && !npei_ctl_status.s.host_mode) {
		cvmx_dprintf("PCIe: ERROR: cvmx_pcie_rc_initialize() called "
			     "on port0, but port0 is not in host mode\n");
		return -1;
	}

	/*
	 * Make sure a CN52XX isn't trying to bring up port 1 when it
	 * is disabled.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX)) {
		npei_dbg_data.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DBG_DATA);
		if ((pcie_port == 1) && npei_dbg_data.cn52xx.qlm0_link_width) {
			cvmx_dprintf("PCIe: ERROR: cvmx_pcie_rc_initialize() "
				     "called on port1, but port1 is "
				     "disabled\n");
			return -1;
		}
	}

	/*
	 * PCIe switch arbitration mode. '0' == fixed priority NPEI,
	 * PCIe0, then PCIe1. '1' == round robin.
	 */
	npei_ctl_status.s.arb = 1;
	/* Allow up to 0x20 config retries */
	npei_ctl_status.s.cfg_rtry = 0x20;
	/*
	 * CN52XX pass1.x has an errata where P0_NTAGS and P1_NTAGS
	 * don't reset.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X)) {
		npei_ctl_status.s.p0_ntags = 0x20;
		npei_ctl_status.s.p1_ntags = 0x20;
	}
	cvmx_write_csr(CVMX_PEXP_NPEI_CTL_STATUS, npei_ctl_status.u64);

	/* Bring the PCIe out of reset */
	if (cvmx_sysinfo_get()->board_type == CVMX_BOARD_TYPE_EBH5200) {
		/*
		 * The EBH5200 board swapped the PCIe reset lines on
		 * the board. As a workaround for this bug, we bring
		 * both PCIe ports out of reset at the same time
		 * instead of on separate calls. So for port 0, we
		 * bring both out of reset and do nothing on port 1.
		 */
		if (pcie_port == 0) {
			ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
			/*
			 * After a chip reset the PCIe will also be in
			 * reset. If it isn't, most likely someone is
			 * trying to init it again without a proper
			 * PCIe reset.
			 */
			if (ciu_soft_prst.s.soft_prst == 0) {
				/* Reset the ports */
				ciu_soft_prst.s.soft_prst = 1;
				cvmx_write_csr(CVMX_CIU_SOFT_PRST,
					       ciu_soft_prst.u64);
				ciu_soft_prst.u64 =
				    cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
				ciu_soft_prst.s.soft_prst = 1;
				cvmx_write_csr(CVMX_CIU_SOFT_PRST1,
					       ciu_soft_prst.u64);
				/* Wait until pcie resets the ports. */
				udelay(2000);
			}
			ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
			ciu_soft_prst.s.soft_prst = 0;
			cvmx_write_csr(CVMX_CIU_SOFT_PRST1, ciu_soft_prst.u64);
			ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
			ciu_soft_prst.s.soft_prst = 0;
			cvmx_write_csr(CVMX_CIU_SOFT_PRST, ciu_soft_prst.u64);
		}
	} else {
		/*
		 * The normal case: The PCIe ports are completely
		 * separate and can be brought out of reset
		 * independently.
		 */
		if (pcie_port)
			ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
		else
			ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
		/*
		 * After a chip reset the PCIe will also be in
		 * reset. If it isn't, most likely someone is trying
		 * to init it again without a proper PCIe reset.
		 */
		if (ciu_soft_prst.s.soft_prst == 0) {
			/* Reset the port */
			ciu_soft_prst.s.soft_prst = 1;
			if (pcie_port)
				cvmx_write_csr(CVMX_CIU_SOFT_PRST1,
					       ciu_soft_prst.u64);
			else
				cvmx_write_csr(CVMX_CIU_SOFT_PRST,
					       ciu_soft_prst.u64);
			/* Wait until pcie resets the ports. */
			udelay(2000);
		}
		if (pcie_port) {
			ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST1);
			ciu_soft_prst.s.soft_prst = 0;
			cvmx_write_csr(CVMX_CIU_SOFT_PRST1, ciu_soft_prst.u64);
		} else {
			ciu_soft_prst.u64 = cvmx_read_csr(CVMX_CIU_SOFT_PRST);
			ciu_soft_prst.s.soft_prst = 0;
			cvmx_write_csr(CVMX_CIU_SOFT_PRST, ciu_soft_prst.u64);
		}
	}

	/*
	 * Wait for PCIe reset to complete. Due to errata PCIE-700, we
	 * don't poll PESCX_CTL_STATUS2[PCIERST], but simply wait a
	 * fixed number of cycles.
	 */
	cvmx_wait(400000);

	/* PESCX_BIST_STATUS2[PCLK_RUN] was missing on pass 1 of CN56XX and
	   CN52XX, so we only probe it on newer chips */
	if (!OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_X)
	    && !OCTEON_IS_MODEL(OCTEON_CN52XX_PASS1_X)) {
		/* Clear PCLK_RUN so we can check if the clock is running */
		pescx_ctl_status2.u64 =
		    cvmx_read_csr(CVMX_PESCX_CTL_STATUS2(pcie_port));
		pescx_ctl_status2.s.pclk_run = 1;
		cvmx_write_csr(CVMX_PESCX_CTL_STATUS2(pcie_port),
			       pescx_ctl_status2.u64);
		/*
		 * Now that we cleared PCLK_RUN, wait for it to be set
		 * again telling us the clock is running.
		 */
		if (CVMX_WAIT_FOR_FIELD64(CVMX_PESCX_CTL_STATUS2(pcie_port),
					  union cvmx_pescx_ctl_status2,
					  pclk_run, ==, 1, 10000)) {
			cvmx_dprintf("PCIe: Port %d isn't clocked, skipping.\n",
				     pcie_port);
			return -1;
		}
	}

	/*
	 * Check and make sure PCIe came out of reset. If it doesn't
	 * the board probably hasn't wired the clocks up and the
	 * interface should be skipped.
	 */
	pescx_ctl_status2.u64 =
	    cvmx_read_csr(CVMX_PESCX_CTL_STATUS2(pcie_port));
	if (pescx_ctl_status2.s.pcierst) {
		cvmx_dprintf("PCIe: Port %d stuck in reset, skipping.\n",
			     pcie_port);
		return -1;
	}

	/*
	 * Check BIST2 status. If any bits are set skip this interface. This
	 * is an attempt to catch PCIE-813 on pass 1 parts.
	 */
	pescx_bist_status2.u64 =
	    cvmx_read_csr(CVMX_PESCX_BIST_STATUS2(pcie_port));
	if (pescx_bist_status2.u64) {
		cvmx_dprintf("PCIe: Port %d BIST2 failed. Most likely this "
			     "port isn't hooked up, skipping.\n",
			     pcie_port);
		return -1;
	}

	/* Check BIST status */
	pescx_bist_status.u64 =
	    cvmx_read_csr(CVMX_PESCX_BIST_STATUS(pcie_port));
	if (pescx_bist_status.u64)
		cvmx_dprintf("PCIe: BIST FAILED for port %d (0x%016llx)\n",
			     pcie_port, CAST64(pescx_bist_status.u64));

	/* Initialize the config space CSRs */
	__cvmx_pcie_rc_initialize_config_space(pcie_port);

	/* Bring the link up */
	if (__cvmx_pcie_rc_initialize_link(pcie_port)) {
		cvmx_dprintf
		    ("PCIe: ERROR: cvmx_pcie_rc_initialize_link() failed\n");
		return -1;
	}

	/* Store merge control (NPEI_MEM_ACCESS_CTL[TIMER,MAX_WORD]) */
	npei_mem_access_ctl.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_MEM_ACCESS_CTL);
	/* Allow 16 words to combine */
	npei_mem_access_ctl.s.max_word = 0;
	/* Wait up to 127 cycles for more data */
	npei_mem_access_ctl.s.timer = 127;
	cvmx_write_csr(CVMX_PEXP_NPEI_MEM_ACCESS_CTL, npei_mem_access_ctl.u64);

	/* Setup Mem access SubDIDs */
	mem_access_subid.u64 = 0;
	/* Port the request is sent to. */
	mem_access_subid.s.port = pcie_port;
	/* Due to an errata on pass 1 chips, no merging is allowed. */
	mem_access_subid.s.nmerge = 1;
	/* Endian-swap for Reads. */
	mem_access_subid.s.esr = 1;
	/* Endian-swap for Writes. */
	mem_access_subid.s.esw = 1;
	/* No Snoop for Reads. */
	mem_access_subid.s.nsr = 1;
	/* No Snoop for Writes. */
	mem_access_subid.s.nsw = 1;
	/* Disable Relaxed Ordering for Reads. */
	mem_access_subid.s.ror = 0;
	/* Disable Relaxed Ordering for Writes. */
	mem_access_subid.s.row = 0;
	/* PCIe Address Bits <63:34>. */
	mem_access_subid.s.ba = 0;

	/*
	 * Setup mem access 12-15 for port 0, 16-19 for port 1,
	 * supplying 36 bits of address space.
	 */
	for (i = 12 + pcie_port * 4; i < 16 + pcie_port * 4; i++) {
		cvmx_write_csr(CVMX_PEXP_NPEI_MEM_ACCESS_SUBIDX(i),
			       mem_access_subid.u64);
		/* Set each SUBID to extend the addressable range */
		mem_access_subid.s.ba += 1;
	}

	/*
	 * Disable the peer to peer forwarding register. This must be
	 * setup by the OS after it enumerates the bus and assigns
	 * addresses to the PCIe busses.
	 */
	for (i = 0; i < 4; i++) {
		cvmx_write_csr(CVMX_PESCX_P2P_BARX_START(i, pcie_port), -1);
		cvmx_write_csr(CVMX_PESCX_P2P_BARX_END(i, pcie_port), -1);
	}

	/* Set Octeon's BAR0 to decode 0-16KB. It overlaps with Bar2 */
	cvmx_write_csr(CVMX_PESCX_P2N_BAR0_START(pcie_port), 0);

	/* BAR1 follows BAR2 with a gap. */
	cvmx_write_csr(CVMX_PESCX_P2N_BAR1_START(pcie_port), CVMX_PCIE_BAR1_RC_BASE);

	bar1_index.u32 = 0;
	bar1_index.s.addr_idx = (CVMX_PCIE_BAR1_PHYS_BASE >> 22);
	bar1_index.s.ca = 1;       /* Not Cached */
	bar1_index.s.end_swp = 1;  /* Endian Swap mode */
	bar1_index.s.addr_v = 1;   /* Valid entry */

	base = pcie_port ? 16 : 0;

	/* Big endian swizzle for 32-bit PEXP_NCB register. */
#ifdef __MIPSEB__
	addr_swizzle = 4;
#else
	addr_swizzle = 0;
#endif
	for (i = 0; i < 16; i++) {
		cvmx_write64_uint32((CVMX_PEXP_NPEI_BAR1_INDEXX(base) ^ addr_swizzle),
				    bar1_index.u32);
		base++;
		/* 256MB / 16 >> 22 == 4 */
		bar1_index.s.addr_idx += (((1ull << 28) / 16ull) >> 22);
	}

	/*
	 * Set Octeon's BAR2 to decode 0-2^39. Bar0 and Bar1 take
	 * precedence where they overlap. It also overlaps with the
	 * device addresses, so make sure the peer to peer forwarding
	 * is set right.
	 */
	cvmx_write_csr(CVMX_PESCX_P2N_BAR2_START(pcie_port), 0);

	/*
	 * Setup BAR2 attributes
	 *
	 * Relaxed Ordering (NPEI_CTL_PORTn[PTLP_RO,CTLP_RO, WAIT_COM])
	 * - PTLP_RO,CTLP_RO should normally be set (except for debug).
	 * - WAIT_COM=0 will likely work for all applications.
	 *
	 * Load completion relaxed ordering (NPEI_CTL_PORTn[WAITL_COM]).
	 */
	if (pcie_port) {
		union cvmx_npei_ctl_port1 npei_ctl_port;
		npei_ctl_port.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_PORT1);
		npei_ctl_port.s.bar2_enb = 1;
		npei_ctl_port.s.bar2_esx = 1;
		npei_ctl_port.s.bar2_cax = 0;
		npei_ctl_port.s.ptlp_ro = 1;
		npei_ctl_port.s.ctlp_ro = 1;
		npei_ctl_port.s.wait_com = 0;
		npei_ctl_port.s.waitl_com = 0;
		cvmx_write_csr(CVMX_PEXP_NPEI_CTL_PORT1, npei_ctl_port.u64);
	} else {
		union cvmx_npei_ctl_port0 npei_ctl_port;
		npei_ctl_port.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_PORT0);
		npei_ctl_port.s.bar2_enb = 1;
		npei_ctl_port.s.bar2_esx = 1;
		npei_ctl_port.s.bar2_cax = 0;
		npei_ctl_port.s.ptlp_ro = 1;
		npei_ctl_port.s.ctlp_ro = 1;
		npei_ctl_port.s.wait_com = 0;
		npei_ctl_port.s.waitl_com = 0;
		cvmx_write_csr(CVMX_PEXP_NPEI_CTL_PORT0, npei_ctl_port.u64);
	}
	return 0;
}


/* Above was cvmx-pcie.c, below original pcie.c */


/**
 * Map a PCI device to the appropriate interrupt line
 *
 * @dev:    The Linux PCI device structure for the device to map
 * @slot:   The slot number for this device on __BUS 0__. Linux
 *               enumerates through all the bridges and figures out the
 *               slot on Bus 0 where this device eventually hooks to.
 * @pin:    The PCI interrupt pin read from the device, then swizzled
 *               as it goes through each bridge.
 * Returns Interrupt number for the device
 */
int __init octeon_pcie_pcibios_map_irq(const struct pci_dev *dev,
				       u8 slot, u8 pin)
{
	/*
	 * The EBH5600 board with the PCI to PCIe bridge mistakenly
	 * wires the first slot for both device id 2 and interrupt
	 * A. According to the PCI spec, device id 2 should be C. The
	 * following kludge attempts to fix this.
	 */
	if (strstr(octeon_board_type_string(), "EBH5600") &&
	    dev->bus && dev->bus->parent) {
		/*
		 * Iterate all the way up the device chain and find
		 * the root bus.
		 */
		while (dev->bus && dev->bus->parent)
			dev = to_pci_dev(dev->bus->bridge);
		/* If the root bus is number 0 and the PEX 8114 is the
		 * root, assume we are behind the miswired bus. We
		 * need to correct the swizzle level by two. Yuck.
		 */
		if ((dev->bus->number == 0) &&
		    (dev->vendor == 0x10b5) && (dev->device == 0x8114)) {
			/*
			 * The pin field is one based, not zero. We
			 * need to swizzle it by minus two.
			 */
			pin = ((pin - 3) & 3) + 1;
		}
	}
	/*
	 * The -1 is because pin starts with one, not zero. It might
	 * be that this equation needs to include the slot number, but
	 * I don't have hardware to check that against.
	 */
	return pin - 1 + OCTEON_IRQ_PCI_INT0;
}

/**
 * Read a value from configuration space
 *
 * @bus:
 * @devfn:
 * @reg:
 * @size:
 * @val:
 * Returns
 */
static inline int octeon_pcie_read_config(int pcie_port, struct pci_bus *bus,
					  unsigned int devfn, int reg, int size,
					  u32 *val)
{
	union octeon_cvmemctl cvmmemctl;
	union octeon_cvmemctl cvmmemctl_save;
	int bus_number = bus->number;

	/*
	 * For the top level bus make sure our hardware bus number
	 * matches the software one.
	 */
	if (bus->parent == NULL) {
		union cvmx_pciercx_cfg006 pciercx_cfg006;
		pciercx_cfg006.u32 = cvmx_pcie_cfgx_read(pcie_port,
			CVMX_PCIERCX_CFG006(pcie_port));
		if (pciercx_cfg006.s.pbnum != bus_number) {
			pciercx_cfg006.s.pbnum = bus_number;
			pciercx_cfg006.s.sbnum = bus_number;
			pciercx_cfg006.s.subbnum = bus_number;
			cvmx_pcie_cfgx_write(pcie_port,
				CVMX_PCIERCX_CFG006(pcie_port),
				pciercx_cfg006.u32);
		}
	}

	/*
	 * PCIe only has a single device connected to Octeon. It is
	 * always device ID 0. Don't bother doing reads for other
	 * device IDs on the first segment.
	 */
	if ((bus->parent == NULL) && (devfn >> 3 != 0))
		return PCIBIOS_FUNC_NOT_SUPPORTED;

	/*
	 * The following is a workaround for the CN57XX, CN56XX,
	 * CN55XX, and CN54XX errata with PCIe config reads from non
	 * existent devices.  These chips will hang the PCIe link if a
	 * config read is performed that causes a UR response.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1) ||
	    OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_1)) {
		/*
		 * For our EBH5600 board, port 0 has a bridge with two
		 * PCI-X slots. We need a new special checks to make
		 * sure we only probe valid stuff.  The PCIe->PCI-X
		 * bridge only respondes to device ID 0, function
		 * 0-1
		 */
		if ((bus->parent == NULL) && (devfn >= 2))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
		/*
		 * The PCI-X slots are device ID 2,3. Choose one of
		 * the below "if" blocks based on what is plugged into
		 * the board.
		 */
#if 1
		/* Use this option if you aren't using either slot */
		if (bus_number == 1)
			return PCIBIOS_FUNC_NOT_SUPPORTED;
#elif 0
		/*
		 * Use this option if you are using the first slot but
		 * not the second.
		 */
		if ((bus_number == 1) && (devfn >> 3 != 2))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
#elif 0
		/*
		 * Use this option if you are using the second slot
		 * but not the first.
		 */
		if ((bus_number == 1) && (devfn >> 3 != 3))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
#elif 0
		/* Use this opion if you are using both slots */
		if ((bus_number == 1) &&
		    !((devfn == (2 << 3)) || (devfn == (3 << 3))))
			return PCIBIOS_FUNC_NOT_SUPPORTED;
#endif

		/*
		 * Shorten the DID timeout so bus errors for PCIe
		 * config reads from non existent devices happen
		 * faster. This allows us to continue booting even if
		 * the above "if" checks are wrong.  Once one of these
		 * errors happens, the PCIe port is dead.
		 */
		cvmmemctl_save.u64 = __read_64bit_c0_register($11, 7);
		cvmmemctl.u64 = cvmmemctl_save.u64;
		cvmmemctl.s.didtto = 2;
		__write_64bit_c0_register($11, 7, cvmmemctl.u64);
	}

	switch (size) {
	case 4:
		*val = cvmx_pcie_config_read32(pcie_port, bus_number,
					       devfn >> 3, devfn & 0x7, reg);
		break;
	case 2:
		*val = cvmx_pcie_config_read16(pcie_port, bus_number,
					       devfn >> 3, devfn & 0x7, reg);
		break;
	case 1:
		*val = cvmx_pcie_config_read8(pcie_port, bus_number, devfn >> 3,
					      devfn & 0x7, reg);
		break;
	default:
		return PCIBIOS_FUNC_NOT_SUPPORTED;
	}

	if (OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1) ||
	    OCTEON_IS_MODEL(OCTEON_CN56XX_PASS1_1))
		__write_64bit_c0_register($11, 7, cvmmemctl_save.u64);
	return PCIBIOS_SUCCESSFUL;
}

static int octeon_pcie0_read_config(struct pci_bus *bus, unsigned int devfn,
				    int reg, int size, u32 *val)
{
	return octeon_pcie_read_config(0, bus, devfn, reg, size, val);
}

static int octeon_pcie1_read_config(struct pci_bus *bus, unsigned int devfn,
				    int reg, int size, u32 *val)
{
	return octeon_pcie_read_config(1, bus, devfn, reg, size, val);
}



/**
 * Write a value to PCI configuration space
 *
 * @bus:
 * @devfn:
 * @reg:
 * @size:
 * @val:
 * Returns
 */
static inline int octeon_pcie_write_config(int pcie_port, struct pci_bus *bus,
					   unsigned int devfn, int reg,
					   int size, u32 val)
{
	int bus_number = bus->number;

	switch (size) {
	case 4:
		cvmx_pcie_config_write32(pcie_port, bus_number, devfn >> 3,
					 devfn & 0x7, reg, val);
		return PCIBIOS_SUCCESSFUL;
	case 2:
		cvmx_pcie_config_write16(pcie_port, bus_number, devfn >> 3,
					 devfn & 0x7, reg, val);
		return PCIBIOS_SUCCESSFUL;
	case 1:
		cvmx_pcie_config_write8(pcie_port, bus_number, devfn >> 3,
					devfn & 0x7, reg, val);
		return PCIBIOS_SUCCESSFUL;
	}
#if PCI_CONFIG_SPACE_DELAY
	udelay(PCI_CONFIG_SPACE_DELAY);
#endif
	return PCIBIOS_FUNC_NOT_SUPPORTED;
}

static int octeon_pcie0_write_config(struct pci_bus *bus, unsigned int devfn,
				     int reg, int size, u32 val)
{
	return octeon_pcie_write_config(0, bus, devfn, reg, size, val);
}

static int octeon_pcie1_write_config(struct pci_bus *bus, unsigned int devfn,
				     int reg, int size, u32 val)
{
	return octeon_pcie_write_config(1, bus, devfn, reg, size, val);
}

static struct pci_ops octeon_pcie0_ops = {
	octeon_pcie0_read_config,
	octeon_pcie0_write_config,
};

static struct resource octeon_pcie0_mem_resource = {
	.name = "Octeon PCIe0 MEM",
	.flags = IORESOURCE_MEM,
};

static struct resource octeon_pcie0_io_resource = {
	.name = "Octeon PCIe0 IO",
	.flags = IORESOURCE_IO,
};

static struct pci_controller octeon_pcie0_controller = {
	.pci_ops = &octeon_pcie0_ops,
	.mem_resource = &octeon_pcie0_mem_resource,
	.io_resource = &octeon_pcie0_io_resource,
};

static struct pci_ops octeon_pcie1_ops = {
	octeon_pcie1_read_config,
	octeon_pcie1_write_config,
};

static struct resource octeon_pcie1_mem_resource = {
	.name = "Octeon PCIe1 MEM",
	.flags = IORESOURCE_MEM,
};

static struct resource octeon_pcie1_io_resource = {
	.name = "Octeon PCIe1 IO",
	.flags = IORESOURCE_IO,
};

static struct pci_controller octeon_pcie1_controller = {
	.pci_ops = &octeon_pcie1_ops,
	.mem_resource = &octeon_pcie1_mem_resource,
	.io_resource = &octeon_pcie1_io_resource,
};


/**
 * Initialize the Octeon PCIe controllers
 *
 * Returns
 */
static int __init octeon_pcie_setup(void)
{
	union cvmx_npei_ctl_status npei_ctl_status;
	int result;

	/* These chips don't have PCIe */
	if (!octeon_has_feature(OCTEON_FEATURE_PCIE))
		return 0;

	/* Point pcibios_map_irq() to the PCIe version of it */
	octeon_pcibios_map_irq = octeon_pcie_pcibios_map_irq;

	/* Use the PCIe based DMA mappings */
	octeon_dma_bar_type = OCTEON_DMA_BAR_TYPE_PCIE;

	/*
	 * PCIe I/O range. It is based on port 0 but includes up until
	 * port 1's end.
	 */
	set_io_port_base(CVMX_ADD_IO_SEG(cvmx_pcie_get_io_base_address(0)));
	ioport_resource.start = 0;
	ioport_resource.end =
		cvmx_pcie_get_io_base_address(1) -
		cvmx_pcie_get_io_base_address(0) + cvmx_pcie_get_io_size(1) - 1;

	npei_ctl_status.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_CTL_STATUS);
	if (npei_ctl_status.s.host_mode) {
		pr_notice("PCIe: Initializing port 0\n");
		result = cvmx_pcie_rc_initialize(0);
		if (result == 0) {
			/* Memory offsets are physical addresses */
			octeon_pcie0_controller.mem_offset =
				cvmx_pcie_get_mem_base_address(0);
			/* IO offsets are Mips virtual addresses */
			octeon_pcie0_controller.io_map_base =
				CVMX_ADD_IO_SEG(cvmx_pcie_get_io_base_address
						(0));
			octeon_pcie0_controller.io_offset = 0;
			/*
			 * To keep things similar to PCI, we start
			 * device addresses at the same place as PCI
			 * uisng big bar support. This normally
			 * translates to 4GB-256MB, which is the same
			 * as most x86 PCs.
			 */
			octeon_pcie0_controller.mem_resource->start =
				cvmx_pcie_get_mem_base_address(0) +
				(4ul << 30) - (OCTEON_PCI_BAR1_HOLE_SIZE << 20);
			octeon_pcie0_controller.mem_resource->end =
				cvmx_pcie_get_mem_base_address(0) +
				cvmx_pcie_get_mem_size(0) - 1;
			/*
			 * Ports must be above 16KB for the ISA bus
			 * filtering in the PCI-X to PCI bridge.
			 */
			octeon_pcie0_controller.io_resource->start = 4 << 10;
			octeon_pcie0_controller.io_resource->end =
				cvmx_pcie_get_io_size(0) - 1;
			register_pci_controller(&octeon_pcie0_controller);
		}
	} else {
		pr_notice("PCIe: Port 0 in endpoint mode, skipping.\n");
	}

	/* Skip the 2nd port on CN52XX if port 0 is in 4 lane mode */
	if (OCTEON_IS_MODEL(OCTEON_CN52XX)) {
		union cvmx_npei_dbg_data npei_dbg_data;
		npei_dbg_data.u64 = cvmx_read_csr(CVMX_PEXP_NPEI_DBG_DATA);
		if (npei_dbg_data.cn52xx.qlm0_link_width)
			return 0;
	}

	pr_notice("PCIe: Initializing port 1\n");
	result = cvmx_pcie_rc_initialize(1);
	if (result == 0) {
		/* Memory offsets are physical addresses */
		octeon_pcie1_controller.mem_offset =
			cvmx_pcie_get_mem_base_address(1);
		/* IO offsets are Mips virtual addresses */
		octeon_pcie1_controller.io_map_base =
			CVMX_ADD_IO_SEG(cvmx_pcie_get_io_base_address(1));
		octeon_pcie1_controller.io_offset =
			cvmx_pcie_get_io_base_address(1) -
			cvmx_pcie_get_io_base_address(0);
		/*
		 * To keep things similar to PCI, we start device
		 * addresses at the same place as PCI uisng big bar
		 * support. This normally translates to 4GB-256MB,
		 * which is the same as most x86 PCs.
		 */
		octeon_pcie1_controller.mem_resource->start =
			cvmx_pcie_get_mem_base_address(1) + (4ul << 30) -
			(OCTEON_PCI_BAR1_HOLE_SIZE << 20);
		octeon_pcie1_controller.mem_resource->end =
			cvmx_pcie_get_mem_base_address(1) +
			cvmx_pcie_get_mem_size(1) - 1;
		/*
		 * Ports must be above 16KB for the ISA bus filtering
		 * in the PCI-X to PCI bridge.
		 */
		octeon_pcie1_controller.io_resource->start =
			cvmx_pcie_get_io_base_address(1) -
			cvmx_pcie_get_io_base_address(0);
		octeon_pcie1_controller.io_resource->end =
			octeon_pcie1_controller.io_resource->start +
			cvmx_pcie_get_io_size(1) - 1;
		register_pci_controller(&octeon_pcie1_controller);
	}

	octeon_pci_dma_init();

	return 0;
}

arch_initcall(octeon_pcie_setup);
