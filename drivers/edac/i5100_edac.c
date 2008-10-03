/*
 * Intel 5100 Memory Controllers kernel module
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * This module is based on the following document:
 *
 * Intel 5100X Chipset Memory Controller Hub (MCH) - Datasheet
 *      http://download.intel.com/design/chipsets/datashts/318378.pdf
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/edac.h>
#include <linux/delay.h>
#include <linux/mmzone.h>

#include "edac_core.h"

/* register addresses */

/* device 16, func 1 */
#define I5100_MC		0x40	/* Memory Control Register */
#define I5100_MS		0x44	/* Memory Status Register */
#define I5100_SPDDATA		0x48	/* Serial Presence Detect Status Reg */
#define I5100_SPDCMD		0x4c	/* Serial Presence Detect Command Reg */
#define I5100_TOLM		0x6c	/* Top of Low Memory */
#define I5100_MIR0		0x80	/* Memory Interleave Range 0 */
#define I5100_MIR1		0x84	/* Memory Interleave Range 1 */
#define I5100_AMIR_0		0x8c	/* Adjusted Memory Interleave Range 0 */
#define I5100_AMIR_1		0x90	/* Adjusted Memory Interleave Range 1 */
#define I5100_FERR_NF_MEM	0xa0	/* MC First Non Fatal Errors */
#define		I5100_FERR_NF_MEM_M16ERR_MASK	(1 << 16)
#define		I5100_FERR_NF_MEM_M15ERR_MASK	(1 << 15)
#define		I5100_FERR_NF_MEM_M14ERR_MASK	(1 << 14)
#define		I5100_FERR_NF_MEM_M12ERR_MASK	(1 << 12)
#define		I5100_FERR_NF_MEM_M11ERR_MASK	(1 << 11)
#define		I5100_FERR_NF_MEM_M10ERR_MASK	(1 << 10)
#define		I5100_FERR_NF_MEM_M6ERR_MASK	(1 << 6)
#define		I5100_FERR_NF_MEM_M5ERR_MASK	(1 << 5)
#define		I5100_FERR_NF_MEM_M4ERR_MASK	(1 << 4)
#define		I5100_FERR_NF_MEM_M1ERR_MASK	1
#define		I5100_FERR_NF_MEM_ANY_MASK	\
			(I5100_FERR_NF_MEM_M16ERR_MASK | \
			I5100_FERR_NF_MEM_M15ERR_MASK | \
			I5100_FERR_NF_MEM_M14ERR_MASK | \
			I5100_FERR_NF_MEM_M12ERR_MASK | \
			I5100_FERR_NF_MEM_M11ERR_MASK | \
			I5100_FERR_NF_MEM_M10ERR_MASK | \
			I5100_FERR_NF_MEM_M6ERR_MASK | \
			I5100_FERR_NF_MEM_M5ERR_MASK | \
			I5100_FERR_NF_MEM_M4ERR_MASK | \
			I5100_FERR_NF_MEM_M1ERR_MASK)
#define	I5100_NERR_NF_MEM	0xa4	/* MC Next Non-Fatal Errors */
#define I5100_EMASK_MEM		0xa8	/* MC Error Mask Register */

/* device 21 and 22, func 0 */
#define I5100_MTR_0	0x154	/* Memory Technology Registers 0-3 */
#define I5100_DMIR	0x15c	/* DIMM Interleave Range */
#define	I5100_VALIDLOG	0x18c	/* Valid Log Markers */
#define	I5100_NRECMEMA	0x190	/* Non-Recoverable Memory Error Log Reg A */
#define	I5100_NRECMEMB	0x194	/* Non-Recoverable Memory Error Log Reg B */
#define	I5100_REDMEMA	0x198	/* Recoverable Memory Data Error Log Reg A */
#define	I5100_REDMEMB	0x19c	/* Recoverable Memory Data Error Log Reg B */
#define	I5100_RECMEMA	0x1a0	/* Recoverable Memory Error Log Reg A */
#define	I5100_RECMEMB	0x1a4	/* Recoverable Memory Error Log Reg B */
#define I5100_MTR_4	0x1b0	/* Memory Technology Registers 4,5 */

/* bit field accessors */

