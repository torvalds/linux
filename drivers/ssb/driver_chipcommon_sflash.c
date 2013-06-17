/*
 * Sonics Silicon Backplane
 * ChipCommon serial flash interface
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/ssb/ssb.h>

#include "ssb_private.h"

struct ssb_sflash_tbl_e {
	char *name;
	u32 id;
	u32 blocksize;
	u16 numblocks;
};

static const struct ssb_sflash_tbl_e ssb_sflash_st_tbl[] = {
	{ "M25P20", 0x11, 0x10000, 4, },
	{ "M25P40", 0x12, 0x10000, 8, },

	{ "M25P16", 0x14, 0x10000, 32, },
	{ "M25P32", 0x15, 0x10000, 64, },
	{ "M25P64", 0x16, 0x10000, 128, },
	{ "M25FL128", 0x17, 0x10000, 256, },
	{ 0 },
};

static const struct ssb_sflash_tbl_e ssb_sflash_sst_tbl[] = {
	{ "SST25WF512", 1, 0x1000, 16, },
	{ "SST25VF512", 0x48, 0x1000, 16, },
	{ "SST25WF010", 2, 0x1000, 32, },
	{ "SST25VF010", 0x49, 0x1000, 32, },
	{ "SST25WF020", 3, 0x1000, 64, },
	{ "SST25VF020", 0x43, 0x1000, 64, },
	{ "SST25WF040", 4, 0x1000, 128, },
	{ "SST25VF040", 0x44, 0x1000, 128, },
	{ "SST25VF040B", 0x8d, 0x1000, 128, },
	{ "SST25WF080", 5, 0x1000, 256, },
	{ "SST25VF080B", 0x8e, 0x1000, 256, },
	{ "SST25VF016", 0x41, 0x1000, 512, },
	{ "SST25VF032", 0x4a, 0x1000, 1024, },
	{ "SST25VF064", 0x4b, 0x1000, 2048, },
	{ 0 },
};

static const struct ssb_sflash_tbl_e ssb_sflash_at_tbl[] = {
	{ "AT45DB011", 0xc, 256, 512, },
	{ "AT45DB021", 0x14, 256, 1024, },
	{ "AT45DB041", 0x1c, 256, 2048, },
	{ "AT45DB081", 0x24, 256, 4096, },
	{ "AT45DB161", 0x2c, 512, 4096, },
	{ "AT45DB321", 0x34, 512, 8192, },
	{ "AT45DB642", 0x3c, 1024, 8192, },
	{ 0 },
};

static void ssb_sflash_cmd(struct ssb_chipcommon *cc, u32 opcode)
{
	int i;
	chipco_write32(cc, SSB_CHIPCO_FLASHCTL,
		       SSB_CHIPCO_FLASHCTL_START | opcode);
	for (i = 0; i < 1000; i++) {
		if (!(chipco_read32(cc, SSB_CHIPCO_FLASHCTL) &
		      SSB_CHIPCO_FLASHCTL_BUSY))
			return;
		cpu_relax();
	}
	pr_err("SFLASH control command failed (timeout)!\n");
}

/* Initialize serial flash access */
int ssb_sflash_init(struct ssb_chipcommon *cc)
{
	const struct ssb_sflash_tbl_e *e;
	u32 id, id2;

	switch (cc->capabilities & SSB_CHIPCO_CAP_FLASHT) {
	case SSB_CHIPCO_FLASHT_STSER:
		ssb_sflash_cmd(cc, SSB_CHIPCO_FLASHCTL_ST_DP);

		chipco_write32(cc, SSB_CHIPCO_FLASHADDR, 0);
		ssb_sflash_cmd(cc, SSB_CHIPCO_FLASHCTL_ST_RES);
		id = chipco_read32(cc, SSB_CHIPCO_FLASHDATA);

		chipco_write32(cc, SSB_CHIPCO_FLASHADDR, 1);
		ssb_sflash_cmd(cc, SSB_CHIPCO_FLASHCTL_ST_RES);
		id2 = chipco_read32(cc, SSB_CHIPCO_FLASHDATA);

		switch (id) {
		case 0xbf:
			for (e = ssb_sflash_sst_tbl; e->name; e++) {
				if (e->id == id2)
					break;
			}
			break;
		case 0x13:
			return -ENOTSUPP;
		default:
			for (e = ssb_sflash_st_tbl; e->name; e++) {
				if (e->id == id)
					break;
			}
			break;
		}
		if (!e->name) {
			pr_err("Unsupported ST serial flash (id: 0x%X, id2: 0x%X)\n",
			       id, id2);
			return -ENOTSUPP;
		}

		break;
	case SSB_CHIPCO_FLASHT_ATSER:
		ssb_sflash_cmd(cc, SSB_CHIPCO_FLASHCTL_AT_STATUS);
		id = chipco_read32(cc, SSB_CHIPCO_FLASHDATA) & 0x3c;

		for (e = ssb_sflash_at_tbl; e->name; e++) {
			if (e->id == id)
				break;
		}
		if (!e->name) {
			pr_err("Unsupported Atmel serial flash (id: 0x%X)\n",
			       id);
			return -ENOTSUPP;
		}

		break;
	default:
		pr_err("Unsupported flash type\n");
		return -ENOTSUPP;
	}

	pr_info("Found %s serial flash (blocksize: 0x%X, blocks: %d)\n",
		e->name, e->blocksize, e->numblocks);

	pr_err("Serial flash support is not implemented yet!\n");

	return -ENOTSUPP;
}
