/*
   Common Flash Interface probe code.
   (C) 2000 Red Hat. GPL'd.
   $Id: jedec_probe.c,v 1.66 2005/11/07 11:14:23 gleixner Exp $
   See JEDEC (http://www.jedec.org/) standard JESD21C (section 3.5)
   for the standard this probe goes back to.

   Occasionally maintained by Thayne Harbaugh tharbaugh at lnxi dot com
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/byteorder.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/gen_probe.h>

/* Manufacturers */
#define MANUFACTURER_AMD	0x0001
#define MANUFACTURER_ATMEL	0x001f
#define MANUFACTURER_FUJITSU	0x0004
#define MANUFACTURER_HYUNDAI	0x00AD
#define MANUFACTURER_INTEL	0x0089
#define MANUFACTURER_MACRONIX	0x00C2
#define MANUFACTURER_NEC	0x0010
#define MANUFACTURER_PMC	0x009D
#define MANUFACTURER_SST	0x00BF
#define MANUFACTURER_ST		0x0020
#define MANUFACTURER_TOSHIBA	0x0098
#define MANUFACTURER_WINBOND	0x00da


/* AMD */
#define AM29DL800BB	0x22C8
#define AM29DL800BT	0x224A

#define AM29F800BB	0x2258
#define AM29F800BT	0x22D6
#define AM29LV400BB	0x22BA
#define AM29LV400BT	0x22B9
#define AM29LV800BB	0x225B
#define AM29LV800BT	0x22DA
#define AM29LV160DT	0x22C4
#define AM29LV160DB	0x2249
#define AM29F017D	0x003D
#define AM29F016D	0x00AD
#define AM29F080	0x00D5
#define AM29F040	0x00A4
#define AM29LV040B	0x004F
#define AM29F032B	0x0041
#define AM29F002T	0x00B0

/* Atmel */
#define AT49BV512	0x0003
#define AT29LV512	0x003d
#define AT49BV16X	0x00C0
#define AT49BV16XT	0x00C2
#define AT49BV32X	0x00C8
#define AT49BV32XT	0x00C9

/* Fujitsu */
#define MBM29F040C	0x00A4
#define MBM29LV650UE	0x22D7
#define MBM29LV320TE	0x22F6
#define MBM29LV320BE	0x22F9
#define MBM29LV160TE	0x22C4
#define MBM29LV160BE	0x2249
#define MBM29LV800BA	0x225B
#define MBM29LV800TA	0x22DA
#define MBM29LV400TC	0x22B9
#define MBM29LV400BC	0x22BA

/* Hyundai */
#define HY29F002T	0x00B0

/* Intel */
#define I28F004B3T	0x00d4
#define I28F004B3B	0x00d5
#define I28F400B3T	0x8894
#define I28F400B3B	0x8895
#define I28F008S5	0x00a6
#define I28F016S5	0x00a0
#define I28F008SA	0x00a2
#define I28F008B3T	0x00d2
#define I28F008B3B	0x00d3
#define I28F800B3T	0x8892
#define I28F800B3B	0x8893
#define I28F016S3	0x00aa
#define I28F016B3T	0x00d0
#define I28F016B3B	0x00d1
#define I28F160B3T	0x8890
#define I28F160B3B	0x8891
#define I28F320B3T	0x8896
#define I28F320B3B	0x8897
#define I28F640B3T	0x8898
#define I28F640B3B	0x8899
#define I82802AB	0x00ad
#define I82802AC	0x00ac

/* Macronix */
#define MX29LV040C	0x004F
#define MX29LV160T	0x22C4
#define MX29LV160B	0x2249
#define MX29F016	0x00AD
#define MX29F002T	0x00B0
#define MX29F004T	0x0045
#define MX29F004B	0x0046

/* NEC */
#define UPD29F064115	0x221C

/* PMC */
#define PM49FL002	0x006D
#define PM49FL004	0x006E
#define PM49FL008	0x006A

/* ST - www.st.com */
#define M29W800DT	0x00D7
#define M29W800DB	0x005B
#define M29W160DT	0x22C4
#define M29W160DB	0x2249
#define M29W040B	0x00E3
#define M50FW040	0x002C
#define M50FW080	0x002D
#define M50FW016	0x002E
#define M50LPW080       0x002F

/* SST */
#define SST29EE020	0x0010
#define SST29LE020	0x0012
#define SST29EE512	0x005d
#define SST29LE512	0x003d
#define SST39LF800	0x2781
#define SST39LF160	0x2782
#define SST39VF1601	0x234b
#define SST39LF512	0x00D4
#define SST39LF010	0x00D5
#define SST39LF020	0x00D6
#define SST39LF040	0x00D7
#define SST39SF010A	0x00B5
#define SST39SF020A	0x00B6
#define SST49LF004B	0x0060
#define SST49LF008A	0x005a
#define SST49LF030A	0x001C
#define SST49LF040A	0x0051
#define SST49LF080A	0x005B

/* Toshiba */
#define TC58FVT160	0x00C2
#define TC58FVB160	0x0043
#define TC58FVT321	0x009A
#define TC58FVB321	0x009C
#define TC58FVT641	0x0093
#define TC58FVB641	0x0095

/* Winbond */
#define W49V002A	0x00b0


/*
 * Unlock address sets for AMD command sets.
 * Intel command sets use the MTD_UADDR_UNNECESSARY.
 * Each identifier, except MTD_UADDR_UNNECESSARY, and
 * MTD_UADDR_NO_SUPPORT must be defined below in unlock_addrs[].
 * MTD_UADDR_NOT_SUPPORTED must be 0 so that structure
 * initialization need not require initializing all of the
 * unlock addresses for all bit widths.
 */