static inline u32 i5100_mc_errdeten(u32 mc)
{
	return mc >> 5 & 1;
}

static inline u16 i5100_spddata_rdo(u16 a)
{
	return a >> 15 & 1;
}

static inline u16 i5100_spddata_sbe(u16 a)
{
	return a >> 13 & 1;
}

static inline u16 i5100_spddata_busy(u16 a)
{
	return a >> 12 & 1;
}

static inline u16 i5100_spddata_data(u16 a)
{
	return a & ((1 << 8) - 1);
}

static inline u32 i5100_spdcmd_create(u32 dti, u32 ckovrd, u32 sa, u32 ba,
				      u32 data, u32 cmd)
{
	return	((dti & ((1 << 4) - 1))  << 28) |
		((ckovrd & 1)            << 27) |
		((sa & ((1 << 3) - 1))   << 24) |
		((ba & ((1 << 8) - 1))   << 16) |
		((data & ((1 << 8) - 1)) <<  8) |
		(cmd & 1);
}

static inline u16 i5100_tolm_tolm(u16 a)
{
	return a >> 12 & ((1 << 4) - 1);
}

static inline u16 i5100_mir_limit(u16 a)
{
	return a >> 4 & ((1 << 12) - 1);
}

static inline u16 i5100_mir_way1(u16 a)
{
	return a >> 1 & 1;
}

static inline u16 i5100_mir_way0(u16 a)
{
	return a & 1;
}

static inline u32 i5100_ferr_nf_mem_chan_indx(u32 a)
{
	return a >> 28 & 1;
}

static inline u32 i5100_ferr_nf_mem_any(u32 a)
{
	return a & I5100_FERR_NF_MEM_ANY_MASK;
}

static inline u32 i5100_nerr_nf_mem_any(u32 a)
{
	return i5100_ferr_nf_mem_any(a);
}

static inline u32 i5100_dmir_limit(u32 a)
{
	return a >> 16 & ((1 << 11) - 1);
}

static inline u32 i5100_dmir_rank(u32 a, u32 i)
{
	return a >> (4 * i) & ((1 << 2) - 1);
}

static inline u16 i5100_mtr_present(u16 a)
{
	return a >> 10 & 1;
}

static inline u16 i5100_mtr_ethrottle(u16 a)
{
	return a >> 9 & 1;
}

static inline u16 i5100_mtr_width(u16 a)
{
	return a >> 8 & 1;
}

static inline u16 i5100_mtr_numbank(u16 a)
{
	return a >> 6 & 1;
}

static inline u16 i5100_mtr_numrow(u16 a)
{
	return a >> 2 & ((1 << 2) - 1);
}

static inline u16 i5100_mtr_numcol(u16 a)
{
	return a & ((1 << 2) - 1);
}


static inline u32 i5100_validlog_redmemvalid(u32 a)
{
	return a >> 2 & 1;
}

static inline u32 i5100_validlog_recmemvalid(u32 a)
{
	return a >> 1 & 1;
}

static inline u32 i5100_validlog_nrecmemvalid(u32 a)
{
	return a & 1;
}

static inline u32 i5100_nrecmema_merr(u32 a)
{
	return a >> 15 & ((1 << 5) - 1);
}

static inline u32 i5100_nrecmema_bank(u32 a)
{
	return a >> 12 & ((1 << 3) - 1);
}

static inline u32 i5100_nrecmema_rank(u32 a)
{
	return a >>  8 & ((1 << 3) - 1);
}

static inline u32 i5100_nrecmema_dm_buf_id(u32 a)
{
	return a & ((1 << 8) - 1);
}

static inline u32 i5100_nrecmemb_cas(u32 a)
{
	return a >> 16 & ((1 << 13) - 1);
}

static inline u32 i5100_nrecmemb_ras(u32 a)
{
	return a & ((1 << 16) - 1);
}

static inline u32 i5100_redmemb_ecc_locator(u32 a)
{
	return a & ((1 << 18) - 1);
}

static inline u32 i5100_recmema_merr(u32 a)
{
	return i5100_nrecmema_merr(a);
}

static inline u32 i5100_recmema_bank(u32 a)
{
	return i5100_nrecmema_bank(a);
}

