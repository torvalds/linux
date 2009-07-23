#include <linux/module.h>
#include "edac_mce_amd.h"

/*
 * string representation for the different MCA reported error types, see F3x48
 * or MSR0000_0411.
 */
const char *tt_msgs[] = {        /* transaction type */
	"instruction",
	"data",
	"generic",
	"reserved"
};
EXPORT_SYMBOL_GPL(tt_msgs);

const char *ll_msgs[] = {	/* cache level */
	"L0",
	"L1",
	"L2",
	"L3/generic"
};
EXPORT_SYMBOL_GPL(ll_msgs);

const char *rrrr_msgs[] = {
	"generic",
	"generic read",
	"generic write",
	"data read",
	"data write",
	"inst fetch",
	"prefetch",
	"evict",
	"snoop",
	"reserved RRRR= 9",
	"reserved RRRR= 10",
	"reserved RRRR= 11",
	"reserved RRRR= 12",
	"reserved RRRR= 13",
	"reserved RRRR= 14",
	"reserved RRRR= 15"
};
EXPORT_SYMBOL_GPL(rrrr_msgs);

const char *pp_msgs[] = {	/* participating processor */
	"local node originated (SRC)",
	"local node responded to request (RES)",
	"local node observed as 3rd party (OBS)",
	"generic"
};
EXPORT_SYMBOL_GPL(pp_msgs);

const char *to_msgs[] = {
	"no timeout",
	"timed out"
};
EXPORT_SYMBOL_GPL(to_msgs);

const char *ii_msgs[] = {	/* memory or i/o */
	"mem access",
	"reserved",
	"i/o access",
	"generic"
};
EXPORT_SYMBOL_GPL(ii_msgs);

/*
 * Map the 4 or 5 (family-specific) bits of Extended Error code to the
 * string table.
 */
const char *ext_msgs[] = {
	"K8 ECC error",					/* 0_0000b */
	"CRC error on link",				/* 0_0001b */
	"Sync error packets on link",			/* 0_0010b */
	"Master Abort during link operation",		/* 0_0011b */
	"Target Abort during link operation",		/* 0_0100b */
	"Invalid GART PTE entry during table walk",	/* 0_0101b */
	"Unsupported atomic RMW command received",	/* 0_0110b */
	"WDT error: NB transaction timeout",		/* 0_0111b */
	"ECC/ChipKill ECC error",			/* 0_1000b */
	"SVM DEV Error",				/* 0_1001b */
	"Link Data error",				/* 0_1010b */
	"Link/L3/Probe Filter Protocol error",		/* 0_1011b */
	"NB Internal Arrays Parity error",		/* 0_1100b */
	"DRAM Address/Control Parity error",		/* 0_1101b */
	"Link Transmission error",			/* 0_1110b */
	"GART/DEV Table Walk Data error"		/* 0_1111b */
	"Res 0x100 error",				/* 1_0000b */
	"Res 0x101 error",				/* 1_0001b */
	"Res 0x102 error",				/* 1_0010b */
	"Res 0x103 error",				/* 1_0011b */
	"Res 0x104 error",				/* 1_0100b */
	"Res 0x105 error",				/* 1_0101b */
	"Res 0x106 error",				/* 1_0110b */
	"Res 0x107 error",				/* 1_0111b */
	"Res 0x108 error",				/* 1_1000b */
	"Res 0x109 error",				/* 1_1001b */
	"Res 0x10A error",				/* 1_1010b */
	"Res 0x10B error",				/* 1_1011b */
	"ECC error in L3 Cache Data",			/* 1_1100b */
	"L3 Cache Tag error",				/* 1_1101b */
	"L3 Cache LRU Parity error",			/* 1_1110b */
	"Probe Filter error"				/* 1_1111b */
};
EXPORT_SYMBOL_GPL(ext_msgs);