enum uaddr {
	MTD_UADDR_NOT_SUPPORTED = 0,	/* data width not supported */
	MTD_UADDR_0x0555_0x02AA,
	MTD_UADDR_0x0555_0x0AAA,
	MTD_UADDR_0x5555_0x2AAA,
	MTD_UADDR_0x0AAA_0x0555,
	MTD_UADDR_DONT_CARE,		/* Requires an arbitrary address */
	MTD_UADDR_UNNECESSARY,		/* Does not require any address */
};


struct unlock_addr {
	u32 addr1;
	u32 addr2;
};


/*
 * I don't like the fact that the first entry in unlock_addrs[]
 * exists, but is for MTD_UADDR_NOT_SUPPORTED - and, therefore,
 * should not be used.  The  problem is that structures with
 * initializers have extra fields initialized to 0.  It is _very_
 * desireable to have the unlock address entries for unsupported
 * data widths automatically initialized - that means that
 * MTD_UADDR_NOT_SUPPORTED must be 0 and the first entry here
 * must go unused.
 */
static const struct unlock_addr  unlock_addrs[] = {
	[MTD_UADDR_NOT_SUPPORTED] = {
		.addr1 = 0xffff,
		.addr2 = 0xffff
	},

	[MTD_UADDR_0x0555_0x02AA] = {
		.addr1 = 0x0555,
		.addr2 = 0x02aa
	},

	[MTD_UADDR_0x0555_0x0AAA] = {
		.addr1 = 0x0555,
		.addr2 = 0x0aaa
	},

	[MTD_UADDR_0x5555_0x2AAA] = {
		.addr1 = 0x5555,
		.addr2 = 0x2aaa
	},

	[MTD_UADDR_0x0AAA_0x0555] = {
		.addr1 = 0x0AAA,
		.addr2 = 0x0555
	},

	[MTD_UADDR_DONT_CARE] = {
		.addr1 = 0x0000,      /* Doesn't matter which address */
		.addr2 = 0x0000       /* is used - must be last entry */
	},

	[MTD_UADDR_UNNECESSARY] = {
		.addr1 = 0x0000,
		.addr2 = 0x0000
	}
};


struct amd_flash_info {
	const __u16 mfr_id;
	const __u16 dev_id;
	const char *name;
	const int DevSize;
	const int NumEraseRegions;
	const int CmdSet;
	const __u8 uaddr[4];		/* unlock addrs for 8, 16, 32, 64 */
	const ulong regions[6];
};

#define ERASEINFO(size,blocks) (size<<8)|(blocks-1)

#define SIZE_64KiB  16
#define SIZE_128KiB 17
#define SIZE_256KiB 18
#define SIZE_512KiB 19
#define SIZE_1MiB   20
#define SIZE_2MiB   21
#define SIZE_4MiB   22
#define SIZE_8MiB   23


/*
 * Please keep this list ordered by manufacturer!
 * Fortunately, the list isn't searched often and so a
 * slow, linear search isn't so bad.
 */