static inline u32 i5100_recmema_rank(u32 a)
{
	return i5100_nrecmema_rank(a);
}

static inline u32 i5100_recmema_dm_buf_id(u32 a)
{
	return i5100_nrecmema_dm_buf_id(a);
}

static inline u32 i5100_recmemb_cas(u32 a)
{
	return i5100_nrecmemb_cas(a);
}

static inline u32 i5100_recmemb_ras(u32 a)
{
	return i5100_nrecmemb_ras(a);
}

/* some generic limits */
#define I5100_MAX_RANKS_PER_CTLR	6
#define I5100_MAX_CTLRS			2
#define I5100_MAX_RANKS_PER_DIMM	4
#define I5100_DIMM_ADDR_LINES		(6 - 3)	/* 64 bits / 8 bits per byte */
#define I5100_MAX_DIMM_SLOTS_PER_CTLR	4
#define I5100_MAX_RANK_INTERLEAVE	4
#define I5100_MAX_DMIRS			5

struct i5100_priv {
	/* ranks on each dimm -- 0 maps to not present -- obtained via SPD */
	int dimm_numrank[I5100_MAX_CTLRS][I5100_MAX_DIMM_SLOTS_PER_CTLR];

	/*
	 * mainboard chip select map -- maps i5100 chip selects to
	 * DIMM slot chip selects.  In the case of only 4 ranks per
	 * controller, the mapping is fairly obvious but not unique.
	 * we map -1 -> NC and assume both controllers use the same
	 * map...
	 *
	 */
	int dimm_csmap[I5100_MAX_DIMM_SLOTS_PER_CTLR][I5100_MAX_RANKS_PER_DIMM];

	/* memory interleave range */
	struct {
		u64	 limit;
		unsigned way[2];
	} mir[I5100_MAX_CTLRS];

	/* adjusted memory interleave range register */
	unsigned amir[I5100_MAX_CTLRS];

	/* dimm interleave range */
	struct {
		unsigned rank[I5100_MAX_RANK_INTERLEAVE];
		u64	 limit;
	} dmir[I5100_MAX_CTLRS][I5100_MAX_DMIRS];

	/* memory technology registers... */
	struct {
		unsigned present;	/* 0 or 1 */
		unsigned ethrottle;	/* 0 or 1 */
		unsigned width;		/* 4 or 8 bits  */
		unsigned numbank;	/* 2 or 3 lines */
		unsigned numrow;	/* 13 .. 16 lines */
		unsigned numcol;	/* 11 .. 12 lines */
	} mtr[I5100_MAX_CTLRS][I5100_MAX_RANKS_PER_CTLR];

	u64 tolm;		/* top of low memory in bytes */
	unsigned ranksperctlr;	/* number of ranks per controller */

	struct pci_dev *mc;	/* device 16 func 1 */
	struct pci_dev *ch0mm;	/* device 21 func 0 */
	struct pci_dev *ch1mm;	/* device 22 func 0 */
};

/* map a rank/ctlr to a slot number on the mainboard */
static int i5100_rank_to_slot(const struct mem_ctl_info *mci,
			      int ctlr, int rank)
{
	const struct i5100_priv *priv = mci->pvt_info;
	int i;

	for (i = 0; i < I5100_MAX_DIMM_SLOTS_PER_CTLR; i++) {
		int j;
		const int numrank = priv->dimm_numrank[ctlr][i];

		for (j = 0; j < numrank; j++)
			if (priv->dimm_csmap[i][j] == rank)
				return i * 2 + ctlr;
	}

	return -1;
}

static const char *i5100_err_msg(unsigned err)
{
	static const char *merrs[] = {
		"unknown", /* 0 */
		"uncorrectable data ECC on replay", /* 1 */
		"unknown", /* 2 */
		"unknown", /* 3 */
		"aliased uncorrectable demand data ECC", /* 4 */
		"aliased uncorrectable spare-copy data ECC", /* 5 */
		"aliased uncorrectable patrol data ECC", /* 6 */
		"unknown", /* 7 */
		"unknown", /* 8 */
		"unknown", /* 9 */
		"non-aliased uncorrectable demand data ECC", /* 10 */
		"non-aliased uncorrectable spare-copy data ECC", /* 11 */
		"non-aliased uncorrectable patrol data ECC", /* 12 */
		"unknown", /* 13 */
		"correctable demand data ECC", /* 14 */
		"correctable spare-copy data ECC", /* 15 */
		"correctable patrol data ECC", /* 16 */
		"unknown", /* 17 */
		"SPD protocol error", /* 18 */
		"unknown", /* 19 */
		"spare copy initiated", /* 20 */
		"spare copy completed", /* 21 */
	};
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(merrs); i++)
		if (1 << i & err)
			return merrs[i];

	return "none";
}

