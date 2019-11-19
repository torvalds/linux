// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009-2016 Cavium, Inc.
 */

enum cavium_mdiobus_mode {
	UNINIT = 0,
	C22,
	C45
};

#define SMI_CMD		0x0
#define SMI_WR_DAT	0x8
#define SMI_RD_DAT	0x10
#define SMI_CLK		0x18
#define SMI_EN		0x20

#ifdef __BIG_ENDIAN_BITFIELD
#define OCT_MDIO_BITFIELD_FIELD(field, more)	\
	field;					\
	more

#else
#define OCT_MDIO_BITFIELD_FIELD(field, more)	\
	more					\
	field;

#endif

union cvmx_smix_clk {
	u64 u64;
	struct cvmx_smix_clk_s {
	  OCT_MDIO_BITFIELD_FIELD(u64 reserved_25_63:39,
	  OCT_MDIO_BITFIELD_FIELD(u64 mode:1,
	  OCT_MDIO_BITFIELD_FIELD(u64 reserved_21_23:3,
	  OCT_MDIO_BITFIELD_FIELD(u64 sample_hi:5,
	  OCT_MDIO_BITFIELD_FIELD(u64 sample_mode:1,
	  OCT_MDIO_BITFIELD_FIELD(u64 reserved_14_14:1,
	  OCT_MDIO_BITFIELD_FIELD(u64 clk_idle:1,
	  OCT_MDIO_BITFIELD_FIELD(u64 preamble:1,
	  OCT_MDIO_BITFIELD_FIELD(u64 sample:4,
	  OCT_MDIO_BITFIELD_FIELD(u64 phase:8,
	  ;))))))))))
	} s;
};

union cvmx_smix_cmd {
	u64 u64;
	struct cvmx_smix_cmd_s {
	  OCT_MDIO_BITFIELD_FIELD(u64 reserved_18_63:46,
	  OCT_MDIO_BITFIELD_FIELD(u64 phy_op:2,
	  OCT_MDIO_BITFIELD_FIELD(u64 reserved_13_15:3,
	  OCT_MDIO_BITFIELD_FIELD(u64 phy_adr:5,
	  OCT_MDIO_BITFIELD_FIELD(u64 reserved_5_7:3,
	  OCT_MDIO_BITFIELD_FIELD(u64 reg_adr:5,
	  ;))))))
	} s;
};

union cvmx_smix_en {
	u64 u64;
	struct cvmx_smix_en_s {
	  OCT_MDIO_BITFIELD_FIELD(u64 reserved_1_63:63,
	  OCT_MDIO_BITFIELD_FIELD(u64 en:1,
	  ;))
	} s;
};

union cvmx_smix_rd_dat {
	u64 u64;
	struct cvmx_smix_rd_dat_s {
	  OCT_MDIO_BITFIELD_FIELD(u64 reserved_18_63:46,
	  OCT_MDIO_BITFIELD_FIELD(u64 pending:1,
	  OCT_MDIO_BITFIELD_FIELD(u64 val:1,
	  OCT_MDIO_BITFIELD_FIELD(u64 dat:16,
	  ;))))
	} s;
};

union cvmx_smix_wr_dat {
	u64 u64;
	struct cvmx_smix_wr_dat_s {
	  OCT_MDIO_BITFIELD_FIELD(u64 reserved_18_63:46,
	  OCT_MDIO_BITFIELD_FIELD(u64 pending:1,
	  OCT_MDIO_BITFIELD_FIELD(u64 val:1,
	  OCT_MDIO_BITFIELD_FIELD(u64 dat:16,
	  ;))))
	} s;
};

struct cavium_mdiobus {
	struct mii_bus *mii_bus;
	u64 register_base;
	enum cavium_mdiobus_mode mode;
};

#ifdef CONFIG_CAVIUM_OCTEON_SOC

#include <asm/octeon/octeon.h>

static inline void oct_mdio_writeq(u64 val, u64 addr)
{
	cvmx_write_csr(addr, val);
}

static inline u64 oct_mdio_readq(u64 addr)
{
	return cvmx_read_csr(addr);
}
#else
#include <linux/io-64-nonatomic-lo-hi.h>

#define oct_mdio_writeq(val, addr)	writeq(val, (void *)addr)
#define oct_mdio_readq(addr)		readq((void *)addr)
#endif

int cavium_mdiobus_read(struct mii_bus *bus, int phy_id, int regnum);
int cavium_mdiobus_write(struct mii_bus *bus, int phy_id, int regnum, u16 val);