static const struct amd_flash_info jedec_table[] = {
	{
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29F032B,
		.name		= "AMD AM29F032B",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_4MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,64)
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29LV160DT,
		.name		= "AMD AM29LV160DT",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA   /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,31),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29LV160DB,
		.name		= "AMD AM29LV160DB",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA   /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,31)
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29LV400BB,
		.name		= "AMD AM29LV400BB",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,7)
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29LV400BT,
		.name		= "AMD AM29LV400BT",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,7),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29LV800BB,
		.name		= "AMD AM29LV800BB",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,15),
		}
	}, {
/* add DL */
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29DL800BB,
		.name		= "AMD AM29DL800BB",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 6,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,4),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x04000,1),
			ERASEINFO(0x10000,14)
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29DL800BT,
		.name		= "AMD AM29DL800BT",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 6,
		.regions	= {
			ERASEINFO(0x10000,14),
			ERASEINFO(0x04000,1),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,4),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29F800BB,
		.name		= "AMD AM29F800BB",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,15),
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29LV800BT,
		.name		= "AMD AM29LV800BT",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,15),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29F800BT,
		.name		= "AMD AM29F800BT",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,15),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29F017D,
		.name		= "AMD AM29F017D",
		.uaddr		= {
			[0] = MTD_UADDR_DONT_CARE     /* x8 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,32),
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29F016D,
		.name		= "AMD AM29F016D",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,32),
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29F080,
		.name		= "AMD AM29F080",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,16),
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29F040,
		.name		= "AMD AM29F040",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,8),
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29LV040B,
		.name		= "AMD AM29LV040B",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,8),
		}
	}, {
		.mfr_id		= MANUFACTURER_AMD,
		.dev_id		= AM29F002T,
		.name		= "AMD AM29F002T",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_256KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,3),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1),
		}
	}, {
		.mfr_id		= MANUFACTURER_ATMEL,
		.dev_id		= AT49BV512,
		.name		= "Atmel AT49BV512",
		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_64KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_ATMEL,
		.dev_id		= AT29LV512,
		.name		= "Atmel AT29LV512",
		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_64KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x80,256),
			ERASEINFO(0x80,256)
		}
	}, {
		.mfr_id		= MANUFACTURER_ATMEL,
		.dev_id		= AT49BV16X,
		.name		= "Atmel AT49BV16X",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x0AAA,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x0AAA   /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000,8),
			ERASEINFO(0x10000,31)
		}
	}, {
		.mfr_id		= MANUFACTURER_ATMEL,
		.dev_id		= AT49BV16XT,
		.name		= "Atmel AT49BV16XT",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x0AAA,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x0AAA   /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000,31),
			ERASEINFO(0x02000,8)
		}
	}, {
		.mfr_id		= MANUFACTURER_ATMEL,
		.dev_id		= AT49BV32X,
		.name		= "Atmel AT49BV32X",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x0AAA,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x0AAA   /* x16 */
		},
		.DevSize	= SIZE_4MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000,8),
			ERASEINFO(0x10000,63)
		}
	}, {
		.mfr_id		= MANUFACTURER_ATMEL,
		.dev_id		= AT49BV32XT,
		.name		= "Atmel AT49BV32XT",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x0AAA,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x0AAA   /* x16 */
		},
		.DevSize	= SIZE_4MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000,63),
			ERASEINFO(0x02000,8)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29F040C,
		.name		= "Fujitsu MBM29F040C",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555, /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,8)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29LV650UE,
		.name		= "Fujitsu MBM29LV650UE",
		.uaddr		= {
			[0] = MTD_UADDR_DONT_CARE     /* x16 */
		},
		.DevSize	= SIZE_8MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,128)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29LV320TE,
		.name		= "Fujitsu MBM29LV320TE",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_4MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000,63),
			ERASEINFO(0x02000,8)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29LV320BE,
		.name		= "Fujitsu MBM29LV320BE",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_4MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000,8),
			ERASEINFO(0x10000,63)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29LV160TE,
		.name		= "Fujitsu MBM29LV160TE",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,31),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29LV160BE,
		.name		= "Fujitsu MBM29LV160BE",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,31)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29LV800BA,
		.name		= "Fujitsu MBM29LV800BA",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,15)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29LV800TA,
		.name		= "Fujitsu MBM29LV800TA",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,15),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29LV400BC,
		.name		= "Fujitsu MBM29LV400BC",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,7)
		}
	}, {
		.mfr_id		= MANUFACTURER_FUJITSU,
		.dev_id		= MBM29LV400TC,
		.name		= "Fujitsu MBM29LV400TC",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,7),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_HYUNDAI,
		.dev_id		= HY29F002T,
		.name		= "Hyundai HY29F002T",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_256KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,3),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F004B3B,
		.name		= "Intel 28F004B3B",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000, 8),
			ERASEINFO(0x10000, 7),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F004B3T,
		.name		= "Intel 28F004B3T",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000, 7),
			ERASEINFO(0x02000, 8),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F400B3B,
		.name		= "Intel 28F400B3B",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000, 8),
			ERASEINFO(0x10000, 7),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F400B3T,
		.name		= "Intel 28F400B3T",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000, 7),
			ERASEINFO(0x02000, 8),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F008B3B,
		.name		= "Intel 28F008B3B",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000, 8),
			ERASEINFO(0x10000, 15),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F008B3T,
		.name		= "Intel 28F008B3T",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000, 15),
			ERASEINFO(0x02000, 8),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F008S5,
		.name		= "Intel 28F008S5",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_INTEL_EXT,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,16),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F016S5,
		.name		= "Intel 28F016S5",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_INTEL_EXT,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,32),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F008SA,
		.name		= "Intel 28F008SA",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000, 16),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F800B3B,
		.name		= "Intel 28F800B3B",
		.uaddr		= {
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000, 8),
			ERASEINFO(0x10000, 15),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F800B3T,
		.name		= "Intel 28F800B3T",
		.uaddr		= {
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000, 15),
			ERASEINFO(0x02000, 8),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F016B3B,
		.name		= "Intel 28F016B3B",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000, 8),
			ERASEINFO(0x10000, 31),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F016S3,
		.name		= "Intel I28F016S3",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000, 32),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F016B3T,
		.name		= "Intel 28F016B3T",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000, 31),
			ERASEINFO(0x02000, 8),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F160B3B,
		.name		= "Intel 28F160B3B",
		.uaddr		= {
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000, 8),
			ERASEINFO(0x10000, 31),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F160B3T,
		.name		= "Intel 28F160B3T",
		.uaddr		= {
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000, 31),
			ERASEINFO(0x02000, 8),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F320B3B,
		.name		= "Intel 28F320B3B",
		.uaddr		= {
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_4MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000, 8),
			ERASEINFO(0x10000, 63),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F320B3T,
		.name		= "Intel 28F320B3T",
		.uaddr		= {
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_4MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000, 63),
			ERASEINFO(0x02000, 8),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F640B3B,
		.name		= "Intel 28F640B3B",
		.uaddr		= {
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_8MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000, 8),
			ERASEINFO(0x10000, 127),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I28F640B3T,
		.name		= "Intel 28F640B3T",
		.uaddr		= {
			[1] = MTD_UADDR_UNNECESSARY,    /* x16 */
		},
		.DevSize	= SIZE_8MiB,
		.CmdSet		= P_ID_INTEL_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000, 127),
			ERASEINFO(0x02000, 8),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I82802AB,
		.name		= "Intel 82802AB",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_INTEL_EXT,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,8),
		}
	}, {
		.mfr_id		= MANUFACTURER_INTEL,
		.dev_id		= I82802AC,
		.name		= "Intel 82802AC",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_INTEL_EXT,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,16),
		}
	}, {
		.mfr_id		= MANUFACTURER_MACRONIX,
		.dev_id		= MX29LV040C,
		.name		= "Macronix MX29LV040C",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA,  /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,8),
		}
	}, {
		.mfr_id		= MANUFACTURER_MACRONIX,
		.dev_id		= MX29LV160T,
		.name		= "MXIC MX29LV160T",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,31),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_NEC,
		.dev_id		= UPD29F064115,
		.name		= "NEC uPD29F064115",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_8MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 3,
		.regions	= {
			ERASEINFO(0x2000,8),
			ERASEINFO(0x10000,126),
			ERASEINFO(0x2000,8),
		}
	}, {
		.mfr_id		= MANUFACTURER_MACRONIX,
		.dev_id		= MX29LV160B,
		.name		= "MXIC MX29LV160B",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,31)
		}
	}, {
		.mfr_id		= MANUFACTURER_MACRONIX,
		.dev_id		= MX29F016,
		.name		= "Macronix MX29F016",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,32),
		}
        }, {
		.mfr_id		= MANUFACTURER_MACRONIX,
		.dev_id		= MX29F004T,
		.name		= "Macronix MX29F004T",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,7),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1),
		}
        }, {
		.mfr_id		= MANUFACTURER_MACRONIX,
		.dev_id		= MX29F004B,
		.name		= "Macronix MX29F004B",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,7),
		}
	}, {
		.mfr_id		= MANUFACTURER_MACRONIX,
		.dev_id		= MX29F002T,
		.name		= "Macronix MX29F002T",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_256KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,3),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1),
		}
	}, {
		.mfr_id		= MANUFACTURER_PMC,
		.dev_id		= PM49FL002,
		.name		= "PMC Pm49FL002",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_256KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO( 0x01000, 64 )
		}
	}, {
		.mfr_id		= MANUFACTURER_PMC,
		.dev_id		= PM49FL004,
		.name		= "PMC Pm49FL004",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO( 0x01000, 128 )
		}
	}, {
		.mfr_id		= MANUFACTURER_PMC,
		.dev_id		= PM49FL008,
		.name		= "PMC Pm49FL008",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO( 0x01000, 256 )
		}
        }, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST39LF512,
		.name		= "SST 39LF512",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_64KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,16),
		}
        }, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST39LF010,
		.name		= "SST 39LF010",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_128KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,32),
		}
        }, {
		.mfr_id		= MANUFACTURER_SST,
 		.dev_id 	= SST29EE020,
		.name		= "SST 29EE020",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
 		.DevSize	= SIZE_256KiB,
 		.CmdSet		= P_ID_SST_PAGE,
 		.NumEraseRegions= 1,
 		.regions = {ERASEINFO(0x01000,64),
 		}
         }, {
 		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST29LE020,
 		.name		= "SST 29LE020",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
 		.DevSize	= SIZE_256KiB,
 		.CmdSet		= P_ID_SST_PAGE,
 		.NumEraseRegions= 1,
 		.regions = {ERASEINFO(0x01000,64),
 		}
	}, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST39LF020,
		.name		= "SST 39LF020",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_256KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,64),
		}
        }, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST39LF040,
		.name		= "SST 39LF040",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,128),
		}
        }, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST39SF010A,
		.name		= "SST 39SF010A",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_128KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,32),
		}
        }, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST39SF020A,
		.name		= "SST 39SF020A",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_256KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,64),
		}
	}, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST49LF004B,
		.name		= "SST 49LF004B",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,128),
		}
	}, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST49LF008A,
		.name		= "SST 49LF008A",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,256),
		}
	}, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST49LF030A,
		.name		= "SST 49LF030A",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,96),
		}
	}, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST49LF040A,
		.name		= "SST 49LF040A",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,128),
		}
	}, {
		.mfr_id		= MANUFACTURER_SST,
		.dev_id		= SST49LF080A,
		.name		= "SST 49LF080A",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x01000,256),
		}
	}, {
               .mfr_id         = MANUFACTURER_SST,     /* should be CFI */
               .dev_id         = SST39LF160,
               .name           = "SST 39LF160",
               .uaddr          = {
                       [0] = MTD_UADDR_0x5555_0x2AAA,  /* x8 */
                       [1] = MTD_UADDR_0x5555_0x2AAA   /* x16 */
               },
               .DevSize        = SIZE_2MiB,
               .CmdSet         = P_ID_AMD_STD,
               .NumEraseRegions= 2,
               .regions        = {
                       ERASEINFO(0x1000,256),
                       ERASEINFO(0x1000,256)
               }
	}, {
               .mfr_id         = MANUFACTURER_SST,     /* should be CFI */
               .dev_id         = SST39VF1601,
               .name           = "SST 39VF1601",
               .uaddr          = {
                       [0] = MTD_UADDR_0x5555_0x2AAA,  /* x8 */
                       [1] = MTD_UADDR_0x5555_0x2AAA   /* x16 */
               },
               .DevSize        = SIZE_2MiB,
               .CmdSet         = P_ID_AMD_STD,
               .NumEraseRegions= 2,
               .regions        = {
                       ERASEINFO(0x1000,256),
                       ERASEINFO(0x1000,256)
               }

       }, {
		.mfr_id		= MANUFACTURER_ST,	/* FIXME - CFI device? */
		.dev_id		= M29W800DT,
		.name		= "ST M29W800DT",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA,  /* x8 */
			[1] = MTD_UADDR_0x5555_0x2AAA   /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,15),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_ST,	/* FIXME - CFI device? */
		.dev_id		= M29W800DB,
		.name		= "ST M29W800DB",
 		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA,  /* x8 */
			[1] = MTD_UADDR_0x5555_0x2AAA   /* x16 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,15)
		}
	}, {
		.mfr_id		= MANUFACTURER_ST,	/* FIXME - CFI device? */
		.dev_id		= M29W160DT,
		.name		= "ST M29W160DT",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,31),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_ST,	/* FIXME - CFI device? */
		.dev_id		= M29W160DB,
		.name		= "ST M29W160DB",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA,  /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA,  /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,31)
		}
        }, {
		.mfr_id		= MANUFACTURER_ST,
		.dev_id		= M29W040B,
		.name		= "ST M29W040B",
		.uaddr		= {
			[0] = MTD_UADDR_0x0555_0x02AA /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,8),
		}
        }, {
		.mfr_id		= MANUFACTURER_ST,
		.dev_id		= M50FW040,
		.name		= "ST M50FW040",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_512KiB,
		.CmdSet		= P_ID_INTEL_EXT,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,8),
		}
        }, {
		.mfr_id		= MANUFACTURER_ST,
		.dev_id		= M50FW080,
		.name		= "ST M50FW080",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_INTEL_EXT,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,16),
		}
        }, {
		.mfr_id		= MANUFACTURER_ST,
		.dev_id		= M50FW016,
		.name		= "ST M50FW016",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_INTEL_EXT,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,32),
		}
	}, {
		.mfr_id		= MANUFACTURER_ST,
		.dev_id		= M50LPW080,
		.name		= "ST M50LPW080",
		.uaddr		= {
			[0] = MTD_UADDR_UNNECESSARY,    /* x8 */
		},
		.DevSize	= SIZE_1MiB,
		.CmdSet		= P_ID_INTEL_EXT,
		.NumEraseRegions= 1,
		.regions	= {
			ERASEINFO(0x10000,16),
		}
	}, {
		.mfr_id		= MANUFACTURER_TOSHIBA,
		.dev_id		= TC58FVT160,
		.name		= "Toshiba TC58FVT160",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555, /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA  /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000,31),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x04000,1)
		}
	}, {
		.mfr_id		= MANUFACTURER_TOSHIBA,
		.dev_id		= TC58FVB160,
		.name		= "Toshiba TC58FVB160",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555, /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA  /* x16 */
		},
		.DevSize	= SIZE_2MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x04000,1),
			ERASEINFO(0x02000,2),
			ERASEINFO(0x08000,1),
			ERASEINFO(0x10000,31)
		}
	}, {
		.mfr_id		= MANUFACTURER_TOSHIBA,
		.dev_id		= TC58FVB321,
		.name		= "Toshiba TC58FVB321",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555, /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA  /* x16 */
		},
		.DevSize	= SIZE_4MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000,8),
			ERASEINFO(0x10000,63)
		}
	}, {
		.mfr_id		= MANUFACTURER_TOSHIBA,
		.dev_id		= TC58FVT321,
		.name		= "Toshiba TC58FVT321",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555, /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA  /* x16 */
		},
		.DevSize	= SIZE_4MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000,63),
			ERASEINFO(0x02000,8)
		}
	}, {
		.mfr_id		= MANUFACTURER_TOSHIBA,
		.dev_id		= TC58FVB641,
		.name		= "Toshiba TC58FVB641",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555, /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA, /* x16 */
		},
		.DevSize	= SIZE_8MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x02000,8),
			ERASEINFO(0x10000,127)
		}
	}, {
		.mfr_id		= MANUFACTURER_TOSHIBA,
		.dev_id		= TC58FVT641,
		.name		= "Toshiba TC58FVT641",
		.uaddr		= {
			[0] = MTD_UADDR_0x0AAA_0x0555, /* x8 */
			[1] = MTD_UADDR_0x0555_0x02AA, /* x16 */
		},
		.DevSize	= SIZE_8MiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 2,
		.regions	= {
			ERASEINFO(0x10000,127),
			ERASEINFO(0x02000,8)
		}
	}, {
		.mfr_id		= MANUFACTURER_WINBOND,
		.dev_id		= W49V002A,
		.name		= "Winbond W49V002A",
		.uaddr		= {
			[0] = MTD_UADDR_0x5555_0x2AAA /* x8 */
		},
		.DevSize	= SIZE_256KiB,
		.CmdSet		= P_ID_AMD_STD,
		.NumEraseRegions= 4,
		.regions	= {
			ERASEINFO(0x10000, 3),
			ERASEINFO(0x08000, 1),
			ERASEINFO(0x02000, 2),
			ERASEINFO(0x04000, 1),
		}
	}
};