/* convert csrow index into a rank (per controller -- 0..5) */
static int i5100_csrow_to_rank(const struct mem_ctl_info *mci, int csrow)
{
	const struct i5100_priv *priv = mci->pvt_info;

	return csrow % priv->ranksperctlr;
}

/* convert csrow index into a controller (0..1) */
static int i5100_csrow_to_cntlr(const struct mem_ctl_info *mci, int csrow)
{
	const struct i5100_priv *priv = mci->pvt_info;

	return csrow / priv->ranksperctlr;
}

static unsigned i5100_rank_to_csrow(const struct mem_ctl_info *mci,
				    int ctlr, int rank)
{
	const struct i5100_priv *priv = mci->pvt_info;

	return ctlr * priv->ranksperctlr + rank;
}

static void i5100_handle_ce(struct mem_ctl_info *mci,
			    int ctlr,
			    unsigned bank,
			    unsigned rank,
			    unsigned long syndrome,
			    unsigned cas,
			    unsigned ras,
			    const char *msg)
{
	const int csrow = i5100_rank_to_csrow(mci, ctlr, rank);

	printk(KERN_ERR
		"CE ctlr %d, bank %u, rank %u, syndrome 0x%lx, "
		"cas %u, ras %u, csrow %u, label \"%s\": %s\n",
		ctlr, bank, rank, syndrome, cas, ras,
		csrow, mci->csrows[csrow].channels[0].label, msg);

	mci->ce_count++;
	mci->csrows[csrow].ce_count++;
	mci->csrows[csrow].channels[0].ce_count++;
}

static void i5100_handle_ue(struct mem_ctl_info *mci,
			    int ctlr,
			    unsigned bank,
			    unsigned rank,
			    unsigned long syndrome,
			    unsigned cas,
			    unsigned ras,
			    const char *msg)
{
	const int csrow = i5100_rank_to_csrow(mci, ctlr, rank);

	printk(KERN_ERR
		"UE ctlr %d, bank %u, rank %u, syndrome 0x%lx, "
		"cas %u, ras %u, csrow %u, label \"%s\": %s\n",
		ctlr, bank, rank, syndrome, cas, ras,
		csrow, mci->csrows[csrow].channels[0].label, msg);

	mci->ue_count++;
	mci->csrows[csrow].ue_count++;
}

static void i5100_read_log(struct mem_ctl_info *mci, int ctlr,
			   u32 ferr, u32 nerr)
{
	struct i5100_priv *priv = mci->pvt_info;
	struct pci_dev *pdev = (ctlr) ? priv->ch1mm : priv->ch0mm;
	u32 dw;
	u32 dw2;
	unsigned syndrome = 0;
	unsigned ecc_loc = 0;
	unsigned merr;
	unsigned bank;
	unsigned rank;
	unsigned cas;
	unsigned ras;

	pci_read_config_dword(pdev, I5100_VALIDLOG, &dw);

	if (i5100_validlog_redmemvalid(dw)) {
		pci_read_config_dword(pdev, I5100_REDMEMA, &dw2);
		syndrome = dw2;
		pci_read_config_dword(pdev, I5100_REDMEMB, &dw2);
		ecc_loc = i5100_redmemb_ecc_locator(dw2);
	}

	if (i5100_validlog_recmemvalid(dw)) {
		const char *msg;

		pci_read_config_dword(pdev, I5100_RECMEMA, &dw2);
		merr = i5100_recmema_merr(dw2);
		bank = i5100_recmema_bank(dw2);
		rank = i5100_recmema_rank(dw2);

		pci_read_config_dword(pdev, I5100_RECMEMB, &dw2);
		cas = i5100_recmemb_cas(dw2);
		ras = i5100_recmemb_ras(dw2);

		/* FIXME:  not really sure if this is what merr is...
		 */
		if (!merr)
			msg = i5100_err_msg(ferr);
		else
			msg = i5100_err_msg(nerr);

		i5100_handle_ce(mci, ctlr, bank, rank, syndrome, cas, ras, msg);
	}

	if (i5100_validlog_nrecmemvalid(dw)) {
		const char *msg;

		pci_read_config_dword(pdev, I5100_NRECMEMA, &dw2);
		merr = i5100_nrecmema_merr(dw2);
		bank = i5100_nrecmema_bank(dw2);
		rank = i5100_nrecmema_rank(dw2);

		pci_read_config_dword(pdev, I5100_NRECMEMB, &dw2);
		cas = i5100_nrecmemb_cas(dw2);
		ras = i5100_nrecmemb_ras(dw2);

		/* FIXME:  not really sure if this is what merr is...
		 */
		if (!merr)
			msg = i5100_err_msg(ferr);
		else
			msg = i5100_err_msg(nerr);

		i5100_handle_ue(mci, ctlr, bank, rank, syndrome, cas, ras, msg);
	}

	pci_write_config_dword(pdev, I5100_VALIDLOG, dw);
}

