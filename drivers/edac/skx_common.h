/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common codes for both the skx_edac driver and Intel 10nm server EDAC driver.
 * Originally split out from the skx_edac driver.
 *
 * Copyright (c) 2018, Intel Corporation.
 */

#ifndef _SKX_COMM_EDAC_H
#define _SKX_COMM_EDAC_H

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

#define I10NM_NUM_IMC		4
#define I10NM_NUM_CHANNELS	2
#define I10NM_NUM_DIMMS		2

#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define NUM_IMC		MAX(SKX_NUM_IMC, I10NM_NUM_IMC)
#define NUM_CHANNELS	MAX(SKX_NUM_CHANNELS, I10NM_NUM_CHANNELS)
#define NUM_DIMMS	MAX(SKX_NUM_DIMMS, I10NM_NUM_DIMMS)

#define IS_DIMM_PRESENT(r)		GET_BITFIELD(r, 15, 15)
#define IS_NVDIMM_PRESENT(r, i)		GET_BITFIELD(r, i, i)

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
	u32 mcroute;
	struct skx_imc {
		struct mem_ctl_info *mci;
		struct pci_dev *mdev; /* for i10nm CPU */
		void __iomem *mbase;  /* for i10nm CPU */
		u8 mc;	/* system wide mc# */
		u8 lmc;	/* socket relative mc# */
		u8 src_id, node_id;
		struct skx_channel {
			struct pci_dev	*cdev;
			struct pci_dev	*edev;
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
	I10NM
};

enum {
	INDEX_SOCKET,
	INDEX_MEMCTRL,
	INDEX_CHANNEL,
	INDEX_DIMM,
	INDEX_MAX
};

struct decoded_addr {
	struct skx_dev *dev;
	u64	addr;
	int	socket;
	int	imc;
	int	channel;
	u64	chan_addr;
	int	sktways;
	int	chanways;
	int	dimm;
	int	rank;
	int	channel_rank;
	u64	rank_address;
	int	row;
	int	column;
	int	bank_address;
	int	bank_group;
};

struct res_config {
	enum type type;
	/* Configuration agent device ID */
	unsigned int decs_did;
	/* Default bus number configuration register offset */
	int busno_cfg_offset;
};

typedef int (*get_dimm_config_f)(struct mem_ctl_info *mci);
typedef bool (*skx_decode_f)(struct decoded_addr *res);
typedef void (*skx_show_retry_log_f)(struct decoded_addr *res, char *msg, int len);

int __init skx_adxl_get(void);
void __exit skx_adxl_put(void);
void skx_set_decode(skx_decode_f decode, skx_show_retry_log_f show_retry_log);

int skx_get_src_id(struct skx_dev *d, int off, u8 *id);
int skx_get_node_id(struct skx_dev *d, u8 *id);

int skx_get_all_bus_mappings(struct res_config *cfg, struct list_head **list);

int skx_get_hi_lo(unsigned int did, int off[], u64 *tolm, u64 *tohm);

int skx_get_dimm_info(u32 mtr, u32 mcmtr, u32 amap, struct dimm_info *dimm,
		      struct skx_imc *imc, int chan, int dimmno);

int skx_get_nvdimm_info(struct dimm_info *dimm, struct skx_imc *imc,
			int chan, int dimmno, const char *mod_str);

int skx_register_mci(struct skx_imc *imc, struct pci_dev *pdev,
		     const char *ctl_name, const char *mod_str,
		     get_dimm_config_f get_dimm_config);

int skx_mce_check_error(struct notifier_block *nb, unsigned long val,
			void *data);

void skx_remove(void);

#endif /* _SKX_COMM_EDAC_H */