static int cfi_jedec_setup(struct cfi_private *p_cfi, int index);

static int jedec_probe_chip(struct map_info *map, __u32 base,
			    unsigned long *chip_map, struct cfi_private *cfi);

static struct mtd_info *jedec_probe(struct map_info *map);

static inline u32 jedec_read_mfr(struct map_info *map, __u32 base,
	struct cfi_private *cfi)
{
	map_word result;
	unsigned long mask;
	u32 ofs = cfi_build_cmd_addr(0, cfi_interleave(cfi), cfi->device_type);
	mask = (1 << (cfi->device_type * 8)) -1;
	result = map_read(map, base + ofs);
	return result.x[0] & mask;
}

static inline u32 jedec_read_id(struct map_info *map, __u32 base,
	struct cfi_private *cfi)
{
	map_word result;
	unsigned long mask;
	u32 ofs = cfi_build_cmd_addr(1, cfi_interleave(cfi), cfi->device_type);
	mask = (1 << (cfi->device_type * 8)) -1;
	result = map_read(map, base + ofs);
	return result.x[0] & mask;
}

static inline void jedec_reset(u32 base, struct map_info *map,
	struct cfi_private *cfi)
{
	/* Reset */

	/* after checking the datasheets for SST, MACRONIX and ATMEL
	 * (oh and incidentaly the jedec spec - 3.5.3.3) the reset
	 * sequence is *supposed* to be 0xaa at 0x5555, 0x55 at
	 * 0x2aaa, 0xF0 at 0x5555 this will not affect the AMD chips
	 * as they will ignore the writes and dont care what address
	 * the F0 is written to */
	if(cfi->addr_unlock1) {
		DEBUG( MTD_DEBUG_LEVEL3,
		       "reset unlock called %x %x \n",
		       cfi->addr_unlock1,cfi->addr_unlock2);
		cfi_send_gen_cmd(0xaa, cfi->addr_unlock1, base, map, cfi, cfi->device_type, NULL);
		cfi_send_gen_cmd(0x55, cfi->addr_unlock2, base, map, cfi, cfi->device_type, NULL);
	}