static void i5100_check_error(struct mem_ctl_info *mci)
{
	struct i5100_priv *priv = mci->pvt_info;
	u32 dw;


	pci_read_config_dword(priv->mc, I5100_FERR_NF_MEM, &dw);
	if (i5100_ferr_nf_mem_any(dw)) {
		u32 dw2;

		pci_read_config_dword(priv->mc, I5100_NERR_NF_MEM, &dw2);
		if (dw2)
			pci_write_config_dword(priv->mc, I5100_NERR_NF_MEM,
					       dw2);
		pci_write_config_dword(priv->mc, I5100_FERR_NF_MEM, dw);

		i5100_read_log(mci, i5100_ferr_nf_mem_chan_indx(dw),
			       i5100_ferr_nf_mem_any(dw),
			       i5100_nerr_nf_mem_any(dw2));
	}
}

static struct pci_dev *pci_get_device_func(unsigned vendor,
					   unsigned device,
					   unsigned func)
{
	struct pci_dev *ret = NULL;

	while (1) {
		ret = pci_get_device(vendor, device, ret);

		if (!ret)
			break;

		if (PCI_FUNC(ret->devfn) == func)
			break;
	}

	return ret;
}

static unsigned long __devinit i5100_npages(struct mem_ctl_info *mci,
					    int csrow)
{
	struct i5100_priv *priv = mci->pvt_info;
	const unsigned ctlr_rank = i5100_csrow_to_rank(mci, csrow);
	const unsigned ctlr = i5100_csrow_to_cntlr(mci, csrow);
	unsigned addr_lines;

	/* dimm present? */
	if (!priv->mtr[ctlr][ctlr_rank].present)
		return 0ULL;

	addr_lines =
		I5100_DIMM_ADDR_LINES +
		priv->mtr[ctlr][ctlr_rank].numcol +
		priv->mtr[ctlr][ctlr_rank].numrow +
		priv->mtr[ctlr][ctlr_rank].numbank;

	return (unsigned long)
		((unsigned long long) (1ULL << addr_lines) / PAGE_SIZE);
}

static void __devinit i5100_init_mtr(struct mem_ctl_info *mci)
{
	struct i5100_priv *priv = mci->pvt_info;
	struct pci_dev *mms[2] = { priv->ch0mm, priv->ch1mm };
	int i;

	for (i = 0; i < I5100_MAX_CTLRS; i++) {
		int j;
		struct pci_dev *pdev = mms[i];

		for (j = 0; j < I5100_MAX_RANKS_PER_CTLR; j++) {
			const unsigned addr =
				(j < 4) ? I5100_MTR_0 + j * 2 :
					  I5100_MTR_4 + (j - 4) * 2;
			u16 w;

			pci_read_config_word(pdev, addr, &w);

			priv->mtr[i][j].present = i5100_mtr_present(w);
			priv->mtr[i][j].ethrottle = i5100_mtr_ethrottle(w);
			priv->mtr[i][j].width = 4 + 4 * i5100_mtr_width(w);
			priv->mtr[i][j].numbank = 2 + i5100_mtr_numbank(w);
			priv->mtr[i][j].numrow = 13 + i5100_mtr_numrow(w);
			priv->mtr[i][j].numcol = 10 + i5100_mtr_numcol(w);
		}
	}
}

