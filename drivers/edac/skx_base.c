// SPDX-License-Identifier: GPL-2.0
/*
 * EDAC driver for Intel(R) Xeon(R) Skylake processors
 * Copyright (c) 2016, Intel Corporation.
 */

#include <linux/kernel.h>
#include <linux/processor.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/mce.h>

#include "edac_module.h"
#include "skx_common.h"

#define EDAC_MOD_STR    "skx_edac"

/*
 * Debug macros
 */
#define skx_printk(level, fmt, arg...)			\
	edac_printk(level, "skx", fmt, ##arg)

#define skx_mc_printk(mci, level, fmt, arg...)		\
	edac_mc_chipset_printk(mci, level, "skx", fmt, ##arg)

static struct list_head *skx_edac_list;

static u64 skx_tolm, skx_tohm;
static int skx_num_sockets;
static unsigned int nvdimm_count;

#define	MASK26	0x3FFFFFF		/* Mask for 2^26 */
#define MASK29	0x1FFFFFFF		/* Mask for 2^29 */

static struct skx_dev *get_skx_dev(struct pci_bus *bus, u8 idx)
{
	struct skx_dev *d;

	list_for_each_entry(d, skx_edac_list, list) {
		if (d->seg == pci_domain_nr(bus) && d->bus[idx] == bus->number)
			return d;
	}

	return NULL;
}

enum munittype {
	CHAN0, CHAN1, CHAN2, SAD_ALL, UTIL_ALL, SAD
};

struct munit {
	u16	did;
	u16	devfn[SKX_NUM_IMC];
	u8	busidx;
	u8	per_socket;
	enum munittype mtype;
};

/*
 * List of PCI device ids that we need together with some device
 * number and function numbers to tell which memory controller the
 * device belongs to.
 */
static const struct munit skx_all_munits[] = {
	{ 0x2054, { }, 1, 1, SAD_ALL },
	{ 0x2055, { }, 1, 1, UTIL_ALL },
	{ 0x2040, { PCI_DEVFN(10, 0), PCI_DEVFN(12, 0) }, 2, 2, CHAN0 },
	{ 0x2044, { PCI_DEVFN(10, 4), PCI_DEVFN(12, 4) }, 2, 2, CHAN1 },
	{ 0x2048, { PCI_DEVFN(11, 0), PCI_DEVFN(13, 0) }, 2, 2, CHAN2 },
	{ 0x208e, { }, 1, 0, SAD },
	{ }
};

static int get_all_munits(const struct munit *m)
{
	struct pci_dev *pdev, *prev;
	struct skx_dev *d;
	u32 reg;
	int i = 0, ndev = 0;

	prev = NULL;
	for (;;) {
		pdev = pci_get_device(PCI_VENDOR_ID_INTEL, m->did, prev);
		if (!pdev)
			break;
		ndev++;
		if (m->per_socket == SKX_NUM_IMC) {
			for (i = 0; i < SKX_NUM_IMC; i++)
				if (m->devfn[i] == pdev->devfn)
					break;
			if (i == SKX_NUM_IMC)
				goto fail;
		}
		d = get_skx_dev(pdev->bus, m->busidx);
		if (!d)
			goto fail;

		/* Be sure that the device is enabled */
		if (unlikely(pci_enable_device(pdev) < 0)) {
			skx_printk(KERN_ERR, "Couldn't enable device %04x:%04x\n",
				   PCI_VENDOR_ID_INTEL, m->did);
			goto fail;
		}

		switch (m->mtype) {
		case CHAN0: case CHAN1: case CHAN2:
			pci_dev_get(pdev);
			d->imc[i].chan[m->mtype].cdev = pdev;
			break;
		case SAD_ALL:
			pci_dev_get(pdev);
			d->sad_all = pdev;
			break;
		case UTIL_ALL:
			pci_dev_get(pdev);
			d->util_all = pdev;
			break;
		case SAD:
			/*
			 * one of these devices per core, including cores
			 * that don't exist on this SKU. Ignore any that
			 * read a route table of zero, make sure all the
			 * non-zero values match.
			 */
			pci_read_config_dword(pdev, 0xB4, &reg);
			if (reg != 0) {
				if (d->mcroute == 0) {
					d->mcroute = reg;
				} else if (d->mcroute != reg) {
					skx_printk(KERN_ERR, "mcroute mismatch\n");
					goto fail;
				}
			}
			ndev--;
			break;
		}

		prev = pdev;
	}

	return ndev;
fail:
	pci_dev_put(pdev);
	return -ENODEV;
}

static const struct x86_cpu_id skx_cpuids[] = {
	{ X86_VENDOR_INTEL, 6, INTEL_FAM6_SKYLAKE_X, 0, 0 },
	{ }
};
MODULE_DEVICE_TABLE(x86cpu, skx_cpuids);

#define SKX_GET_MTMTR(dev, reg) \
	pci_read_config_dword((dev), 0x87c, &(reg))

static bool skx_check_ecc(struct pci_dev *pdev)
{
	u32 mtmtr;

	SKX_GET_MTMTR(pdev, mtmtr);

	return !!GET_BITFIELD(mtmtr, 2, 2);
}

static int skx_get_dimm_config(struct mem_ctl_info *mci)
{
	struct skx_pvt *pvt = mci->pvt_info;
	struct skx_imc *imc = pvt->imc;
	u32 mtr, amap, mcddrtcfg;
	struct dimm_info *dimm;
	int i, j;
	int ndimms;

	for (i = 0; i < SKX_NUM_CHANNELS; i++) {
		ndimms = 0;
		pci_read_config_dword(imc->chan[i].cdev, 0x8C, &amap);
		pci_read_config_dword(imc->chan[i].cdev, 0x400, &mcddrtcfg);
		for (j = 0; j < SKX_NUM_DIMMS; j++) {
			dimm = EDAC_DIMM_PTR(mci->layers, mci->dimms,
					     mci->n_layers, i, j, 0);
			pci_read_config_dword(imc->chan[i].cdev,
					      0x80 + 4 * j, &mtr);
			if (IS_DIMM_PRESENT(mtr)) {
				ndimms += skx_get_dimm_info(mtr, amap, dimm, imc, i, j);
			} else if (IS_NVDIMM_PRESENT(mcddrtcfg, j)) {
				ndimms += skx_get_nvdimm_info(dimm, imc, i, j,
							      EDAC_MOD_STR);
				nvdimm_count++;
			}
		}
		if (ndimms && !skx_check_ecc(imc->chan[0].cdev)) {
			skx_printk(KERN_ERR, "ECC is disabled on imc %d\n", imc->mc);
			return -ENODEV;
		}
	}

	return 0;
}

#define	SKX_MAX_SAD 24

#define SKX_GET_SAD(d, i, reg)	\
	pci_read_config_dword((d)->sad_all, 0x60 + 8 * (i), &(reg))
#define SKX_GET_ILV(d, i, reg)	\
	pci_read_config_dword((d)->sad_all, 0x64 + 8 * (i), &(reg))

#define	SKX_SAD_MOD3MODE(sad)	GET_BITFIELD((sad), 30, 31)
#define	SKX_SAD_MOD3(sad)	GET_BITFIELD((sad), 27, 27)
#define SKX_SAD_LIMIT(sad)	(((u64)GET_BITFIELD((sad), 7, 26) << 26) | MASK26)
#define	SKX_SAD_MOD3ASMOD2(sad)	GET_BITFIELD((sad), 5, 6)
#define	SKX_SAD_ATTR(sad)	GET_BITFIELD((sad), 3, 4)
#define	SKX_SAD_INTERLEAVE(sad)	GET_BITFIELD((sad), 1, 2)
#define SKX_SAD_ENABLE(sad)	GET_BITFIELD((sad), 0, 0)

#define SKX_ILV_REMOTE(tgt)	(((tgt) & 8) == 0)
#define SKX_ILV_TARGET(tgt)	((tgt) & 7)

static bool skx_sad_decode(struct decoded_addr *res)
{
	struct skx_dev *d = list_first_entry(skx_edac_list, typeof(*d), list);
	u64 addr = res->addr;
	int i, idx, tgt, lchan, shift;
	u32 sad, ilv;
	u64 limit, prev_limit;
	int remote = 0;

	/* Simple sanity check for I/O space or out of range */
	if (addr >= skx_tohm || (addr >= skx_tolm && addr < BIT_ULL(32))) {
		edac_dbg(0, "Address 0x%llx out of range\n", addr);
		return false;
	}

restart:
	prev_limit = 0;
	for (i = 0; i < SKX_MAX_SAD; i++) {
		SKX_GET_SAD(d, i, sad);
		limit = SKX_SAD_LIMIT(sad);
		if (SKX_SAD_ENABLE(sad)) {
			if (addr >= prev_limit && addr <= limit)
				goto sad_found;
		}
		prev_limit = limit + 1;
	}
	edac_dbg(0, "No SAD entry for 0x%llx\n", addr);
	return false;

sad_found:
	SKX_GET_ILV(d, i, ilv);

	switch (SKX_SAD_INTERLEAVE(sad)) {
	case 0:
		idx = GET_BITFIELD(addr, 6, 8);
		break;
	case 1:
		idx = GET_BITFIELD(addr, 8, 10);
		break;
	case 2:
		idx = GET_BITFIELD(addr, 12, 14);
		break;
	case 3:
		idx = GET_BITFIELD(addr, 30, 32);
		break;
	}

	tgt = GET_BITFIELD(ilv, 4 * idx, 4 * idx + 3);

	/* If point to another node, find it and start over */
	if (SKX_ILV_REMOTE(tgt)) {
		if (remote) {
			edac_dbg(0, "Double remote!\n");
			return false;
		}
		remote = 1;
		list_for_each_entry(d, skx_edac_list, list) {
			if (d->imc[0].src_id == SKX_ILV_TARGET(tgt))
				goto restart;
		}
		edac_dbg(0, "Can't find node %d\n", SKX_ILV_TARGET(tgt));
		return false;
	}

	if (SKX_SAD_MOD3(sad) == 0) {
		lchan = SKX_ILV_TARGET(tgt);
	} else {
		switch (SKX_SAD_MOD3MODE(sad)) {
		case 0:
			shift = 6;
			break;
		case 1:
			shift = 8;
			break;
		case 2:
			shift = 12;
			break;
		default:
			edac_dbg(0, "illegal mod3mode\n");
			return false;
		}
		switch (SKX_SAD_MOD3ASMOD2(sad)) {
		case 0:
			lchan = (addr >> shift) % 3;
			break;
		case 1:
			lchan = (addr >> shift) % 2;
			break;
		case 2:
			lchan = (addr >> shift) % 2;
			lchan = (lchan << 1) | !lchan;
			break;
		case 3:
			lchan = ((addr >> shift) % 2) << 1;
			break;
		}
		lchan = (lchan << 1) | (SKX_ILV_TARGET(tgt) & 1);
	}

	res->dev = d;
	res->socket = d->imc[0].src_id;
	res->imc = GET_BITFIELD(d->mcroute, lchan * 3, lchan * 3 + 2);
	res->channel = GET_BITFIELD(d->mcroute, lchan * 2 + 18, lchan * 2 + 19);

	edac_dbg(2, "0x%llx: socket=%d imc=%d channel=%d\n",
		 res->addr, res->socket, res->imc, res->channel);
	return true;
}

#define	SKX_MAX_TAD 8

#define SKX_GET_TADBASE(d, mc, i, reg)			\
	pci_read_config_dword((d)->imc[mc].chan[0].cdev, 0x850 + 4 * (i), &(reg))
#define SKX_GET_TADWAYNESS(d, mc, i, reg)		\
	pci_read_config_dword((d)->imc[mc].chan[0].cdev, 0x880 + 4 * (i), &(reg))
#define SKX_GET_TADCHNILVOFFSET(d, mc, ch, i, reg)	\
	pci_read_config_dword((d)->imc[mc].chan[ch].cdev, 0x90 + 4 * (i), &(reg))

#define	SKX_TAD_BASE(b)		((u64)GET_BITFIELD((b), 12, 31) << 26)
#define SKX_TAD_SKT_GRAN(b)	GET_BITFIELD((b), 4, 5)
#define SKX_TAD_CHN_GRAN(b)	GET_BITFIELD((b), 6, 7)
#define	SKX_TAD_LIMIT(b)	(((u64)GET_BITFIELD((b), 12, 31) << 26) | MASK26)
#define	SKX_TAD_OFFSET(b)	((u64)GET_BITFIELD((b), 4, 23) << 26)
#define	SKX_TAD_SKTWAYS(b)	(1 << GET_BITFIELD((b), 10, 11))
#define	SKX_TAD_CHNWAYS(b)	(GET_BITFIELD((b), 8, 9) + 1)

/* which bit used for both socket and channel interleave */
static int skx_granularity[] = { 6, 8, 12, 30 };

static u64 skx_do_interleave(u64 addr, int shift, int ways, u64 lowbits)
{
	addr >>= shift;
	addr /= ways;
	addr <<= shift;

	return addr | (lowbits & ((1ull << shift) - 1));
}

static bool skx_tad_decode(struct decoded_addr *res)
{
	int i;
	u32 base, wayness, chnilvoffset;
	int skt_interleave_bit, chn_interleave_bit;
	u64 channel_addr;

	for (i = 0; i < SKX_MAX_TAD; i++) {
		SKX_GET_TADBASE(res->dev, res->imc, i, base);
		SKX_GET_TADWAYNESS(res->dev, res->imc, i, wayness);
		if (SKX_TAD_BASE(base) <= res->addr && res->addr <= SKX_TAD_LIMIT(wayness))
			goto tad_found;
	}
	edac_dbg(0, "No TAD entry for 0x%llx\n", res->addr);
	return false;

tad_found:
	res->sktways = SKX_TAD_SKTWAYS(wayness);
	res->chanways = SKX_TAD_CHNWAYS(wayness);
	skt_interleave_bit = skx_granularity[SKX_TAD_SKT_GRAN(base)];
	chn_interleave_bit = skx_granularity[SKX_TAD_CHN_GRAN(base)];

	SKX_GET_TADCHNILVOFFSET(res->dev, res->imc, res->channel, i, chnilvoffset);
	channel_addr = res->addr - SKX_TAD_OFFSET(chnilvoffset);

	if (res->chanways == 3 && skt_interleave_bit > chn_interleave_bit) {
		/* Must handle channel first, then socket */
		channel_addr = skx_do_interleave(channel_addr, chn_interleave_bit,
						 res->chanways, channel_addr);
		channel_addr = skx_do_interleave(channel_addr, skt_interleave_bit,
						 res->sktways, channel_addr);
	} else {
		/* Handle socket then channel. Preserve low bits from original address */
		channel_addr = skx_do_interleave(channel_addr, skt_interleave_bit,
						 res->sktways, res->addr);
		channel_addr = skx_do_interleave(channel_addr, chn_interleave_bit,
						 res->chanways, res->addr);
	}

	res->chan_addr = channel_addr;

	edac_dbg(2, "0x%llx: chan_addr=0x%llx sktways=%d chanways=%d\n",
		 res->addr, res->chan_addr, res->sktways, res->chanways);
	return true;
}

#define SKX_MAX_RIR 4

#define SKX_GET_RIRWAYNESS(d, mc, ch, i, reg)		\
	pci_read_config_dword((d)->imc[mc].chan[ch].cdev,	\
			      0x108 + 4 * (i), &(reg))
#define SKX_GET_RIRILV(d, mc, ch, idx, i, reg)		\
	pci_read_config_dword((d)->imc[mc].chan[ch].cdev,	\
			      0x120 + 16 * (idx) + 4 * (i), &(reg))

#define	SKX_RIR_VALID(b) GET_BITFIELD((b), 31, 31)
#define	SKX_RIR_LIMIT(b) (((u64)GET_BITFIELD((b), 1, 11) << 29) | MASK29)
#define	SKX_RIR_WAYS(b) (1 << GET_BITFIELD((b), 28, 29))
#define	SKX_RIR_CHAN_RANK(b) GET_BITFIELD((b), 16, 19)
#define	SKX_RIR_OFFSET(b) ((u64)(GET_BITFIELD((b), 2, 15) << 26))

static bool skx_rir_decode(struct decoded_addr *res)
{
	int i, idx, chan_rank;
	int shift;
	u32 rirway, rirlv;
	u64 rank_addr, prev_limit = 0, limit;

	if (res->dev->imc[res->imc].chan[res->channel].dimms[0].close_pg)
		shift = 6;
	else
		shift = 13;

	for (i = 0; i < SKX_MAX_RIR; i++) {
		SKX_GET_RIRWAYNESS(res->dev, res->imc, res->channel, i, rirway);
		limit = SKX_RIR_LIMIT(rirway);
		if (SKX_RIR_VALID(rirway)) {
			if (prev_limit <= res->chan_addr &&
			    res->chan_addr <= limit)
				goto rir_found;
		}
		prev_limit = limit;
	}
	edac_dbg(0, "No RIR entry for 0x%llx\n", res->addr);
	return false;

rir_found:
	rank_addr = res->chan_addr >> shift;
	rank_addr /= SKX_RIR_WAYS(rirway);
	rank_addr <<= shift;
	rank_addr |= res->chan_addr & GENMASK_ULL(shift - 1, 0);

	res->rank_address = rank_addr;
	idx = (res->chan_addr >> shift) % SKX_RIR_WAYS(rirway);

	SKX_GET_RIRILV(res->dev, res->imc, res->channel, idx, i, rirlv);
	res->rank_address = rank_addr - SKX_RIR_OFFSET(rirlv);
	chan_rank = SKX_RIR_CHAN_RANK(rirlv);
	res->channel_rank = chan_rank;
	res->dimm = chan_rank / 4;
	res->rank = chan_rank % 4;

	edac_dbg(2, "0x%llx: dimm=%d rank=%d chan_rank=%d rank_addr=0x%llx\n",
		 res->addr, res->dimm, res->rank,
		 res->channel_rank, res->rank_address);
	return true;
}

static u8 skx_close_row[] = {
	15, 16, 17, 18, 20, 21, 22, 28, 10, 11, 12, 13, 29, 30, 31, 32, 33
};

static u8 skx_close_column[] = {
	3, 4, 5, 14, 19, 23, 24, 25, 26, 27
};

static u8 skx_open_row[] = {
	14, 15, 16, 20, 28, 21, 22, 23, 24, 25, 26, 27, 29, 30, 31, 32, 33
};

static u8 skx_open_column[] = {
	3, 4, 5, 6, 7, 8, 9, 10, 11, 12
};

static u8 skx_open_fine_column[] = {
	3, 4, 5, 7, 8, 9, 10, 11, 12, 13
};

static int skx_bits(u64 addr, int nbits, u8 *bits)
{
	int i, res = 0;

	for (i = 0; i < nbits; i++)
		res |= ((addr >> bits[i]) & 1) << i;
	return res;
}

static int skx_bank_bits(u64 addr, int b0, int b1, int do_xor, int x0, int x1)
{
	int ret = GET_BITFIELD(addr, b0, b0) | (GET_BITFIELD(addr, b1, b1) << 1);

	if (do_xor)
		ret ^= GET_BITFIELD(addr, x0, x0) | (GET_BITFIELD(addr, x1, x1) << 1);

	return ret;
}

static bool skx_mad_decode(struct decoded_addr *r)
{
	struct skx_dimm *dimm = &r->dev->imc[r->imc].chan[r->channel].dimms[r->dimm];
	int bg0 = dimm->fine_grain_bank ? 6 : 13;

	if (dimm->close_pg) {
		r->row = skx_bits(r->rank_address, dimm->rowbits, skx_close_row);
		r->column = skx_bits(r->rank_address, dimm->colbits, skx_close_column);
		r->column |= 0x400; /* C10 is autoprecharge, always set */
		r->bank_address = skx_bank_bits(r->rank_address, 8, 9, dimm->bank_xor_enable, 22, 28);
		r->bank_group = skx_bank_bits(r->rank_address, 6, 7, dimm->bank_xor_enable, 20, 21);
	} else {
		r->row = skx_bits(r->rank_address, dimm->rowbits, skx_open_row);
		if (dimm->fine_grain_bank)
			r->column = skx_bits(r->rank_address, dimm->colbits, skx_open_fine_column);
		else
			r->column = skx_bits(r->rank_address, dimm->colbits, skx_open_column);
		r->bank_address = skx_bank_bits(r->rank_address, 18, 19, dimm->bank_xor_enable, 22, 23);
		r->bank_group = skx_bank_bits(r->rank_address, bg0, 17, dimm->bank_xor_enable, 20, 21);
	}
	r->row &= (1u << dimm->rowbits) - 1;

	edac_dbg(2, "0x%llx: row=0x%x col=0x%x bank_addr=%d bank_group=%d\n",
		 r->addr, r->row, r->column, r->bank_address,
		 r->bank_group);
	return true;
}

static bool skx_decode(struct decoded_addr *res)
{
	return skx_sad_decode(res) && skx_tad_decode(res) &&
		skx_rir_decode(res) && skx_mad_decode(res);
}

static struct notifier_block skx_mce_dec = {
	.notifier_call	= skx_mce_check_error,
	.priority	= MCE_PRIO_EDAC,
};

/*
 * skx_init:
 *	make sure we are running on the correct cpu model
 *	search for all the devices we need
 *	check which DIMMs are present.
 */
static int __init skx_init(void)
{
	const struct x86_cpu_id *id;
	const struct munit *m;
	const char *owner;
	int rc = 0, i, off[3] = {0xd0, 0xd4, 0xd8};
	u8 mc = 0, src_id, node_id;
	struct skx_dev *d;

	edac_dbg(2, "\n");

	owner = edac_get_owner();
	if (owner && strncmp(owner, EDAC_MOD_STR, sizeof(EDAC_MOD_STR)))
		return -EBUSY;

	id = x86_match_cpu(skx_cpuids);
	if (!id)
		return -ENODEV;

	rc = skx_get_hi_lo(0x2034, off, &skx_tolm, &skx_tohm);
	if (rc)
		return rc;

	rc = skx_get_all_bus_mappings(0x2016, 0xcc, SKX, &skx_edac_list);
	if (rc < 0)
		goto fail;
	if (rc == 0) {
		edac_dbg(2, "No memory controllers found\n");
		return -ENODEV;
	}
	skx_num_sockets = rc;

	for (m = skx_all_munits; m->did; m++) {
		rc = get_all_munits(m);
		if (rc < 0)
			goto fail;
		if (rc != m->per_socket * skx_num_sockets) {
			edac_dbg(2, "Expected %d, got %d of 0x%x\n",
				 m->per_socket * skx_num_sockets, rc, m->did);
			rc = -ENODEV;
			goto fail;
		}
	}

	list_for_each_entry(d, skx_edac_list, list) {
		rc = skx_get_src_id(d, &src_id);
		if (rc < 0)
			goto fail;
		rc = skx_get_node_id(d, &node_id);
		if (rc < 0)
			goto fail;
		edac_dbg(2, "src_id=%d node_id=%d\n", src_id, node_id);
		for (i = 0; i < SKX_NUM_IMC; i++) {
			d->imc[i].mc = mc++;
			d->imc[i].lmc = i;
			d->imc[i].src_id = src_id;
			d->imc[i].node_id = node_id;
			rc = skx_register_mci(&d->imc[i], d->imc[i].chan[0].cdev,
					      "Skylake Socket", EDAC_MOD_STR,
					      skx_get_dimm_config);
			if (rc < 0)
				goto fail;
		}
	}

	skx_set_decode(skx_decode);

	if (nvdimm_count && skx_adxl_get() == -ENODEV)
		skx_printk(KERN_NOTICE, "Only decoding DDR4 address!\n");

	/* Ensure that the OPSTATE is set correctly for POLL or NMI */
	opstate_init();

	setup_skx_debug("skx_test");

	mce_register_decode_chain(&skx_mce_dec);

	return 0;
fail:
	skx_remove();
	return rc;
}

static void __exit skx_exit(void)
{
	edac_dbg(2, "\n");
	mce_unregister_decode_chain(&skx_mce_dec);
	teardown_skx_debug();
	if (nvdimm_count)
		skx_adxl_put();
	skx_remove();
}

module_init(skx_init);
module_exit(skx_exit);

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tony Luck");
MODULE_DESCRIPTION("MC Driver for Intel Skylake server processors");