	cfi_send_gen_cmd(0xF0, cfi->addr_unlock1, base, map, cfi, cfi->device_type, NULL);
	/* Some misdesigned intel chips do not respond for 0xF0 for a reset,
	 * so ensure we're in read mode.  Send both the Intel and the AMD command
	 * for this.  Intel uses 0xff for this, AMD uses 0xff for NOP, so
	 * this should be safe.
	 */
	cfi_send_gen_cmd(0xFF, 0, base, map, cfi, cfi->device_type, NULL);
	/* FIXME - should have reset delay before continuing */
}


static inline __u8 finfo_uaddr(const struct amd_flash_info *finfo, int device_type)
{
	int uaddr_idx;
	__u8 uaddr = MTD_UADDR_NOT_SUPPORTED;

	switch ( device_type ) {
	case CFI_DEVICETYPE_X8:  uaddr_idx = 0; break;
	case CFI_DEVICETYPE_X16: uaddr_idx = 1; break;
	case CFI_DEVICETYPE_X32: uaddr_idx = 2; break;
	default:
		printk(KERN_NOTICE "MTD: %s(): unknown device_type %d\n",
		       __func__, device_type);
		goto uaddr_done;
	}

	uaddr = finfo->uaddr[uaddr_idx];

	if (uaddr != MTD_UADDR_NOT_SUPPORTED ) {
		/* ASSERT("The unlock addresses for non-8-bit mode
		   are bollocks. We don't really need an array."); */
		uaddr = finfo->uaddr[0];
	}

 uaddr_done:
	return uaddr;
}


