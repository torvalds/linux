/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common codes for both the skx_edac driver and Intel 10nm server EDAC driver.
 * Originally split out from the skx_edac driver.
 *
 * Copyright (c) 2018, Intel Corporation.
 */

#ifndef _SKX_COMM_EDAC_H
#define _SKX_COMM_EDAC_H

#include <linux/bits.h>
#include <asm/mce.h>

#define MSG_SIZE		1024

/*
 * Debug macros
 */
#define skx_printk(level, fmt, arg...)			\
	edac_printk(level, "skx", fmt, ##arg)

#define skx_mc_printk(mci, level, fmt, arg...)		\
	edac_mc_chipset_printk(mci, level, "skx", fmt, ##arg)

/*
 * Get a bit field at register value <v>, from bit <lo> to bit <hi>
 */
#define GET_BITFIELD(v, lo, hi) \
	(((v) & GENMASK_ULL((hi), (lo))) >> (lo))

#define SKX_NUM_IMC		2	/* Memory controllers per socket */
#define SKX_NUM_CHANNELS	3	/* Channels per memory controller */
#define SKX_NUM_DIMMS		2	/* Max DIMMS per channel */

#define I10NM_NUM_DDR_IMC	12
#define I10NM_NUM_DDR_CHANNELS	2
#define I10NM_NUM_DDR_DIMMS	2

#define I10NM_NUM_HBM_IMC	16
#define I10NM_NUM_HBM_CHANNELS	2
#define I10NM_NUM_HBM_DIMMS	1

#define I10NM_NUM_IMC		(I10NM_NUM_DDR_IMC + I10NM_NUM_HBM_IMC)
#define I10NM_NUM_CHANNELS	MAX(I10NM_NUM_DDR_CHANNELS, I10NM_NUM_HBM_CHANNELS)
#define I10NM_NUM_DIMMS		MAX(I10NM_NUM_DDR_DIMMS, I10NM_NUM_HBM_DIMMS)

#define NUM_IMC		MAX(SKX_NUM_IMC, I10NM_NUM_IMC)
#define NUM_CHANNELS	MAX(SKX_NUM_CHANNELS, I10NM_NUM_CHANNELS)
#define NUM_DIMMS	MAX(SKX_NUM_DIMMS, I10NM_NUM_DIMMS)

#define IS_DIMM_PRESENT(r)		GET_BITFIELD(r, 15, 15)
#define IS_NVDIMM_PRESENT(r, i)		GET_BITFIELD(r, i, i)

#define MCI_MISC_ECC_MODE(m)	(((m) >> 59) & 15)
#define MCI_MISC_ECC_DDRT	8	/* read from DDRT */

/*
 * According to Intel Architecture spec vol 3B,
 * Table 15-10 "IA32_MCi_Status [15:0] Compound Error Code Encoding"
 * memory errors should fit one of these masks:
 *	000f 0000 1mmm cccc (binary)
 *	000f 0010 1mmm cccc (binary)	[RAM used as cache]
 * where:
 *	f = Correction Report Filtering Bit. If 1, subsequent errors
 *	    won't be shown
 *	mmm = error type
 *	cccc = channel
 */
#define MCACOD_MEM_ERR_MASK	0xef80
/*
 * Errors from either the memory of the 1-level memory system or the
 * 2nd level memory (the slow "far" memory) of the 2-level memory system.
 */
#define MCACOD_MEM_CTL_ERR	0x80
/*
 * Errors from the 1st level memory (the fast "near" memory as cache)
 * of the 2-level memory system.
 */
#define MCACOD_EXT_MEM_ERR	0x280

/*
 * Each cpu socket contains some pci devices that provide global
 * information, and also some that are local to each of the two
 * memory controllers on the die.
 */
struct skx_dev {
	struct list_head list;
	u8 bus[4];
	int seg;
	struct pci_dev *sad_all;
	struct pci_dev *util_all;
	struct pci_dev *uracu; /* for i10nm CPU */
	struct pci_dev *pcu_cr3; /* for HBM memory detection */
	u32 mcroute;
	struct skx_imc {
		struct mem_ctl_info *mci;
		struct pci_dev *mdev; /* for i10nm CPU */
		void __iomem *mbase;  /* for i10nm CPU */
		int chan_mmio_sz;     /* for i10nm CPU */
		int num_channels; /* channels per memory controller */
		int num_dimms; /* dimms per channel */
		bool hbm_mc;
		u8 mc;	/* system wide mc# */
		u8 lmc;	/* socket relative mc# */
		u8 src_id, node_id;
		struct skx_channel {
			struct pci_dev	*cdev;
			struct pci_dev	*edev;
			u32 retry_rd_err_log_s;
			u32 retry_rd_err_log_d;
			u32 retry_rd_err_log_d2;
			struct skx_dimm {
				u8 close_pg;
				u8 bank_xor_enable;
				u8 fine_grain_bank;
				u8 rowbits;
				u8 colbits;
			} dimms[NUM_DIMMS];
		} chan[NUM_CHANNELS];
	} imc[NUM_IMC];
};

struct skx_pvt {
	struct skx_imc	*imc;
};

enum type {
	SKX,
	I10NM,
	SPR,
	GNR
};

enum {
	INDEX_SOCKET,
	INDEX_MEMCTRL,
	INDEX_CHANNEL,
	INDEX_DIMM,
	INDEX_CS,
	INDEX_NM_FIRST,
	INDEX_NM_MEMCTRL = INDEX_NM_FIRST,
	INDEX_NM_CHANNEL,
	INDEX_NM_DIMM,
	INDEX_NM_CS,
	INDEX_MAX
};

enum error_source {
	ERR_SRC_1LM,
	ERR_SRC_2LM_NM,
	ERR_SRC_2LM_FM,
	ERR_SRC_NOT_MEMORY,
};

#define BIT_NM_MEMCTRL	BIT_ULL(INDEX_NM_MEMCTRL)
#define BIT_NM_CHANNEL	BIT_ULL(INDEX_NM_CHANNEL)
#define BIT_NM_DIMM	BIT_ULL(INDEX_NM_DIMM)
#define BIT_NM_CS	BIT_ULL(INDEX_NM_CS)

struct decoded_addr {
	struct mce *mce;
	struct skx_dev *dev;
	u64	addr;
	int	socket;
	int	imc;
	int	channel;
	u64	chan_addr;
	int	sktways;
	int	chanways;
	int	dimm;
	int	cs;
	int	rank;
	int	channel_rank;
	u64	rank_address;
	int	row;
	int	column;
	int	bank_address;
	int	bank_group;
	bool	decoded_by_adxl;
};

struct pci_bdf {
	u32 bus : 8;
	u32 dev : 5;
	u32 fun : 3;
};

struct res_config {
	enum type type;
	/* Configuration agent device ID */
	unsigned int decs_did;
	/* Default bus number configuration register offset */
	int busno_cfg_offset;
	/* DDR memory controllers per socket */
	int ddr_imc_num;
	/* DDR channels per DDR memory controller */
	int ddr_chan_num;
	/* DDR DIMMs per DDR memory channel */
	int ddr_dimm_num;
	/* Per DDR channel memory-mapped I/O size */
	int ddr_chan_mmio_sz;
	/* HBM memory controllers per socket */
	int hbm_imc_num;
	/* HBM channels per HBM memory controller */
	int hbm_chan_num;
	/* HBM DIMMs per HBM memory channel */
	int hbm_dimm_num;
	/* Per HBM channel memory-mapped I/O size */
	int hbm_chan_mmio_sz;
	bool support_ddr5;
	/* SAD device BDF */
	struct pci_bdf sad_all_bdf;
	/* PCU device BDF */
	struct pci_bdf pcu_cr3_bdf;
	/* UTIL device BDF */
	struct pci_bdf util_all_bdf;
	/* URACU device BDF */
	struct pci_bdf uracu_bdf;
	/* DDR mdev device BDF */
	struct pci_bdf ddr_mdev_bdf;
	/* HBM mdev device BDF */
	struct pci_bdf hbm_mdev_bdf;
	int sad_all_offset;
	/* Offsets of retry_rd_err_log registers */
	u32 *offsets_scrub;
	u32 *offsets_scrub_hbm0;
	u32 *offsets_scrub_hbm1;
	u32 *offsets_demand;
	u32 *offsets_demand2;
	u32 *offsets_demand_hbm0;
	u32 *offsets_demand_hbm1;
};

typedef int (*get_dimm_config_f)(struct mem_ctl_info *mci,
				 struct res_config *cfg);
typedef bool (*skx_decode_f)(struct decoded_addr *res);
typedef void (*skx_show_retry_log_f)(struct decoded_addr *res, char *msg, int len, bool scrub_err);

int skx_adxl_get(void);
void skx_adxl_put(void);
void skx_set_decode(skx_decode_f decode, skx_show_retry_log_f show_retry_log);
void skx_set_mem_cfg(bool mem_cfg_2lm);
void skx_set_res_cfg(struct res_config *cfg);

int skx_get_src_id(struct skx_dev *d, int off, u8 *id);
int skx_get_node_id(struct skx_dev *d, u8 *id);

int skx_get_all_bus_mappings(struct res_config *cfg, struct list_head **list);

int skx_get_hi_lo(unsigned int did, int off[], u64 *tolm, u64 *tohm);

int skx_get_dimm_info(u32 mtr, u32 mcmtr, u32 amap, struct dimm_info *dimm,
		      struct skx_imc *imc, int chan, int dimmno,
		      struct res_config *cfg);

int skx_get_nvdimm_info(struct dimm_info *dimm, struct skx_imc *imc,
			int chan, int dimmno, const char *mod_str);

int skx_register_mci(struct skx_imc *imc, struct pci_dev *pdev,
		     const char *ctl_name, const char *mod_str,
		     get_dimm_config_f get_dimm_config,
		     struct res_config *cfg);

int skx_mce_check_error(struct notifier_block *nb, unsigned long val,
			void *data);

void skx_remove(void);

#ifdef CONFIG_EDAC_DEBUG
void skx_setup_debug(const char *name);
void skx_teardown_debug(void);
#else
static inline void skx_setup_debug(const char *name) {}
static inline void skx_teardown_debug(void) {}
#endif

#endif /* _SKX_COMM_EDAC_H */