/*
 * FIXME: make this into a real i2c adapter (so that dimm-decode
 * will work)?
 */
static int i5100_read_spd_byte(const struct mem_ctl_info *mci,
			       u8 ch, u8 slot, u8 addr, u8 *byte)
{
	struct i5100_priv *priv = mci->pvt_info;
	u16 w;
	unsigned long et;

	pci_read_config_word(priv->mc, I5100_SPDDATA, &w);
	if (i5100_spddata_busy(w))
		return -1;

	pci_write_config_dword(priv->mc, I5100_SPDCMD,
			       i5100_spdcmd_create(0xa, 1, ch * 4 + slot, addr,
						   0, 0));

	/* wait up to 100ms */
	et = jiffies + HZ / 10;
	udelay(100);
	while (1) {
		pci_read_config_word(priv->mc, I5100_SPDDATA, &w);
		if (!i5100_spddata_busy(w))
			break;
		udelay(100);
	}

	if (!i5100_spddata_rdo(w) || i5100_spddata_sbe(w))
		return -1;

	*byte = i5100_spddata_data(w);

	return 0;
}

/*
 * fill dimm chip select map
 *
 * FIXME:
 *   o only valid for 4 ranks per controller
 *   o not the only way to may chip selects to dimm slots
 *   o investigate if there is some way to obtain this map from the bios
 */
static void __devinit i5100_init_dimm_csmap(struct mem_ctl_info *mci)
{
	struct i5100_priv *priv = mci->pvt_info;
	int i;

	WARN_ON(priv->ranksperctlr != 4);

	for (i = 0; i < I5100_MAX_DIMM_SLOTS_PER_CTLR; i++) {
		int j;

		for (j = 0; j < I5100_MAX_RANKS_PER_DIMM; j++)
			priv->dimm_csmap[i][j] = -1; /* default NC */
	}

	/* only 2 chip selects per slot... */
	priv->dimm_csmap[0][0] = 0;
	priv->dimm_csmap[0][1] = 3;
	priv->dimm_csmap[1][0] = 1;
	priv->dimm_csmap[1][1] = 2;
	priv->dimm_csmap[2][0] = 2;
	priv->dimm_csmap[3][0] = 3;
}

static void __devinit i5100_init_dimm_layout(struct pci_dev *pdev,
					     struct mem_ctl_info *mci)
{
	struct i5100_priv *priv = mci->pvt_info;
	int i;

	for (i = 0; i < I5100_MAX_CTLRS; i++) {
		int j;

		for (j = 0; j < I5100_MAX_DIMM_SLOTS_PER_CTLR; j++) {
			u8 rank;

			if (i5100_read_spd_byte(mci, i, j, 5, &rank) < 0)
				priv->dimm_numrank[i][j] = 0;
			else
				priv->dimm_numrank[i][j] = (rank & 3) + 1;
		}
	}

	i5100_init_dimm_csmap(mci);
}

static void __devinit i5100_init_interleaving(struct pci_dev *pdev,
					      struct mem_ctl_info *mci)
{
	u16 w;
	u32 dw;
	struct i5100_priv *priv = mci->pvt_info;
	struct pci_dev *mms[2] = { priv->ch0mm, priv->ch1mm };
	int i;

	pci_read_config_word(pdev, I5100_TOLM, &w);
	priv->tolm = (u64) i5100_tolm_tolm(w) * 256 * 1024 * 1024;

	pci_read_config_word(pdev, I5100_MIR0, &w);
	priv->mir[0].limit = (u64) i5100_mir_limit(w) << 28;
	priv->mir[0].way[1] = i5100_mir_way1(w);
	priv->mir[0].way[0] = i5100_mir_way0(w);