static int cfi_jedec_setup(struct cfi_private *p_cfi, int index)
{
	int i,num_erase_regions;
	__u8 uaddr;

	printk("Found: %s\n",jedec_table[index].name);

	num_erase_regions = jedec_table[index].NumEraseRegions;

	p_cfi->cfiq = kmalloc(sizeof(struct cfi_ident) + num_erase_regions * 4, GFP_KERNEL);
	if (!p_cfi->cfiq) {
		//xx printk(KERN_WARNING "%s: kmalloc failed for CFI ident structure\n", map->name);
		return 0;
	}

	memset(p_cfi->cfiq,0,sizeof(struct cfi_ident));

	p_cfi->cfiq->P_ID = jedec_table[index].CmdSet;
	p_cfi->cfiq->NumEraseRegions = jedec_table[index].NumEraseRegions;
	p_cfi->cfiq->DevSize = jedec_table[index].DevSize;
	p_cfi->cfi_mode = CFI_MODE_JEDEC;

	for (i=0; i<num_erase_regions; i++){
		p_cfi->cfiq->EraseRegionInfo[i] = jedec_table[index].regions[i];
	}
	p_cfi->cmdset_priv = NULL;

	/* This may be redundant for some cases, but it doesn't hurt */
	p_cfi->mfr = jedec_table[index].mfr_id;
	p_cfi->id = jedec_table[index].dev_id;

	uaddr = finfo_uaddr(&jedec_table[index], p_cfi->device_type);
	if ( uaddr == MTD_UADDR_NOT_SUPPORTED ) {
		kfree( p_cfi->cfiq );
		return 0;
	}

	p_cfi->addr_unlock1 = unlock_addrs[uaddr].addr1;
	p_cfi->addr_unlock2 = unlock_addrs[uaddr].addr2;

	return 1; 	/* ok */
}


/*
 * There is a BIG problem properly ID'ing the JEDEC devic and guaranteeing
 * the mapped address, unlock addresses, and proper chip ID.  This function
 * attempts to minimize errors.  It is doubtfull that this probe will ever
 * be perfect - consequently there should be some module parameters that
 * could be manually specified to force the chip info.
 */
static inline int jedec_match( __u32 base,
			       struct map_info *map,
			       struct cfi_private *cfi,
			       const struct amd_flash_info *finfo )
{
	int rc = 0;           /* failure until all tests pass */
	u32 mfr, id;
	__u8 uaddr;

	/*
	 * The IDs must match.  For X16 and X32 devices operating in
	 * a lower width ( X8 or X16 ), the device ID's are usually just
	 * the lower byte(s) of the larger device ID for wider mode.  If
	 * a part is found that doesn't fit this assumption (device id for
	 * smaller width mode is completely unrealated to full-width mode)
	 * then the jedec_table[] will have to be augmented with the IDs
	 * for different widths.
	 */
	switch (cfi->device_type) {
	case CFI_DEVICETYPE_X8:
		mfr = (__u8)finfo->mfr_id;
		id = (__u8)finfo->dev_id;

		/* bjd: it seems that if we do this, we can end up
		 * detecting 16bit flashes as an 8bit device, even though
		 * there aren't.
		 */
		if (finfo->dev_id > 0xff) {
			DEBUG( MTD_DEBUG_LEVEL3, "%s(): ID is not 8bit\n",
			       __func__);
			goto match_done;
		}
		break;
	case CFI_DEVICETYPE_X16:
		mfr = (__u16)finfo->mfr_id;
		id = (__u16)finfo->dev_id;
		break;
	case CFI_DEVICETYPE_X32:
		mfr = (__u16)finfo->mfr_id;
		id = (__u32)finfo->dev_id;
		break;
	default:
		printk(KERN_WARNING
		       "MTD %s(): Unsupported device type %d\n",
		       __func__, cfi->device_type);
		goto match_done;
	}
	if ( cfi->mfr != mfr || cfi->id != id ) {
		goto match_done;
	}

	/* the part size must fit in the memory window */
	DEBUG( MTD_DEBUG_LEVEL3,
	       "MTD %s(): Check fit 0x%.8x + 0x%.8x = 0x%.8x\n",
	       __func__, base, 1 << finfo->DevSize, base + (1 << finfo->DevSize) );
	if ( base + cfi_interleave(cfi) * ( 1 << finfo->DevSize ) > map->size ) {
		DEBUG( MTD_DEBUG_LEVEL3,
		       "MTD %s(): 0x%.4x 0x%.4x %dKiB doesn't fit\n",
		       __func__, finfo->mfr_id, finfo->dev_id,
		       1 << finfo->DevSize );
		goto match_done;
	}

	uaddr = finfo_uaddr(finfo, cfi->device_type);
	if ( uaddr == MTD_UADDR_NOT_SUPPORTED ) {
		goto match_done;
	}

	DEBUG( MTD_DEBUG_LEVEL3, "MTD %s(): check unlock addrs 0x%.4x 0x%.4x\n",
	       __func__, cfi->addr_unlock1, cfi->addr_unlock2 );
	if ( MTD_UADDR_UNNECESSARY != uaddr && MTD_UADDR_DONT_CARE != uaddr
	     && ( unlock_addrs[uaddr].addr1 != cfi->addr_unlock1 ||
		  unlock_addrs[uaddr].addr2 != cfi->addr_unlock2 ) ) {
		DEBUG( MTD_DEBUG_LEVEL3,
			"MTD %s(): 0x%.4x 0x%.4x did not match\n",
			__func__,
			unlock_addrs[uaddr].addr1,
			unlock_addrs[uaddr].addr2);
		goto match_done;
	}

	/*
	 * Make sure the ID's dissappear when the device is taken out of
	 * ID mode.  The only time this should fail when it should succeed
	 * is when the ID's are written as data to the same
	 * addresses.  For this rare and unfortunate case the chip
	 * cannot be probed correctly.
	 * FIXME - write a driver that takes all of the chip info as
	 * module parameters, doesn't probe but forces a load.
	 */
	DEBUG( MTD_DEBUG_LEVEL3,
	       "MTD %s(): check ID's disappear when not in ID mode\n",
	       __func__ );
	jedec_reset( base, map, cfi );
	mfr = jedec_read_mfr( map, base, cfi );
	id = jedec_read_id( map, base, cfi );
	if ( mfr == cfi->mfr && id == cfi->id ) {
		DEBUG( MTD_DEBUG_LEVEL3,
		       "MTD %s(): ID 0x%.2x:0x%.2x did not change after reset:\n"
		       "You might need to manually specify JEDEC parameters.\n",
			__func__, cfi->mfr, cfi->id );
		goto match_done;
	}

	/* all tests passed - mark  as success */
	rc = 1;

	/*
	 * Put the device back in ID mode - only need to do this if we
	 * were truly frobbing a real device.
	 */
	DEBUG( MTD_DEBUG_LEVEL3, "MTD %s(): return to ID mode\n", __func__ );
	if(cfi->addr_unlock1) {
		cfi_send_gen_cmd(0xaa, cfi->addr_unlock1, base, map, cfi, cfi->device_type, NULL);
		cfi_send_gen_cmd(0x55, cfi->addr_unlock2, base, map, cfi, cfi->device_type, NULL);
	}
	cfi_send_gen_cmd(0x90, cfi->addr_unlock1, base, map, cfi, cfi->device_type, NULL);
	/* FIXME - should have a delay before continuing */

 match_done:
	return rc;
}