	pci_read_config_word(pdev, I5100_MIR1, &w);
	priv->mir[1].limit = (u64) i5100_mir_limit(w) << 28;
	priv->mir[1].way[1] = i5100_mir_way1(w);
	priv->mir[1].way[0] = i5100_mir_way0(w);

	pci_read_config_word(pdev, I5100_AMIR_0, &w);
	priv->amir[0] = w;
	pci_read_config_word(pdev, I5100_AMIR_1, &w);
	priv->amir[1] = w;

	for (i = 0; i < I5100_MAX_CTLRS; i++) {
		int j;

		for (j = 0; j < 5; j++) {
			int k;

			pci_read_config_dword(mms[i], I5100_DMIR + j * 4, &dw);

			priv->dmir[i][j].limit =
				(u64) i5100_dmir_limit(dw) << 28;
			for (k = 0; k < I5100_MAX_RANKS_PER_DIMM; k++)
				priv->dmir[i][j].rank[k] =
					i5100_dmir_rank(dw, k);
		}
	}

	i5100_init_mtr(mci);
}

static void __devinit i5100_init_csrows(struct mem_ctl_info *mci)
{
	int i;
	unsigned long total_pages = 0UL;
	struct i5100_priv *priv = mci->pvt_info;

	for (i = 0; i < mci->nr_csrows; i++) {
		const unsigned long npages = i5100_npages(mci, i);
		const unsigned cntlr = i5100_csrow_to_cntlr(mci, i);
		const unsigned rank = i5100_csrow_to_rank(mci, i);

		if (!npages)
			continue;

		/*
		 * FIXME: these two are totally bogus -- I don't see how to
		 * map them correctly to this structure...
		 */
		mci->csrows[i].first_page = total_pages;
		mci->csrows[i].last_page = total_pages + npages - 1;
		mci->csrows[i].page_mask = 0UL;

		mci->csrows[i].nr_pages = npages;
		mci->csrows[i].grain = 32;
		mci->csrows[i].csrow_idx = i;
		mci->csrows[i].dtype =
			(priv->mtr[cntlr][rank].width == 4) ? DEV_X4 : DEV_X8;
		mci->csrows[i].ue_count = 0;
		mci->csrows[i].ce_count = 0;
		mci->csrows[i].mtype = MEM_RDDR2;
		mci->csrows[i].edac_mode = EDAC_SECDED;
		mci->csrows[i].mci = mci;
		mci->csrows[i].nr_channels = 1;
		mci->csrows[i].channels[0].chan_idx = 0;
		mci->csrows[i].channels[0].ce_count = 0;
		mci->csrows[i].channels[0].csrow = mci->csrows + i;
		snprintf(mci->csrows[i].channels[0].label,
			 sizeof(mci->csrows[i].channels[0].label),
			 "DIMM%u", i5100_rank_to_slot(mci, cntlr, rank));

		total_pages += npages;
	}
}