static int jedec_probe_chip(struct map_info *map, __u32 base,
			    unsigned long *chip_map, struct cfi_private *cfi)
{
	int i;
	enum uaddr uaddr_idx = MTD_UADDR_NOT_SUPPORTED;
	u32 probe_offset1, probe_offset2;

 retry:
	if (!cfi->numchips) {
		uaddr_idx++;

		if (MTD_UADDR_UNNECESSARY == uaddr_idx)
			return 0;

		cfi->addr_unlock1 = unlock_addrs[uaddr_idx].addr1;
		cfi->addr_unlock2 = unlock_addrs[uaddr_idx].addr2;
	}

	/* Make certain we aren't probing past the end of map */
	if (base >= map->size) {
		printk(KERN_NOTICE
			"Probe at base(0x%08x) past the end of the map(0x%08lx)\n",
			base, map->size -1);
		return 0;

	}
	/* Ensure the unlock addresses we try stay inside the map */
	probe_offset1 = cfi_build_cmd_addr(
		cfi->addr_unlock1,
		cfi_interleave(cfi),
		cfi->device_type);
	probe_offset2 = cfi_build_cmd_addr(
		cfi->addr_unlock1,
		cfi_interleave(cfi),
		cfi->device_type);
	if (	((base + probe_offset1 + map_bankwidth(map)) >= map->size) ||
		((base + probe_offset2 + map_bankwidth(map)) >= map->size))
	{
		goto retry;
	}

	/* Reset */
	jedec_reset(base, map, cfi);

	/* Autoselect Mode */
	if(cfi->addr_unlock1) {
		cfi_send_gen_cmd(0xaa, cfi->addr_unlock1, base, map, cfi, cfi->device_type, NULL);
		cfi_send_gen_cmd(0x55, cfi->addr_unlock2, base, map, cfi, cfi->device_type, NULL);
	}
	cfi_send_gen_cmd(0x90, cfi->addr_unlock1, base, map, cfi, cfi->device_type, NULL);
	/* FIXME - should have a delay before continuing */

	if (!cfi->numchips) {
		/* This is the first time we're called. Set up the CFI
		   stuff accordingly and return */

		cfi->mfr = jedec_read_mfr(map, base, cfi);
		cfi->id = jedec_read_id(map, base, cfi);
		DEBUG(MTD_DEBUG_LEVEL3,
		      "Search for id:(%02x %02x) interleave(%d) type(%d)\n",
			cfi->mfr, cfi->id, cfi_interleave(cfi), cfi->device_type);
		for (i=0; i<sizeof(jedec_table)/sizeof(jedec_table[0]); i++) {
			if ( jedec_match( base, map, cfi, &jedec_table[i] ) ) {
				DEBUG( MTD_DEBUG_LEVEL3,
				       "MTD %s(): matched device 0x%x,0x%x unlock_addrs: 0x%.4x 0x%.4x\n",
				       __func__, cfi->mfr, cfi->id,
				       cfi->addr_unlock1, cfi->addr_unlock2 );
				if (!cfi_jedec_setup(cfi, i))
					return 0;
				goto ok_out;
			}
		}
		goto retry;
	} else {
		__u16 mfr;
		__u16 id;

		/* Make sure it is a chip of the same manufacturer and id */
		mfr = jedec_read_mfr(map, base, cfi);
		id = jedec_read_id(map, base, cfi);

		if ((mfr != cfi->mfr) || (id != cfi->id)) {
			printk(KERN_DEBUG "%s: Found different chip or no chip at all (mfr 0x%x, id 0x%x) at 0x%x\n",
			       map->name, mfr, id, base);
			jedec_reset(base, map, cfi);
			return 0;
		}
	}

	/* Check each previous chip locations to see if it's an alias */
	for (i=0; i < (base >> cfi->chipshift); i++) {
		unsigned long start;
		if(!test_bit(i, chip_map)) {
			continue; /* Skip location; no valid chip at this address */
		}
		start = i << cfi->chipshift;
		if (jedec_read_mfr(map, start, cfi) == cfi->mfr &&
		    jedec_read_id(map, start, cfi) == cfi->id) {
			/* Eep. This chip also looks like it's in autoselect mode.
			   Is it an alias for the new one? */
			jedec_reset(start, map, cfi);

			/* If the device IDs go away, it's an alias */
			if (jedec_read_mfr(map, base, cfi) != cfi->mfr ||
			    jedec_read_id(map, base, cfi) != cfi->id) {
				printk(KERN_DEBUG "%s: Found an alias at 0x%x for the chip at 0x%lx\n",
				       map->name, base, start);
				return 0;
			}

			/* Yes, it's actually got the device IDs as data. Most
			 * unfortunate. Stick the new chip in read mode
			 * too and if it's the same, assume it's an alias. */
			/* FIXME: Use other modes to do a proper check */
			jedec_reset(base, map, cfi);
			if (jedec_read_mfr(map, base, cfi) == cfi->mfr &&
			    jedec_read_id(map, base, cfi) == cfi->id) {
				printk(KERN_DEBUG "%s: Found an alias at 0x%x for the chip at 0x%lx\n",
				       map->name, base, start);
				return 0;
			}
		}
	}

	/* OK, if we got to here, then none of the previous chips appear to
	   be aliases for the current one. */
	set_bit((base >> cfi->chipshift), chip_map); /* Update chip map */
	cfi->numchips++;

ok_out:
	/* Put it back into Read Mode */
	jedec_reset(base, map, cfi);

	printk(KERN_INFO "%s: Found %d x%d devices at 0x%x in %d-bit bank\n",
	       map->name, cfi_interleave(cfi), cfi->device_type*8, base,
	       map->bankwidth*8);

	return 1;
}

static struct chip_probe jedec_chip_probe = {
	.name = "JEDEC",
	.probe_chip = jedec_probe_chip
};

static struct mtd_info *jedec_probe(struct map_info *map)
{
	/*
	 * Just use the generic probe stuff to call our CFI-specific
	 * chip_probe routine in all the possible permutations, etc.
	 */
	return mtd_do_chip_probe(map, &jedec_chip_probe);
}

static struct mtd_chip_driver jedec_chipdrv = {
	.probe	= jedec_probe,
	.name	= "jedec_probe",
	.module	= THIS_MODULE
};

static int __init jedec_probe_init(void)
{
	register_mtd_chip_driver(&jedec_chipdrv);
	return 0;
}

static void __exit jedec_probe_exit(void)
{
	unregister_mtd_chip_driver(&jedec_chipdrv);
}

module_init(jedec_probe_init);
module_exit(jedec_probe_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Erwin Authried <eauth@softsys.co.at> et al.");
MODULE_DESCRIPTION("Probe code for JEDEC-compliant flash chips");