static int __devinit i5100_init_one(struct pci_dev *pdev,
				    const struct pci_device_id *id)
{
	int rc;
	struct mem_ctl_info *mci;
	struct i5100_priv *priv;
	struct pci_dev *ch0mm, *ch1mm;
	int ret = 0;
	u32 dw;
	int ranksperch;

	if (PCI_FUNC(pdev->devfn) != 1)
		return -ENODEV;

	rc = pci_enable_device(pdev);
	if (rc < 0) {
		ret = rc;
		goto bail;
	}

	/* ECC enabled? */
	pci_read_config_dword(pdev, I5100_MC, &dw);
	if (!i5100_mc_errdeten(dw)) {
		printk(KERN_INFO "i5100_edac: ECC not enabled.\n");
		ret = -ENODEV;
		goto bail_pdev;
	}

	/* figure out how many ranks, from strapped state of 48GB_Mode input */
	pci_read_config_dword(pdev, I5100_MS, &dw);
	ranksperch = !!(dw & (1 << 8)) * 2 + 4;

	if (ranksperch != 4) {
		/* FIXME: get 6 ranks / controller to work - need hw... */
		printk(KERN_INFO "i5100_edac: unsupported configuration.\n");
		ret = -ENODEV;
		goto bail_pdev;
	}

	/* enable error reporting... */
	pci_read_config_dword(pdev, I5100_EMASK_MEM, &dw);
	dw &= ~I5100_FERR_NF_MEM_ANY_MASK;
	pci_write_config_dword(pdev, I5100_EMASK_MEM, dw);

	/* device 21, func 0, Channel 0 Memory Map, Error Flag/Mask, etc... */
	ch0mm = pci_get_device_func(PCI_VENDOR_ID_INTEL,
				    PCI_DEVICE_ID_INTEL_5100_21, 0);
	if (!ch0mm) {
		ret = -ENODEV;
		goto bail_pdev;
	}

	rc = pci_enable_device(ch0mm);
	if (rc < 0) {
		ret = rc;
		goto bail_ch0;
	}

	/* device 22, func 0, Channel 1 Memory Map, Error Flag/Mask, etc... */
	ch1mm = pci_get_device_func(PCI_VENDOR_ID_INTEL,
				    PCI_DEVICE_ID_INTEL_5100_22, 0);
	if (!ch1mm) {
		ret = -ENODEV;
		goto bail_disable_ch0;
	}

	rc = pci_enable_device(ch1mm);
	if (rc < 0) {
		ret = rc;
		goto bail_ch1;
	}

	mci = edac_mc_alloc(sizeof(*priv), ranksperch * 2, 1, 0);
	if (!mci) {
		ret = -ENOMEM;
		goto bail_disable_ch1;
	}

	mci->dev = &pdev->dev;

	priv = mci->pvt_info;
	priv->ranksperctlr = ranksperch;
	priv->mc = pdev;
	priv->ch0mm = ch0mm;
	priv->ch1mm = ch1mm;

	i5100_init_dimm_layout(pdev, mci);
	i5100_init_interleaving(pdev, mci);

	mci->mtype_cap = MEM_FLAG_FB_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = "i5100_edac.c";
	mci->mod_ver = "not versioned";
	mci->ctl_name = "i5100";
	mci->dev_name = pci_name(pdev);
	mci->ctl_page_to_phys = NULL;

	mci->edac_check = i5100_check_error;

	i5100_init_csrows(mci);

	/* this strange construction seems to be in every driver, dunno why */
	switch (edac_op_state) {
	case EDAC_OPSTATE_POLL:
	case EDAC_OPSTATE_NMI:
		break;
	default:
		edac_op_state = EDAC_OPSTATE_POLL;
		break;
	}

	if (edac_mc_add_mc(mci)) {
		ret = -ENODEV;
		goto bail_mc;
	}

	return ret;

bail_mc:
	edac_mc_free(mci);

bail_disable_ch1:
	pci_disable_device(ch1mm);

bail_ch1:
	pci_dev_put(ch1mm);

bail_disable_ch0:
	pci_disable_device(ch0mm);

bail_ch0:
	pci_dev_put(ch0mm);

bail_pdev:
	pci_disable_device(pdev);

bail:
	return ret;
}

static void __devexit i5100_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;
	struct i5100_priv *priv;

	mci = edac_mc_del_mc(&pdev->dev);

	if (!mci)
		return;

	priv = mci->pvt_info;
	pci_disable_device(pdev);
	pci_disable_device(priv->ch0mm);
	pci_disable_device(priv->ch1mm);
	pci_dev_put(priv->ch0mm);
	pci_dev_put(priv->ch1mm);

	edac_mc_free(mci);
}

static const struct pci_device_id i5100_pci_tbl[] __devinitdata = {
	/* Device 16, Function 0, Channel 0 Memory Map, Error Flag/Mask, ... */
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_5100_16) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, i5100_pci_tbl);

static struct pci_driver i5100_driver = {
	.name = KBUILD_BASENAME,
	.probe = i5100_init_one,
	.remove = __devexit_p(i5100_remove_one),
	.id_table = i5100_pci_tbl,
};

static int __init i5100_init(void)
{
	int pci_rc;

	pci_rc = pci_register_driver(&i5100_driver);

	return (pci_rc < 0) ? pci_rc : 0;
}

static void __exit i5100_exit(void)
{
	pci_unregister_driver(&i5100_driver);
}

module_init(i5100_init);
module_exit(i5100_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR
    ("Arthur Jones <ajones@riverbed.com>");
MODULE_DESCRIPTION("MC Driver for Intel I5100 memory controllers");
