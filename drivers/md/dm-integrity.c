/*
 * Copyright (C) 2016-2017 Red Hat, Inc. All rights reserved.
 * Copyright (C) 2016-2017 Milan Broz
 * Copyright (C) 2016-2017 Mikulas Patocka
 *
 * This file is released under the GPL.
 */

#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/device-mapper.h>
#include <linux/dm-io.h>
#include <linux/vmalloc.h>
#include <linux/sort.h>
#include <linux/rbtree.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <linux/async_tx.h>
#include <linux/dm-bufio.h>

#define DM_MSG_PREFIX "integrity"

#define DEFAULT_INTERLEAVE_SECTORS	32768
#define DEFAULT_JOURNAL_SIZE_FACTOR	7
#define DEFAULT_BUFFER_SECTORS		128
#define DEFAULT_JOURNAL_WATERMARK	50
#define DEFAULT_SYNC_MSEC		10000
#define DEFAULT_MAX_JOURNAL_SECTORS	131072
#define MIN_LOG2_INTERLEAVE_SECTORS	3
#define MAX_LOG2_INTERLEAVE_SECTORS	31
#define METADATA_WORKQUEUE_MAX_ACTIVE	16
#define RECALC_SECTORS			8192
#define RECALC_WRITE_SUPER		16

/*
 * Warning - DEBUG_PRINT prints security-sensitive data to the log,
 * so it should not be enabled in the official kernel
 */
//#define DEBUG_PRINT
//#define INTERNAL_VERIFY

/*
 * On disk structures
 */

#define SB_MAGIC			"integrt"
#define SB_VERSION_1			1
#define SB_VERSION_2			2
#define SB_SECTORS			8
#define MAX_SECTORS_PER_BLOCK		8

struct superblock {
	__u8 magic[8];
	__u8 version;
	__u8 log2_interleave_sectors;
	__u16 integrity_tag_size;
	__u32 journal_sections;
	__u64 provided_data_sectors;	/* userspace uses this value */
	__u32 flags;
	__u8 log2_sectors_per_block;
	__u8 pad[3];
	__u64 recalc_sector;
};

#define SB_FLAG_HAVE_JOURNAL_MAC	0x1
#define SB_FLAG_RECALCULATING		0x2

#define	JOURNAL_ENTRY_ROUNDUP		8

typedef __u64 commit_id_t;
#define JOURNAL_MAC_PER_SECTOR		8

struct journal_entry {
	union {
		struct {
			__u32 sector_lo;
			__u32 sector_hi;
		} s;
		__u64 sector;
	} u;
	commit_id_t last_bytes[0];
	/* __u8 tag[0]; */
};

#define journal_entry_tag(ic, je)		((__u8 *)&(je)->last_bytes[(ic)->sectors_per_block])

#if BITS_PER_LONG == 64
#define journal_entry_set_sector(je, x)		do { smp_wmb(); WRITE_ONCE((je)->u.sector, cpu_to_le64(x)); } while (0)
#define journal_entry_get_sector(je)		le64_to_cpu((je)->u.sector)
#elif defined(CONFIG_LBDAF)
#define journal_entry_set_sector(je, x)		do { (je)->u.s.sector_lo = cpu_to_le32(x); smp_wmb(); WRITE_ONCE((je)->u.s.sector_hi, cpu_to_le32((x) >> 32)); } while (0)
#define journal_entry_get_sector(je)		le64_to_cpu((je)->u.sector)
#else
#define journal_entry_set_sector(je, x)		do { (je)->u.s.sector_lo = cpu_to_le32(x); smp_wmb(); WRITE_ONCE((je)->u.s.sector_hi, cpu_to_le32(0)); } while (0)
#define journal_entry_get_sector(je)		le32_to_cpu((je)->u.s.sector_lo)
#endif
#define journal_entry_is_unused(je)		((je)->u.s.sector_hi == cpu_to_le32(-1))
#define journal_entry_set_unused(je)		do { ((je)->u.s.sector_hi = cpu_to_le32(-1)); } while (0)
#define journal_entry_is_inprogress(je)		((je)->u.s.sector_hi == cpu_to_le32(-2))
#define journal_entry_set_inprogress(je)	do { ((je)->u.s.sector_hi = cpu_to_le32(-2)); } while (0)

#define JOURNAL_BLOCK_SECTORS		8
#define JOURNAL_SECTOR_DATA		((1 << SECTOR_SHIFT) - sizeof(commit_id_t))
#define JOURNAL_MAC_SIZE		(JOURNAL_MAC_PER_SECTOR * JOURNAL_BLOCK_SECTORS)

struct journal_sector {
	__u8 entries[JOURNAL_SECTOR_DATA - JOURNAL_MAC_PER_SECTOR];
	__u8 mac[JOURNAL_MAC_PER_SECTOR];
	commit_id_t commit_id;
};

#define MAX_TAG_SIZE			(JOURNAL_SECTOR_DATA - JOURNAL_MAC_PER_SECTOR - offsetof(struct journal_entry, last_bytes[MAX_SECTORS_PER_BLOCK]))

#define METADATA_PADDING_SECTORS	8

#define N_COMMIT_IDS			4

static unsigned char prev_commit_seq(unsigned char seq)
{
	return (seq + N_COMMIT_IDS - 1) % N_COMMIT_IDS;
}

static unsigned char next_commit_seq(unsigned char seq)
{
	return (seq + 1) % N_COMMIT_IDS;
}

/*
 * In-memory structures
 */

struct journal_node {
	struct rb_node node;
	sector_t sector;
};

struct alg_spec {
	char *alg_string;
	char *key_string;
	__u8 *key;
	unsigned key_size;
};

struct dm_integrity_c {
	struct dm_dev *dev;
	struct dm_dev *meta_dev;
	unsigned tag_size;
	__s8 log2_tag_size;
	sector_t start;
	mempool_t journal_io_mempool;
	struct dm_io_client *io;
	struct dm_bufio_client *bufio;
	struct workqueue_struct *metadata_wq;
	struct superblock *sb;
	unsigned journal_pages;
	struct page_list *journal;
	struct page_list *journal_io;
	struct page_list *journal_xor;

	struct crypto_skcipher *journal_crypt;
	struct scatterlist **journal_scatterlist;
	struct scatterlist **journal_io_scatterlist;
	struct skcipher_request **sk_requests;

	struct crypto_shash *journal_mac;

	struct journal_node *journal_tree;
	struct rb_root journal_tree_root;

	sector_t provided_data_sectors;

	unsigned short journal_entry_size;
	unsigned char journal_entries_per_sector;
	unsigned char journal_section_entries;
	unsigned short journal_section_sectors;
	unsigned journal_sections;
	unsigned journal_entries;
	sector_t data_device_sectors;
	sector_t meta_device_sectors;
	unsigned initial_sectors;
	unsigned metadata_run;
	__s8 log2_metadata_run;
	__u8 log2_buffer_sectors;
	__u8 sectors_per_block;

	unsigned char mode;

	int failed;

	struct crypto_shash *internal_hash;

	struct dm_target *ti;

	/* these variables are locked with endio_wait.lock */
	struct rb_root in_progress;
	struct list_head wait_list;
	wait_queue_head_t endio_wait;
	struct workqueue_struct *wait_wq;

	unsigned char commit_seq;
	commit_id_t commit_ids[N_COMMIT_IDS];

	unsigned committed_section;
	unsigned n_committed_sections;

	unsigned uncommitted_section;
	unsigned n_uncommitted_sections;

	unsigned free_section;
	unsigned char free_section_entry;
	unsigned free_sectors;

	unsigned free_sectors_threshold;

	struct workqueue_struct *commit_wq;
	struct work_struct commit_work;

	struct workqueue_struct *writer_wq;
	struct work_struct writer_work;

	struct workqueue_struct *recalc_wq;
	struct work_struct recalc_work;
	u8 *recalc_buffer;
	u8 *recalc_tags;

	struct bio_list flush_bio_list;

	unsigned long autocommit_jiffies;
	struct timer_list autocommit_timer;
	unsigned autocommit_msec;

	wait_queue_head_t copy_to_journal_wait;

	struct completion crypto_backoff;

	bool journal_uptodate;
	bool just_formatted;

	struct alg_spec internal_hash_alg;
	struct alg_spec journal_crypt_alg;
	struct alg_spec journal_mac_alg;

	atomic64_t number_of_mismatches;
};

struct dm_integrity_range {
	sector_t logical_sector;
	unsigned n_sectors;
	bool waiting;
	union {
		struct rb_node node;
		struct {
			struct task_struct *task;
			struct list_head wait_entry;
		};
	};
};

struct dm_integrity_io {
	struct work_struct work;

	struct dm_integrity_c *ic;
	bool write;
	bool fua;

	struct dm_integrity_range range;

	sector_t metadata_block;
	unsigned metadata_offset;

	atomic_t in_flight;
	blk_status_t bi_status;

	struct completion *completion;

	struct gendisk *orig_bi_disk;
	u8 orig_bi_partno;
	bio_end_io_t *orig_bi_end_io;
	struct bio_integrity_payload *orig_bi_integrity;
	struct bvec_iter orig_bi_iter;
};

struct journal_completion {
	struct dm_integrity_c *ic;
	atomic_t in_flight;
	struct completion comp;
};

struct journal_io {
	struct dm_integrity_range range;
	struct journal_completion *comp;
};

static struct kmem_cache *journal_io_cache;

#define JOURNAL_IO_MEMPOOL	32

#ifdef DEBUG_PRINT
#define DEBUG_print(x, ...)	printk(KERN_DEBUG x, ##__VA_ARGS__)
static void __DEBUG_bytes(__u8 *bytes, size_t len, const char *msg, ...)
{
	va_list args;
	va_start(args, msg);
	vprintk(msg, args);
	va_end(args);
	if (len)
		pr_cont(":");
	while (len) {
		pr_cont(" %02x", *bytes);
		bytes++;
		len--;
	}
	pr_cont("\n");
}
#define DEBUG_bytes(bytes, len, msg, ...)	__DEBUG_bytes(bytes, len, KERN_DEBUG msg, ##__VA_ARGS__)
#else
#define DEBUG_print(x, ...)			do { } while (0)
#define DEBUG_bytes(bytes, len, msg, ...)	do { } while (0)
#endif

/*
 * DM Integrity profile, protection is performed layer above (dm-crypt)
 */
static const struct blk_integrity_profile dm_integrity_profile = {
	.name			= "DM-DIF-EXT-TAG",
	.generate_fn		= NULL,
	.verify_fn		= NULL,
};

static void dm_integrity_map_continue(struct dm_integrity_io *dio, bool from_map);
static void integrity_bio_wait(struct work_struct *w);
static void dm_integrity_dtr(struct dm_target *ti);

static void dm_integrity_io_error(struct dm_integrity_c *ic, const char *msg, int err)
{
	if (err == -EILSEQ)
		atomic64_inc(&ic->number_of_mismatches);
	if (!cmpxchg(&ic->failed, 0, err))
		DMERR("Error on %s: %d", msg, err);
}

static int dm_integrity_failed(struct dm_integrity_c *ic)
{
	return READ_ONCE(ic->failed);
}

static commit_id_t dm_integrity_commit_id(struct dm_integrity_c *ic, unsigned i,
					  unsigned j, unsigned char seq)
{
	/*
	 * Xor the number with section and sector, so that if a piece of
	 * journal is written at wrong place, it is detected.
	 */
	return ic->commit_ids[seq] ^ cpu_to_le64(((__u64)i << 32) ^ j);
}

static void get_area_and_offset(struct dm_integrity_c *ic, sector_t data_sector,
				sector_t *area, sector_t *offset)
{
	if (!ic->meta_dev) {
		__u8 log2_interleave_sectors = ic->sb->log2_interleave_sectors;
		*area = data_sector >> log2_interleave_sectors;
		*offset = (unsigned)data_sector & ((1U << log2_interleave_sectors) - 1);
	} else {
		*area = 0;
		*offset = data_sector;
	}
}

#define sector_to_block(ic, n)						\
do {									\
	BUG_ON((n) & (unsigned)((ic)->sectors_per_block - 1));		\
	(n) >>= (ic)->sb->log2_sectors_per_block;			\
} while (0)

static __u64 get_metadata_sector_and_offset(struct dm_integrity_c *ic, sector_t area,
					    sector_t offset, unsigned *metadata_offset)
{
	__u64 ms;
	unsigned mo;

	ms = area << ic->sb->log2_interleave_sectors;
	if (likely(ic->log2_metadata_run >= 0))
		ms += area << ic->log2_metadata_run;
	else
		ms += area * ic->metadata_run;
	ms >>= ic->log2_buffer_sectors;

	sector_to_block(ic, offset);

	if (likely(ic->log2_tag_size >= 0)) {
		ms += offset >> (SECTOR_SHIFT + ic->log2_buffer_sectors - ic->log2_tag_size);
		mo = (offset << ic->log2_tag_size) & ((1U << SECTOR_SHIFT << ic->log2_buffer_sectors) - 1);
	} else {
		ms += (__u64)offset * ic->tag_size >> (SECTOR_SHIFT + ic->log2_buffer_sectors);
		mo = (offset * ic->tag_size) & ((1U << SECTOR_SHIFT << ic->log2_buffer_sectors) - 1);
	}
	*metadata_offset = mo;
	return ms;
}

static sector_t get_data_sector(struct dm_integrity_c *ic, sector_t area, sector_t offset)
{
	sector_t result;

	if (ic->meta_dev)
		return offset;

	result = area << ic->sb->log2_interleave_sectors;
	if (likely(ic->log2_metadata_run >= 0))
		result += (area + 1) << ic->log2_metadata_run;
	else
		result += (area + 1) * ic->metadata_run;

	result += (sector_t)ic->initial_sectors + offset;
	result += ic->start;

	return result;
}

static void wraparound_section(struct dm_integrity_c *ic, unsigned *sec_ptr)
{
	if (unlikely(*sec_ptr >= ic->journal_sections))
		*sec_ptr -= ic->journal_sections;
}

static void sb_set_version(struct dm_integrity_c *ic)
{
	if (ic->meta_dev || ic->sb->flags & cpu_to_le32(SB_FLAG_RECALCULATING))
		ic->sb->version = SB_VERSION_2;
	else
		ic->sb->version = SB_VERSION_1;
}

static int sync_rw_sb(struct dm_integrity_c *ic, int op, int op_flags)
{
	struct dm_io_request io_req;
	struct dm_io_region io_loc;

	io_req.bi_op = op;
	io_req.bi_op_flags = op_flags;
	io_req.mem.type = DM_IO_KMEM;
	io_req.mem.ptr.addr = ic->sb;
	io_req.notify.fn = NULL;
	io_req.client = ic->io;
	io_loc.bdev = ic->meta_dev ? ic->meta_dev->bdev : ic->dev->bdev;
	io_loc.sector = ic->start;
	io_loc.count = SB_SECTORS;

	return dm_io(&io_req, 1, &io_loc, NULL);
}

static void access_journal_check(struct dm_integrity_c *ic, unsigned section, unsigned offset,
				 bool e, const char *function)
{
#if defined(CONFIG_DM_DEBUG) || defined(INTERNAL_VERIFY)
	unsigned limit = e ? ic->journal_section_entries : ic->journal_section_sectors;

	if (unlikely(section >= ic->journal_sections) ||
	    unlikely(offset >= limit)) {
		printk(KERN_CRIT "%s: invalid access at (%u,%u), limit (%u,%u)\n",
			function, section, offset, ic->journal_sections, limit);
		BUG();
	}
#endif
}

static void page_list_location(struct dm_integrity_c *ic, unsigned section, unsigned offset,
			       unsigned *pl_index, unsigned *pl_offset)
{
	unsigned sector;

	access_journal_check(ic, section, offset, false, "page_list_location");

	sector = section * ic->journal_section_sectors + offset;

	*pl_index = sector >> (PAGE_SHIFT - SECTOR_SHIFT);
	*pl_offset = (sector << SECTOR_SHIFT) & (PAGE_SIZE - 1);
}

static struct journal_sector *access_page_list(struct dm_integrity_c *ic, struct page_list *pl,
					       unsigned section, unsigned offset, unsigned *n_sectors)
{
	unsigned pl_index, pl_offset;
	char *va;

	page_list_location(ic, section, offset, &pl_index, &pl_offset);

	if (n_sectors)
		*n_sectors = (PAGE_SIZE - pl_offset) >> SECTOR_SHIFT;

	va = lowmem_page_address(pl[pl_index].page);

	return (struct journal_sector *)(va + pl_offset);
}

static struct journal_sector *access_journal(struct dm_integrity_c *ic, unsigned section, unsigned offset)
{
	return access_page_list(ic, ic->journal, section, offset, NULL);
}

static struct journal_entry *access_journal_entry(struct dm_integrity_c *ic, unsigned section, unsigned n)
{
	unsigned rel_sector, offset;
	struct journal_sector *js;

	access_journal_check(ic, section, n, true, "access_journal_entry");

	rel_sector = n % JOURNAL_BLOCK_SECTORS;
	offset = n / JOURNAL_BLOCK_SECTORS;

	js = access_journal(ic, section, rel_sector);
	return (struct journal_entry *)((char *)js + offset * ic->journal_entry_size);
}

static struct journal_sector *access_journal_data(struct dm_integrity_c *ic, unsigned section, unsigned n)
{
	n <<= ic->sb->log2_sectors_per_block;

	n += JOURNAL_BLOCK_SECTORS;

	access_journal_check(ic, section, n, false, "access_journal_data");

	return access_journal(ic, section, n);
}

static void section_mac(struct dm_integrity_c *ic, unsigned section, __u8 result[JOURNAL_MAC_SIZE])
{
	SHASH_DESC_ON_STACK(desc, ic->journal_mac);
	int r;
	unsigned j, size;

	desc->tfm = ic->journal_mac;
	desc->flags = 0;

	r = crypto_shash_init(desc);
	if (unlikely(r)) {
		dm_integrity_io_error(ic, "crypto_shash_init", r);
		goto err;
	}

	for (j = 0; j < ic->journal_section_entries; j++) {
		struct journal_entry *je = access_journal_entry(ic, section, j);
		r = crypto_shash_update(desc, (__u8 *)&je->u.sector, sizeof je->u.sector);
		if (unlikely(r)) {
			dm_integrity_io_error(ic, "crypto_shash_update", r);
			goto err;
		}
	}

	size = crypto_shash_digestsize(ic->journal_mac);

	if (likely(size <= JOURNAL_MAC_SIZE)) {
		r = crypto_shash_final(desc, result);
		if (unlikely(r)) {
			dm_integrity_io_error(ic, "crypto_shash_final", r);
			goto err;
		}
		memset(result + size, 0, JOURNAL_MAC_SIZE - size);
	} else {
		__u8 digest[size];
		r = crypto_shash_final(desc, digest);
		if (unlikely(r)) {
			dm_integrity_io_error(ic, "crypto_shash_final", r);
			goto err;
		}
		memcpy(result, digest, JOURNAL_MAC_SIZE);
	}

	return;
err:
	memset(result, 0, JOURNAL_MAC_SIZE);
}

static void rw_section_mac(struct dm_integrity_c *ic, unsigned section, bool wr)
{
	__u8 result[JOURNAL_MAC_SIZE];
	unsigned j;

	if (!ic->journal_mac)
		return;

	section_mac(ic, section, result);

	for (j = 0; j < JOURNAL_BLOCK_SECTORS; j++) {
		struct journal_sector *js = access_journal(ic, section, j);

		if (likely(wr))
			memcpy(&js->mac, result + (j * JOURNAL_MAC_PER_SECTOR), JOURNAL_MAC_PER_SECTOR);
		else {
			if (memcmp(&js->mac, result + (j * JOURNAL_MAC_PER_SECTOR), JOURNAL_MAC_PER_SECTOR))
				dm_integrity_io_error(ic, "journal mac", -EILSEQ);
		}
	}
}

static void complete_journal_op(void *context)
{
	struct journal_completion *comp = context;
	BUG_ON(!atomic_read(&comp->in_flight));
	if (likely(atomic_dec_and_test(&comp->in_flight)))
		complete(&comp->comp);
}

static void xor_journal(struct dm_integrity_c *ic, bool encrypt, unsigned section,
			unsigned n_sections, struct journal_completion *comp)
{
	struct async_submit_ctl submit;
	size_t n_bytes = (size_t)(n_sections * ic->journal_section_sectors) << SECTOR_SHIFT;
	unsigned pl_index, pl_offset, section_index;
	struct page_list *source_pl, *target_pl;

	if (likely(encrypt)) {
		source_pl = ic->journal;
		target_pl = ic->journal_io;
	} else {
		source_pl = ic->journal_io;
		target_pl = ic->journal;
	}

	page_list_location(ic, section, 0, &pl_index, &pl_offset);

	atomic_add(roundup(pl_offset + n_bytes, PAGE_SIZE) >> PAGE_SHIFT, &comp->in_flight);

	init_async_submit(&submit, ASYNC_TX_XOR_ZERO_DST, NULL, complete_journal_op, comp, NULL);

	section_index = pl_index;

	do {
		size_t this_step;
		struct page *src_pages[2];
		struct page *dst_page;

		while (unlikely(pl_index == section_index)) {
			unsigned dummy;
			if (likely(encrypt))
				rw_section_mac(ic, section, true);
			section++;
			n_sections--;
			if (!n_sections)
				break;
			page_list_location(ic, section, 0, &section_index, &dummy);
		}

		this_step = min(n_bytes, (size_t)PAGE_SIZE - pl_offset);
		dst_page = target_pl[pl_index].page;
		src_pages[0] = source_pl[pl_index].page;
		src_pages[1] = ic->journal_xor[pl_index].page;

		async_xor(dst_page, src_pages, pl_offset, 2, this_step, &submit);

		pl_index++;
		pl_offset = 0;
		n_bytes -= this_step;
	} while (n_bytes);

	BUG_ON(n_sections);

	async_tx_issue_pending_all();
}

static void complete_journal_encrypt(struct crypto_async_request *req, int err)
{
	struct journal_completion *comp = req->data;
	if (unlikely(err)) {
		if (likely(err == -EINPROGRESS)) {
			complete(&comp->ic->crypto_backoff);
			return;
		}
		dm_integrity_io_error(comp->ic, "asynchronous encrypt", err);
	}
	complete_journal_op(comp);
}

static bool do_crypt(bool encrypt, struct skcipher_request *req, struct journal_completion *comp)
{
	int r;
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				      complete_journal_encrypt, comp);
	if (likely(encrypt))
		r = crypto_skcipher_encrypt(req);
	else
		r = crypto_skcipher_decrypt(req);
	if (likely(!r))
		return false;
	if (likely(r == -EINPROGRESS))
		return true;
	if (likely(r == -EBUSY)) {
		wait_for_completion(&comp->ic->crypto_backoff);
		reinit_completion(&comp->ic->crypto_backoff);
		return true;
	}
	dm_integrity_io_error(comp->ic, "encrypt", r);
	return false;
}

static void crypt_journal(struct dm_integrity_c *ic, bool encrypt, unsigned section,
			  unsigned n_sections, struct journal_completion *comp)
{
	struct scatterlist **source_sg;
	struct scatterlist **target_sg;

	atomic_add(2, &comp->in_flight);

	if (likely(encrypt)) {
		source_sg = ic->journal_scatterlist;
		target_sg = ic->journal_io_scatterlist;
	} else {
		source_sg = ic->journal_io_scatterlist;
		target_sg = ic->journal_scatterlist;
	}

	do {
		struct skcipher_request *req;
		unsigned ivsize;
		char *iv;

		if (likely(encrypt))
			rw_section_mac(ic, section, true);

		req = ic->sk_requests[section];
		ivsize = crypto_skcipher_ivsize(ic->journal_crypt);
		iv = req->iv;

		memcpy(iv, iv + ivsize, ivsize);

		req->src = source_sg[section];
		req->dst = target_sg[section];

		if (unlikely(do_crypt(encrypt, req, comp)))
			atomic_inc(&comp->in_flight);

		section++;
		n_sections--;
	} while (n_sections);

	atomic_dec(&comp->in_flight);
	complete_journal_op(comp);
}

static void encrypt_journal(struct dm_integrity_c *ic, bool encrypt, unsigned section,
			    unsigned n_sections, struct journal_completion *comp)
{
	if (ic->journal_xor)
		return xor_journal(ic, encrypt, section, n_sections, comp);
	else
		return crypt_journal(ic, encrypt, section, n_sections, comp);
}

static void complete_journal_io(unsigned long error, void *context)
{
	struct journal_completion *comp = context;
	if (unlikely(error != 0))
		dm_integrity_io_error(comp->ic, "writing journal", -EIO);
	complete_journal_op(comp);
}

static void rw_journal(struct dm_integrity_c *ic, int op, int op_flags, unsigned section,
		       unsigned n_sections, struct journal_completion *comp)
{
	struct dm_io_request io_req;
	struct dm_io_region io_loc;
	unsigned sector, n_sectors, pl_index, pl_offset;
	int r;

	if (unlikely(dm_integrity_failed(ic))) {
		if (comp)
			complete_journal_io(-1UL, comp);
		return;
	}

	sector = section * ic->journal_section_sectors;
	n_sectors = n_sections * ic->journal_section_sectors;

	pl_index = sector >> (PAGE_SHIFT - SECTOR_SHIFT);
	pl_offset = (sector << SECTOR_SHIFT) & (PAGE_SIZE - 1);

	io_req.bi_op = op;
	io_req.bi_op_flags = op_flags;
	io_req.mem.type = DM_IO_PAGE_LIST;
	if (ic->journal_io)
		io_req.mem.ptr.pl = &ic->journal_io[pl_index];
	else
		io_req.mem.ptr.pl = &ic->journal[pl_index];
	io_req.mem.offset = pl_offset;
	if (likely(comp != NULL)) {
		io_req.notify.fn = complete_journal_io;
		io_req.notify.context = comp;
	} else {
		io_req.notify.fn = NULL;
	}
	io_req.client = ic->io;
	io_loc.bdev = ic->meta_dev ? ic->meta_dev->bdev : ic->dev->bdev;
	io_loc.sector = ic->start + SB_SECTORS + sector;
	io_loc.count = n_sectors;

	r = dm_io(&io_req, 1, &io_loc, NULL);
	if (unlikely(r)) {
		dm_integrity_io_error(ic, op == REQ_OP_READ ? "reading journal" : "writing journal", r);
		if (comp) {
			WARN_ONCE(1, "asynchronous dm_io failed: %d", r);
			complete_journal_io(-1UL, comp);
		}
	}
}

static void write_journal(struct dm_integrity_c *ic, unsigned commit_start, unsigned commit_sections)
{
	struct journal_completion io_comp;
	struct journal_completion crypt_comp_1;
	struct journal_completion crypt_comp_2;
	unsigned i;

	io_comp.ic = ic;
	init_completion(&io_comp.comp);

	if (commit_start + commit_sections <= ic->journal_sections) {
		io_comp.in_flight = (atomic_t)ATOMIC_INIT(1);
		if (ic->journal_io) {
			crypt_comp_1.ic = ic;
			init_completion(&crypt_comp_1.comp);
			crypt_comp_1.in_flight = (atomic_t)ATOMIC_INIT(0);
			encrypt_journal(ic, true, commit_start, commit_sections, &crypt_comp_1);
			wait_for_completion_io(&crypt_comp_1.comp);
		} else {
			for (i = 0; i < commit_sections; i++)
				rw_section_mac(ic, commit_start + i, true);
		}
		rw_journal(ic, REQ_OP_WRITE, REQ_FUA | REQ_SYNC, commit_start,
			   commit_sections, &io_comp);
	} else {
		unsigned to_end;
		io_comp.in_flight = (atomic_t)ATOMIC_INIT(2);
		to_end = ic->journal_sections - commit_start;
		if (ic->journal_io) {
			crypt_comp_1.ic = ic;
			init_completion(&crypt_comp_1.comp);
			crypt_comp_1.in_flight = (atomic_t)ATOMIC_INIT(0);
			encrypt_journal(ic, true, commit_start, to_end, &crypt_comp_1);
			if (try_wait_for_completion(&crypt_comp_1.comp)) {
				rw_journal(ic, REQ_OP_WRITE, REQ_FUA, commit_start, to_end, &io_comp);
				reinit_completion(&crypt_comp_1.comp);
				crypt_comp_1.in_flight = (atomic_t)ATOMIC_INIT(0);
				encrypt_journal(ic, true, 0, commit_sections - to_end, &crypt_comp_1);
				wait_for_completion_io(&crypt_comp_1.comp);
			} else {
				crypt_comp_2.ic = ic;
				init_completion(&crypt_comp_2.comp);
				crypt_comp_2.in_flight = (atomic_t)ATOMIC_INIT(0);
				encrypt_journal(ic, true, 0, commit_sections - to_end, &crypt_comp_2);
				wait_for_completion_io(&crypt_comp_1.comp);
				rw_journal(ic, REQ_OP_WRITE, REQ_FUA, commit_start, to_end, &io_comp);
				wait_for_completion_io(&crypt_comp_2.comp);
			}
		} else {
			for (i = 0; i < to_end; i++)
				rw_section_mac(ic, commit_start + i, true);
			rw_journal(ic, REQ_OP_WRITE, REQ_FUA, commit_start, to_end, &io_comp);
			for (i = 0; i < commit_sections - to_end; i++)
				rw_section_mac(ic, i, true);
		}
		rw_journal(ic, REQ_OP_WRITE, REQ_FUA, 0, commit_sections - to_end, &io_comp);
	}

	wait_for_completion_io(&io_comp.comp);
}

static void copy_from_journal(struct dm_integrity_c *ic, unsigned section, unsigned offset,
			      unsigned n_sectors, sector_t target, io_notify_fn fn, void *data)
{
	struct dm_io_request io_req;
	struct dm_io_region io_loc;
	int r;
	unsigned sector, pl_index, pl_offset;

	BUG_ON((target | n_sectors | offset) & (unsigned)(ic->sectors_per_block - 1));

	if (unlikely(dm_integrity_failed(ic))) {
		fn(-1UL, data);
		return;
	}

	sector = section * ic->journal_section_sectors + JOURNAL_BLOCK_SECTORS + offset;

	pl_index = sector >> (PAGE_SHIFT - SECTOR_SHIFT);
	pl_offset = (sector << SECTOR_SHIFT) & (PAGE_SIZE - 1);

	io_req.bi_op = REQ_OP_WRITE;
	io_req.bi_op_flags = 0;
	io_req.mem.type = DM_IO_PAGE_LIST;
	io_req.mem.ptr.pl = &ic->journal[pl_index];
	io_req.mem.offset = pl_offset;
	io_req.notify.fn = fn;
	io_req.notify.context = data;
	io_req.client = ic->io;
	io_loc.bdev = ic->dev->bdev;
	io_loc.sector = target;
	io_loc.count = n_sectors;

	r = dm_io(&io_req, 1, &io_loc, NULL);
	if (unlikely(r)) {
		WARN_ONCE(1, "asynchronous dm_io failed: %d", r);
		fn(-1UL, data);
	}
}

static bool ranges_overlap(struct dm_integrity_range *range1, struct dm_integrity_range *range2)
{
	return range1->logical_sector < range2->logical_sector + range2->n_sectors &&
	       range1->logical_sector + range1->n_sectors > range2->logical_sector;
}

static bool add_new_range(struct dm_integrity_c *ic, struct dm_integrity_range *new_range, bool check_waiting)
{
	struct rb_node **n = &ic->in_progress.rb_node;
	struct rb_node *parent;

	BUG_ON((new_range->logical_sector | new_range->n_sectors) & (unsigned)(ic->sectors_per_block - 1));

	if (likely(check_waiting)) {
		struct dm_integrity_range *range;
		list_for_each_entry(range, &ic->wait_list, wait_entry) {
			if (unlikely(ranges_overlap(range, new_range)))
				return false;
		}
	}

	parent = NULL;

	while (*n) {
		struct dm_integrity_range *range = container_of(*n, struct dm_integrity_range, node);

		parent = *n;
		if (new_range->logical_sector + new_range->n_sectors <= range->logical_sector) {
			n = &range->node.rb_left;
		} else if (new_range->logical_sector >= range->logical_sector + range->n_sectors) {
			n = &range->node.rb_right;
		} else {
			return false;
		}
	}

	rb_link_node(&new_range->node, parent, n);
	rb_insert_color(&new_range->node, &ic->in_progress);

	return true;
}

static void remove_range_unlocked(struct dm_integrity_c *ic, struct dm_integrity_range *range)
{
	rb_erase(&range->node, &ic->in_progress);
	while (unlikely(!list_empty(&ic->wait_list))) {
		struct dm_integrity_range *last_range =
			list_first_entry(&ic->wait_list, struct dm_integrity_range, wait_entry);
		struct task_struct *last_range_task;
		last_range_task = last_range->task;
		list_del(&last_range->wait_entry);
		if (!add_new_range(ic, last_range, false)) {
			last_range->task = last_range_task;
			list_add(&last_range->wait_entry, &ic->wait_list);
			break;
		}
		last_range->waiting = false;
		wake_up_process(last_range_task);
	}
}

static void remove_range(struct dm_integrity_c *ic, struct dm_integrity_range *range)
{
	unsigned long flags;

	spin_lock_irqsave(&ic->endio_wait.lock, flags);
	remove_range_unlocked(ic, range);
	spin_unlock_irqrestore(&ic->endio_wait.lock, flags);
}

static void wait_and_add_new_range(struct dm_integrity_c *ic, struct dm_integrity_range *new_range)
{
	new_range->waiting = true;
	list_add_tail(&new_range->wait_entry, &ic->wait_list);
	new_range->task = current;
	do {
		__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock_irq(&ic->endio_wait.lock);
		io_schedule();
		spin_lock_irq(&ic->endio_wait.lock);
	} while (unlikely(new_range->waiting));
}

static void init_journal_node(struct journal_node *node)
{
	RB_CLEAR_NODE(&node->node);
	node->sector = (sector_t)-1;
}

static void add_journal_node(struct dm_integrity_c *ic, struct journal_node *node, sector_t sector)
{
	struct rb_node **link;
	struct rb_node *parent;

	node->sector = sector;
	BUG_ON(!RB_EMPTY_NODE(&node->node));

	link = &ic->journal_tree_root.rb_node;
	parent = NULL;

	while (*link) {
		struct journal_node *j;
		parent = *link;
		j = container_of(parent, struct journal_node, node);
		if (sector < j->sector)
			link = &j->node.rb_left;
		else
			link = &j->node.rb_right;
	}

	rb_link_node(&node->node, parent, link);
	rb_insert_color(&node->node, &ic->journal_tree_root);
}

static void remove_journal_node(struct dm_integrity_c *ic, struct journal_node *node)
{
	BUG_ON(RB_EMPTY_NODE(&node->node));
	rb_erase(&node->node, &ic->journal_tree_root);
	init_journal_node(node);
}

#define NOT_FOUND	(-1U)

static unsigned find_journal_node(struct dm_integrity_c *ic, sector_t sector, sector_t *next_sector)
{
	struct rb_node *n = ic->journal_tree_root.rb_node;
	unsigned found = NOT_FOUND;
	*next_sector = (sector_t)-1;
	while (n) {
		struct journal_node *j = container_of(n, struct journal_node, node);
		if (sector == j->sector) {
			found = j - ic->journal_tree;
		}
		if (sector < j->sector) {
			*next_sector = j->sector;
			n = j->node.rb_left;
		} else {
			n = j->node.rb_right;
		}
	}

	return found;
}

static bool test_journal_node(struct dm_integrity_c *ic, unsigned pos, sector_t sector)
{
	struct journal_node *node, *next_node;
	struct rb_node *next;

	if (unlikely(pos >= ic->journal_entries))
		return false;
	node = &ic->journal_tree[pos];
	if (unlikely(RB_EMPTY_NODE(&node->node)))
		return false;
	if (unlikely(node->sector != sector))
		return false;

	next = rb_next(&node->node);
	if (unlikely(!next))
		return true;

	next_node = container_of(next, struct journal_node, node);
	return next_node->sector != sector;
}

static bool find_newer_committed_node(struct dm_integrity_c *ic, struct journal_node *node)
{
	struct rb_node *next;
	struct journal_node *next_node;
	unsigned next_section;

	BUG_ON(RB_EMPTY_NODE(&node->node));

	next = rb_next(&node->node);
	if (unlikely(!next))
		return false;

	next_node = container_of(next, struct journal_node, node);

	if (next_node->sector != node->sector)
		return false;

	next_section = (unsigned)(next_node - ic->journal_tree) / ic->journal_section_entries;
	if (next_section >= ic->committed_section &&
	    next_section < ic->committed_section + ic->n_committed_sections)
		return true;
	if (next_section + ic->journal_sections < ic->committed_section + ic->n_committed_sections)
		return true;

	return false;
}

#define TAG_READ	0
#define TAG_WRITE	1
#define TAG_CMP		2

static int dm_integrity_rw_tag(struct dm_integrity_c *ic, unsigned char *tag, sector_t *metadata_block,
			       unsigned *metadata_offset, unsigned total_size, int op)
{
	do {
		unsigned char *data, *dp;
		struct dm_buffer *b;
		unsigned to_copy;
		int r;

		r = dm_integrity_failed(ic);
		if (unlikely(r))
			return r;

		data = dm_bufio_read(ic->bufio, *metadata_block, &b);
		if (unlikely(IS_ERR(data)))
			return PTR_ERR(data);

		to_copy = min((1U << SECTOR_SHIFT << ic->log2_buffer_sectors) - *metadata_offset, total_size);
		dp = data + *metadata_offset;
		if (op == TAG_READ) {
			memcpy(tag, dp, to_copy);
		} else if (op == TAG_WRITE) {
			memcpy(dp, tag, to_copy);
			dm_bufio_mark_partial_buffer_dirty(b, *metadata_offset, *metadata_offset + to_copy);
		} else  {
			/* e.g.: op == TAG_CMP */
			if (unlikely(memcmp(dp, tag, to_copy))) {
				unsigned i;

				for (i = 0; i < to_copy; i++) {
					if (dp[i] != tag[i])
						break;
					total_size--;
				}
				dm_bufio_release(b);
				return total_size;
			}
		}
		dm_bufio_release(b);

		tag += to_copy;
		*metadata_offset += to_copy;
		if (unlikely(*metadata_offset == 1U << SECTOR_SHIFT << ic->log2_buffer_sectors)) {
			(*metadata_block)++;
			*metadata_offset = 0;
		}
		total_size -= to_copy;
	} while (unlikely(total_size));

	return 0;
}

static void dm_integrity_flush_buffers(struct dm_integrity_c *ic)
{
	int r;
	r = dm_bufio_write_dirty_buffers(ic->bufio);
	if (unlikely(r))
		dm_integrity_io_error(ic, "writing tags", r);
}

static void sleep_on_endio_wait(struct dm_integrity_c *ic)
{
	DECLARE_WAITQUEUE(wait, current);
	__add_wait_queue(&ic->endio_wait, &wait);
	__set_current_state(TASK_UNINTERRUPTIBLE);
	spin_unlock_irq(&ic->endio_wait.lock);
	io_schedule();
	spin_lock_irq(&ic->endio_wait.lock);
	__remove_wait_queue(&ic->endio_wait, &wait);
}

static void autocommit_fn(struct timer_list *t)
{
	struct dm_integrity_c *ic = from_timer(ic, t, autocommit_timer);

	if (likely(!dm_integrity_failed(ic)))
		queue_work(ic->commit_wq, &ic->commit_work);
}

static void schedule_autocommit(struct dm_integrity_c *ic)
{
	if (!timer_pending(&ic->autocommit_timer))
		mod_timer(&ic->autocommit_timer, jiffies + ic->autocommit_jiffies);
}

static void submit_flush_bio(struct dm_integrity_c *ic, struct dm_integrity_io *dio)
{
	struct bio *bio;
	unsigned long flags;

	spin_lock_irqsave(&ic->endio_wait.lock, flags);
	bio = dm_bio_from_per_bio_data(dio, sizeof(struct dm_integrity_io));
	bio_list_add(&ic->flush_bio_list, bio);
	spin_unlock_irqrestore(&ic->endio_wait.lock, flags);

	queue_work(ic->commit_wq, &ic->commit_work);
}

static void do_endio(struct dm_integrity_c *ic, struct bio *bio)
{
	int r = dm_integrity_failed(ic);
	if (unlikely(r) && !bio->bi_status)
		bio->bi_status = errno_to_blk_status(r);
	bio_endio(bio);
}

static void do_endio_flush(struct dm_integrity_c *ic, struct dm_integrity_io *dio)
{
	struct bio *bio = dm_bio_from_per_bio_data(dio, sizeof(struct dm_integrity_io));

	if (unlikely(dio->fua) && likely(!bio->bi_status) && likely(!dm_integrity_failed(ic)))
		submit_flush_bio(ic, dio);
	else
		do_endio(ic, bio);
}

static void dec_in_flight(struct dm_integrity_io *dio)
{
	if (atomic_dec_and_test(&dio->in_flight)) {
		struct dm_integrity_c *ic = dio->ic;
		struct bio *bio;

		remove_range(ic, &dio->range);

		if (unlikely(dio->write))
			schedule_autocommit(ic);

		bio = dm_bio_from_per_bio_data(dio, sizeof(struct dm_integrity_io));

		if (unlikely(dio->bi_status) && !bio->bi_status)
			bio->bi_status = dio->bi_status;
		if (likely(!bio->bi_status) && unlikely(bio_sectors(bio) != dio->range.n_sectors)) {
			dio->range.logical_sector += dio->range.n_sectors;
			bio_advance(bio, dio->range.n_sectors << SECTOR_SHIFT);
			INIT_WORK(&dio->work, integrity_bio_wait);
			queue_work(ic->wait_wq, &dio->work);
			return;
		}
		do_endio_flush(ic, dio);
	}
}

static void integrity_end_io(struct bio *bio)
{
	struct dm_integrity_io *dio = dm_per_bio_data(bio, sizeof(struct dm_integrity_io));

	bio->bi_iter = dio->orig_bi_iter;
	bio->bi_disk = dio->orig_bi_disk;
	bio->bi_partno = dio->orig_bi_partno;
	if (dio->orig_bi_integrity) {
		bio->bi_integrity = dio->orig_bi_integrity;
		bio->bi_opf |= REQ_INTEGRITY;
	}
	bio->bi_end_io = dio->orig_bi_end_io;

	if (dio->completion)
		complete(dio->completion);

	dec_in_flight(dio);
}

static void integrity_sector_checksum(struct dm_integrity_c *ic, sector_t sector,
				      const char *data, char *result)
{
	__u64 sector_le = cpu_to_le64(sector);
	SHASH_DESC_ON_STACK(req, ic->internal_hash);
	int r;
	unsigned digest_size;

	req->tfm = ic->internal_hash;
	req->flags = 0;

	r = crypto_shash_init(req);
	if (unlikely(r < 0)) {
		dm_integrity_io_error(ic, "crypto_shash_init", r);
		goto failed;
	}

	r = crypto_shash_update(req, (const __u8 *)&sector_le, sizeof sector_le);
	if (unlikely(r < 0)) {
		dm_integrity_io_error(ic, "crypto_shash_update", r);
		goto failed;
	}

	r = crypto_shash_update(req, data, ic->sectors_per_block << SECTOR_SHIFT);
	if (unlikely(r < 0)) {
		dm_integrity_io_error(ic, "crypto_shash_update", r);
		goto failed;
	}

	r = crypto_shash_final(req, result);
	if (unlikely(r < 0)) {
		dm_integrity_io_error(ic, "crypto_shash_final", r);
		goto failed;
	}

	digest_size = crypto_shash_digestsize(ic->internal_hash);
	if (unlikely(digest_size < ic->tag_size))
		memset(result + digest_size, 0, ic->tag_size - digest_size);

	return;

failed:
	/* this shouldn't happen anyway, the hash functions have no reason to fail */
	get_random_bytes(result, ic->tag_size);
}

static void integrity_metadata(struct work_struct *w)
{
	struct dm_integrity_io *dio = container_of(w, struct dm_integrity_io, work);
	struct dm_integrity_c *ic = dio->ic;

	int r;

	if (ic->internal_hash) {
		struct bvec_iter iter;
		struct bio_vec bv;
		unsigned digest_size = crypto_shash_digestsize(ic->internal_hash);
		struct bio *bio = dm_bio_from_per_bio_data(dio, sizeof(struct dm_integrity_io));
		char *checksums;
		unsigned extra_space = unlikely(digest_size > ic->tag_size) ? digest_size - ic->tag_size : 0;
		char checksums_onstack[ic->tag_size + extra_space];
		unsigned sectors_to_process = dio->range.n_sectors;
		sector_t sector = dio->range.logical_sector;

		if (unlikely(ic->mode == 'R'))
			goto skip_io;

		checksums = kmalloc((PAGE_SIZE >> SECTOR_SHIFT >> ic->sb->log2_sectors_per_block) * ic->tag_size + extra_space,
				    GFP_NOIO | __GFP_NORETRY | __GFP_NOWARN);
		if (!checksums)
			checksums = checksums_onstack;

		__bio_for_each_segment(bv, bio, iter, dio->orig_bi_iter) {
			unsigned pos;
			char *mem, *checksums_ptr;

again:
			mem = (char *)kmap_atomic(bv.bv_page) + bv.bv_offset;
			pos = 0;
			checksums_ptr = checksums;
			do {
				integrity_sector_checksum(ic, sector, mem + pos, checksums_ptr);
				checksums_ptr += ic->tag_size;
				sectors_to_process -= ic->sectors_per_block;
				pos += ic->sectors_per_block << SECTOR_SHIFT;
				sector += ic->sectors_per_block;
			} while (pos < bv.bv_len && sectors_to_process && checksums != checksums_onstack);
			kunmap_atomic(mem);

			r = dm_integrity_rw_tag(ic, checksums, &dio->metadata_block, &dio->metadata_offset,
						checksums_ptr - checksums, !dio->write ? TAG_CMP : TAG_WRITE);
			if (unlikely(r)) {
				if (r > 0) {
					DMERR_LIMIT("Checksum failed at sector 0x%llx",
						    (unsigned long long)(sector - ((r + ic->tag_size - 1) / ic->tag_size)));
					r = -EILSEQ;
					atomic64_inc(&ic->number_of_mismatches);
				}
				if (likely(checksums != checksums_onstack))
					kfree(checksums);
				goto error;
			}

			if (!sectors_to_process)
				break;

			if (unlikely(pos < bv.bv_len)) {
				bv.bv_offset += pos;
				bv.bv_len -= pos;
				goto again;
			}
		}

		if (likely(checksums != checksums_onstack))
			kfree(checksums);
	} else {
		struct bio_integrity_payload *bip = dio->orig_bi_integrity;

		if (bip) {
			struct bio_vec biv;
			struct bvec_iter iter;
			unsigned data_to_process = dio->range.n_sectors;
			sector_to_block(ic, data_to_process);
			data_to_process *= ic->tag_size;

			bip_for_each_vec(biv, bip, iter) {
				unsigned char *tag;
				unsigned this_len;

				BUG_ON(PageHighMem(biv.bv_page));
				tag = lowmem_page_address(biv.bv_page) + biv.bv_offset;
				this_len = min(biv.bv_len, data_to_process);
				r = dm_integrity_rw_tag(ic, tag, &dio->metadata_block, &dio->metadata_offset,
							this_len, !dio->write ? TAG_READ : TAG_WRITE);
				if (unlikely(r))
					goto error;
				data_to_process -= this_len;
				if (!data_to_process)
					break;
			}
		}
	}
skip_io:
	dec_in_flight(dio);
	return;
error:
	dio->bi_status = errno_to_blk_status(r);
	dec_in_flight(dio);
}

static int dm_integrity_map(struct dm_target *ti, struct bio *bio)
{
	struct dm_integrity_c *ic = ti->private;
	struct dm_integrity_io *dio = dm_per_bio_data(bio, sizeof(struct dm_integrity_io));
	struct bio_integrity_payload *bip;

	sector_t area, offset;

	dio->ic = ic;
	dio->bi_status = 0;

	if (unlikely(bio->bi_opf & REQ_PREFLUSH)) {
		submit_flush_bio(ic, dio);
		return DM_MAPIO_SUBMITTED;
	}

	dio->range.logical_sector = dm_target_offset(ti, bio->bi_iter.bi_sector);
	dio->write = bio_op(bio) == REQ_OP_WRITE;
	dio->fua = dio->write && bio->bi_opf & REQ_FUA;
	if (unlikely(dio->fua)) {
		/*
		 * Don't pass down the FUA flag because we have to flush
		 * disk cache anyway.
		 */
		bio->bi_opf &= ~REQ_FUA;
	}
	if (unlikely(dio->range.logical_sector + bio_sectors(bio) > ic->provided_data_sectors)) {
		DMERR("Too big sector number: 0x%llx + 0x%x > 0x%llx",
		      (unsigned long long)dio->range.logical_sector, bio_sectors(bio),
		      (unsigned long long)ic->provided_data_sectors);
		return DM_MAPIO_KILL;
	}
	if (unlikely((dio->range.logical_sector | bio_sectors(bio)) & (unsigned)(ic->sectors_per_block - 1))) {
		DMERR("Bio not aligned on %u sectors: 0x%llx, 0x%x",
		      ic->sectors_per_block,
		      (unsigned long long)dio->range.logical_sector, bio_sectors(bio));
		return DM_MAPIO_KILL;
	}

	if (ic->sectors_per_block > 1) {
		struct bvec_iter iter;
		struct bio_vec bv;
		bio_for_each_segment(bv, bio, iter) {
			if (unlikely(bv.bv_len & ((ic->sectors_per_block << SECTOR_SHIFT) - 1))) {
				DMERR("Bio vector (%u,%u) is not aligned on %u-sector boundary",
					bv.bv_offset, bv.bv_len, ic->sectors_per_block);
				return DM_MAPIO_KILL;
			}
		}
	}

	bip = bio_integrity(bio);
	if (!ic->internal_hash) {
		if (bip) {
			unsigned wanted_tag_size = bio_sectors(bio) >> ic->sb->log2_sectors_per_block;
			if (ic->log2_tag_size >= 0)
				wanted_tag_size <<= ic->log2_tag_size;
			else
				wanted_tag_size *= ic->tag_size;
			if (unlikely(wanted_tag_size != bip->bip_iter.bi_size)) {
				DMERR("Invalid integrity data size %u, expected %u", bip->bip_iter.bi_size, wanted_tag_size);
				return DM_MAPIO_KILL;
			}
		}
	} else {
		if (unlikely(bip != NULL)) {
			DMERR("Unexpected integrity data when using internal hash");
			return DM_MAPIO_KILL;
		}
	}

	if (unlikely(ic->mode == 'R') && unlikely(dio->write))
		return DM_MAPIO_KILL;

	get_area_and_offset(ic, dio->range.logical_sector, &area, &offset);
	dio->metadata_block = get_metadata_sector_and_offset(ic, area, offset, &dio->metadata_offset);
	bio->bi_iter.bi_sector = get_data_sector(ic, area, offset);

	dm_integrity_map_continue(dio, true);
	return DM_MAPIO_SUBMITTED;
}

static bool __journal_read_write(struct dm_integrity_io *dio, struct bio *bio,
				 unsigned journal_section, unsigned journal_entry)
{
	struct dm_integrity_c *ic = dio->ic;
	sector_t logical_sector;
	unsigned n_sectors;

	logical_sector = dio->range.logical_sector;
	n_sectors = dio->range.n_sectors;
	do {
		struct bio_vec bv = bio_iovec(bio);
		char *mem;

		if (unlikely(bv.bv_len >> SECTOR_SHIFT > n_sectors))
			bv.bv_len = n_sectors << SECTOR_SHIFT;
		n_sectors -= bv.bv_len >> SECTOR_SHIFT;
		bio_advance_iter(bio, &bio->bi_iter, bv.bv_len);
retry_kmap:
		mem = kmap_atomic(bv.bv_page);
		if (likely(dio->write))
			flush_dcache_page(bv.bv_page);

		do {
			struct journal_entry *je = access_journal_entry(ic, journal_section, journal_entry);

			if (unlikely(!dio->write)) {
				struct journal_sector *js;
				char *mem_ptr;
				unsigned s;

				if (unlikely(journal_entry_is_inprogress(je))) {
					flush_dcache_page(bv.bv_page);
					kunmap_atomic(mem);

					__io_wait_event(ic->copy_to_journal_wait, !journal_entry_is_inprogress(je));
					goto retry_kmap;
				}
				smp_rmb();
				BUG_ON(journal_entry_get_sector(je) != logical_sector);
				js = access_journal_data(ic, journal_section, journal_entry);
				mem_ptr = mem + bv.bv_offset;
				s = 0;
				do {
					memcpy(mem_ptr, js, JOURNAL_SECTOR_DATA);
					*(commit_id_t *)(mem_ptr + JOURNAL_SECTOR_DATA) = je->last_bytes[s];
					js++;
					mem_ptr += 1 << SECTOR_SHIFT;
				} while (++s < ic->sectors_per_block);
#ifdef INTERNAL_VERIFY
				if (ic->internal_hash) {
					char checksums_onstack[max(crypto_shash_digestsize(ic->internal_hash), ic->tag_size)];

					integrity_sector_checksum(ic, logical_sector, mem + bv.bv_offset, checksums_onstack);
					if (unlikely(memcmp(checksums_onstack, journal_entry_tag(ic, je), ic->tag_size))) {
						DMERR_LIMIT("Checksum failed when reading from journal, at sector 0x%llx",
							    (unsigned long long)logical_sector);
					}
				}
#endif
			}

			if (!ic->internal_hash) {
				struct bio_integrity_payload *bip = bio_integrity(bio);
				unsigned tag_todo = ic->tag_size;
				char *tag_ptr = journal_entry_tag(ic, je);

				if (bip) do {
					struct bio_vec biv = bvec_iter_bvec(bip->bip_vec, bip->bip_iter);
					unsigned tag_now = min(biv.bv_len, tag_todo);
					char *tag_addr;
					BUG_ON(PageHighMem(biv.bv_page));
					tag_addr = lowmem_page_address(biv.bv_page) + biv.bv_offset;
					if (likely(dio->write))
						memcpy(tag_ptr, tag_addr, tag_now);
					else
						memcpy(tag_addr, tag_ptr, tag_now);
					bvec_iter_advance(bip->bip_vec, &bip->bip_iter, tag_now);
					tag_ptr += tag_now;
					tag_todo -= tag_now;
				} while (unlikely(tag_todo)); else {
					if (likely(dio->write))
						memset(tag_ptr, 0, tag_todo);
				}
			}

			if (likely(dio->write)) {
				struct journal_sector *js;
				unsigned s;

				js = access_journal_data(ic, journal_section, journal_entry);
				memcpy(js, mem + bv.bv_offset, ic->sectors_per_block << SECTOR_SHIFT);

				s = 0;
				do {
					je->last_bytes[s] = js[s].commit_id;
				} while (++s < ic->sectors_per_block);

				if (ic->internal_hash) {
					unsigned digest_size = crypto_shash_digestsize(ic->internal_hash);
					if (unlikely(digest_size > ic->tag_size)) {
						char checksums_onstack[digest_size];
						integrity_sector_checksum(ic, logical_sector, (char *)js, checksums_onstack);
						memcpy(journal_entry_tag(ic, je), checksums_onstack, ic->tag_size);
					} else
						integrity_sector_checksum(ic, logical_sector, (char *)js, journal_entry_tag(ic, je));
				}

				journal_entry_set_sector(je, logical_sector);
			}
			logical_sector += ic->sectors_per_block;

			journal_entry++;
			if (unlikely(journal_entry == ic->journal_section_entries)) {
				journal_entry = 0;
				journal_section++;
				wraparound_section(ic, &journal_section);
			}

			bv.bv_offset += ic->sectors_per_block << SECTOR_SHIFT;
		} while (bv.bv_len -= ic->sectors_per_block << SECTOR_SHIFT);

		if (unlikely(!dio->write))
			flush_dcache_page(bv.bv_page);
		kunmap_atomic(mem);
	} while (n_sectors);

	if (likely(dio->write)) {
		smp_mb();
		if (unlikely(waitqueue_active(&ic->copy_to_journal_wait)))
			wake_up(&ic->copy_to_journal_wait);
		if (READ_ONCE(ic->free_sectors) <= ic->free_sectors_threshold) {
			queue_work(ic->commit_wq, &ic->commit_work);
		} else {
			schedule_autocommit(ic);
		}
	} else {
		remove_range(ic, &dio->range);
	}

	if (unlikely(bio->bi_iter.bi_size)) {
		sector_t area, offset;

		dio->range.logical_sector = logical_sector;
		get_area_and_offset(ic, dio->range.logical_sector, &area, &offset);
		dio->metadata_block = get_metadata_sector_and_offset(ic, area, offset, &dio->metadata_offset);
		return true;
	}

	return false;
}

static void dm_integrity_map_continue(struct dm_integrity_io *dio, bool from_map)
{
	struct dm_integrity_c *ic = dio->ic;
	struct bio *bio = dm_bio_from_per_bio_data(dio, sizeof(struct dm_integrity_io));
	unsigned journal_section, journal_entry;
	unsigned journal_read_pos;
	struct completion read_comp;
	bool need_sync_io = ic->internal_hash && !dio->write;

	if (need_sync_io && from_map) {
		INIT_WORK(&dio->work, integrity_bio_wait);
		queue_work(ic->metadata_wq, &dio->work);
		return;
	}

lock_retry:
	spin_lock_irq(&ic->endio_wait.lock);
retry:
	if (unlikely(dm_integrity_failed(ic))) {
		spin_unlock_irq(&ic->endio_wait.lock);
		do_endio(ic, bio);
		return;
	}
	dio->range.n_sectors = bio_sectors(bio);
	journal_read_pos = NOT_FOUND;
	if (likely(ic->mode == 'J')) {
		if (dio->write) {
			unsigned next_entry, i, pos;
			unsigned ws, we, range_sectors;

			dio->range.n_sectors = min(dio->range.n_sectors,
						   ic->free_sectors << ic->sb->log2_sectors_per_block);
			if (unlikely(!dio->range.n_sectors)) {
				if (from_map)
					goto offload_to_thread;
				sleep_on_endio_wait(ic);
				goto retry;
			}
			range_sectors = dio->range.n_sectors >> ic->sb->log2_sectors_per_block;
			ic->free_sectors -= range_sectors;
			journal_section = ic->free_section;
			journal_entry = ic->free_section_entry;

			next_entry = ic->free_section_entry + range_sectors;
			ic->free_section_entry = next_entry % ic->journal_section_entries;
			ic->free_section += next_entry / ic->journal_section_entries;
			ic->n_uncommitted_sections += next_entry / ic->journal_section_entries;
			wraparound_section(ic, &ic->free_section);

			pos = journal_section * ic->journal_section_entries + journal_entry;
			ws = journal_section;
			we = journal_entry;
			i = 0;
			do {
				struct journal_entry *je;

				add_journal_node(ic, &ic->journal_tree[pos], dio->range.logical_sector + i);
				pos++;
				if (unlikely(pos >= ic->journal_entries))
					pos = 0;

				je = access_journal_entry(ic, ws, we);
				BUG_ON(!journal_entry_is_unused(je));
				journal_entry_set_inprogress(je);
				we++;
				if (unlikely(we == ic->journal_section_entries)) {
					we = 0;
					ws++;
					wraparound_section(ic, &ws);
				}
			} while ((i += ic->sectors_per_block) < dio->range.n_sectors);

			spin_unlock_irq(&ic->endio_wait.lock);
			goto journal_read_write;
		} else {
			sector_t next_sector;
			journal_read_pos = find_journal_node(ic, dio->range.logical_sector, &next_sector);
			if (likely(journal_read_pos == NOT_FOUND)) {
				if (unlikely(dio->range.n_sectors > next_sector - dio->range.logical_sector))
					dio->range.n_sectors = next_sector - dio->range.logical_sector;
			} else {
				unsigned i;
				unsigned jp = journal_read_pos + 1;
				for (i = ic->sectors_per_block; i < dio->range.n_sectors; i += ic->sectors_per_block, jp++) {
					if (!test_journal_node(ic, jp, dio->range.logical_sector + i))
						break;
				}
				dio->range.n_sectors = i;
			}
		}
	}
	if (unlikely(!add_new_range(ic, &dio->range, true))) {
		/*
		 * We must not sleep in the request routine because it could
		 * stall bios on current->bio_list.
		 * So, we offload the bio to a workqueue if we have to sleep.
		 */
		if (from_map) {
offload_to_thread:
			spin_unlock_irq(&ic->endio_wait.lock);
			INIT_WORK(&dio->work, integrity_bio_wait);
			queue_work(ic->wait_wq, &dio->work);
			return;
		}
		if (journal_read_pos != NOT_FOUND)
			dio->range.n_sectors = ic->sectors_per_block;
		wait_and_add_new_range(ic, &dio->range);
		/*
		 * wait_and_add_new_range drops the spinlock, so the journal
		 * may have been changed arbitrarily. We need to recheck.
		 * To simplify the code, we restrict I/O size to just one block.
		 */
		if (journal_read_pos != NOT_FOUND) {
			sector_t next_sector;
			unsigned new_pos = find_journal_node(ic, dio->range.logical_sector, &next_sector);
			if (unlikely(new_pos != journal_read_pos)) {
				remove_range_unlocked(ic, &dio->range);
				goto retry;
			}
		}
	}
	spin_unlock_irq(&ic->endio_wait.lock);

	if (unlikely(journal_read_pos != NOT_FOUND)) {
		journal_section = journal_read_pos / ic->journal_section_entries;
		journal_entry = journal_read_pos % ic->journal_section_entries;
		goto journal_read_write;
	}

	dio->in_flight = (atomic_t)ATOMIC_INIT(2);

	if (need_sync_io) {
		init_completion(&read_comp);
		dio->completion = &read_comp;
	} else
		dio->completion = NULL;

	dio->orig_bi_iter = bio->bi_iter;

	dio->orig_bi_disk = bio->bi_disk;
	dio->orig_bi_partno = bio->bi_partno;
	bio_set_dev(bio, ic->dev->bdev);

	dio->orig_bi_integrity = bio_integrity(bio);
	bio->bi_integrity = NULL;
	bio->bi_opf &= ~REQ_INTEGRITY;

	dio->orig_bi_end_io = bio->bi_end_io;
	bio->bi_end_io = integrity_end_io;

	bio->bi_iter.bi_size = dio->range.n_sectors << SECTOR_SHIFT;
	generic_make_request(bio);

	if (need_sync_io) {
		wait_for_completion_io(&read_comp);
		if (unlikely(ic->recalc_wq != NULL) &&
		    ic->sb->flags & cpu_to_le32(SB_FLAG_RECALCULATING) &&
		    dio->range.logical_sector + dio->range.n_sectors > le64_to_cpu(ic->sb->recalc_sector))
			goto skip_check;
		if (likely(!bio->bi_status))
			integrity_metadata(&dio->work);
		else
skip_check:
			dec_in_flight(dio);

	} else {
		INIT_WORK(&dio->work, integrity_metadata);
		queue_work(ic->metadata_wq, &dio->work);
	}

	return;

journal_read_write:
	if (unlikely(__journal_read_write(dio, bio, journal_section, journal_entry)))
		goto lock_retry;

	do_endio_flush(ic, dio);
}


static void integrity_bio_wait(struct work_struct *w)
{
	struct dm_integrity_io *dio = container_of(w, struct dm_integrity_io, work);

	dm_integrity_map_continue(dio, false);
}

static void pad_uncommitted(struct dm_integrity_c *ic)
{
	if (ic->free_section_entry) {
		ic->free_sectors -= ic->journal_section_entries - ic->free_section_entry;
		ic->free_section_entry = 0;
		ic->free_section++;
		wraparound_section(ic, &ic->free_section);
		ic->n_uncommitted_sections++;
	}
	WARN_ON(ic->journal_sections * ic->journal_section_entries !=
		(ic->n_uncommitted_sections + ic->n_committed_sections) * ic->journal_section_entries + ic->free_sectors);
}

static void integrity_commit(struct work_struct *w)
{
	struct dm_integrity_c *ic = container_of(w, struct dm_integrity_c, commit_work);
	unsigned commit_start, commit_sections;
	unsigned i, j, n;
	struct bio *flushes;

	del_timer(&ic->autocommit_timer);

	spin_lock_irq(&ic->endio_wait.lock);
	flushes = bio_list_get(&ic->flush_bio_list);
	if (unlikely(ic->mode != 'J')) {
		spin_unlock_irq(&ic->endio_wait.lock);
		dm_integrity_flush_buffers(ic);
		goto release_flush_bios;
	}

	pad_uncommitted(ic);
	commit_start = ic->uncommitted_section;
	commit_sections = ic->n_uncommitted_sections;
	spin_unlock_irq(&ic->endio_wait.lock);

	if (!commit_sections)
		goto release_flush_bios;

	i = commit_start;
	for (n = 0; n < commit_sections; n++) {
		for (j = 0; j < ic->journal_section_entries; j++) {
			struct journal_entry *je;
			je = access_journal_entry(ic, i, j);
			io_wait_event(ic->copy_to_journal_wait, !journal_entry_is_inprogress(je));
		}
		for (j = 0; j < ic->journal_section_sectors; j++) {
			struct journal_sector *js;
			js = access_journal(ic, i, j);
			js->commit_id = dm_integrity_commit_id(ic, i, j, ic->commit_seq);
		}
		i++;
		if (unlikely(i >= ic->journal_sections))
			ic->commit_seq = next_commit_seq(ic->commit_seq);
		wraparound_section(ic, &i);
	}
	smp_rmb();

	write_journal(ic, commit_start, commit_sections);

	spin_lock_irq(&ic->endio_wait.lock);
	ic->uncommitted_section += commit_sections;
	wraparound_section(ic, &ic->uncommitted_section);
	ic->n_uncommitted_sections -= commit_sections;
	ic->n_committed_sections += commit_sections;
	spin_unlock_irq(&ic->endio_wait.lock);

	if (READ_ONCE(ic->free_sectors) <= ic->free_sectors_threshold)
		queue_work(ic->writer_wq, &ic->writer_work);

release_flush_bios:
	while (flushes) {
		struct bio *next = flushes->bi_next;
		flushes->bi_next = NULL;
		do_endio(ic, flushes);
		flushes = next;
	}
}

static void complete_copy_from_journal(unsigned long error, void *context)
{
	struct journal_io *io = context;
	struct journal_completion *comp = io->comp;
	struct dm_integrity_c *ic = comp->ic;
	remove_range(ic, &io->range);
	mempool_free(io, &ic->journal_io_mempool);
	if (unlikely(error != 0))
		dm_integrity_io_error(ic, "copying from journal", -EIO);
	complete_journal_op(comp);
}

static void restore_last_bytes(struct dm_integrity_c *ic, struct journal_sector *js,
			       struct journal_entry *je)
{
	unsigned s = 0;
	do {
		js->commit_id = je->last_bytes[s];
		js++;
	} while (++s < ic->sectors_per_block);
}

static void do_journal_write(struct dm_integrity_c *ic, unsigned write_start,
			     unsigned write_sections, bool from_replay)
{
	unsigned i, j, n;
	struct journal_completion comp;
	struct blk_plug plug;

	blk_start_plug(&plug);

	comp.ic = ic;
	comp.in_flight = (atomic_t)ATOMIC_INIT(1);
	init_completion(&comp.comp);

	i = write_start;
	for (n = 0; n < write_sections; n++, i++, wraparound_section(ic, &i)) {
#ifndef INTERNAL_VERIFY
		if (unlikely(from_replay))
#endif
			rw_section_mac(ic, i, false);
		for (j = 0; j < ic->journal_section_entries; j++) {
			struct journal_entry *je = access_journal_entry(ic, i, j);
			sector_t sec, area, offset;
			unsigned k, l, next_loop;
			sector_t metadata_block;
			unsigned metadata_offset;
			struct journal_io *io;

			if (journal_entry_is_unused(je))
				continue;
			BUG_ON(unlikely(journal_entry_is_inprogress(je)) && !from_replay);
			sec = journal_entry_get_sector(je);
			if (unlikely(from_replay)) {
				if (unlikely(sec & (unsigned)(ic->sectors_per_block - 1))) {
					dm_integrity_io_error(ic, "invalid sector in journal", -EIO);
					sec &= ~(sector_t)(ic->sectors_per_block - 1);
				}
			}
			get_area_and_offset(ic, sec, &area, &offset);
			restore_last_bytes(ic, access_journal_data(ic, i, j), je);
			for (k = j + 1; k < ic->journal_section_entries; k++) {
				struct journal_entry *je2 = access_journal_entry(ic, i, k);
				sector_t sec2, area2, offset2;
				if (journal_entry_is_unused(je2))
					break;
				BUG_ON(unlikely(journal_entry_is_inprogress(je2)) && !from_replay);
				sec2 = journal_entry_get_sector(je2);
				get_area_and_offset(ic, sec2, &area2, &offset2);
				if (area2 != area || offset2 != offset + ((k - j) << ic->sb->log2_sectors_per_block))
					break;
				restore_last_bytes(ic, access_journal_data(ic, i, k), je2);
			}
			next_loop = k - 1;

			io = mempool_alloc(&ic->journal_io_mempool, GFP_NOIO);
			io->comp = &comp;
			io->range.logical_sector = sec;
			io->range.n_sectors = (k - j) << ic->sb->log2_sectors_per_block;

			spin_lock_irq(&ic->endio_wait.lock);
			if (unlikely(!add_new_range(ic, &io->range, true)))
				wait_and_add_new_range(ic, &io->range);

			if (likely(!from_replay)) {
				struct journal_node *section_node = &ic->journal_tree[i * ic->journal_section_entries];

				/* don't write if there is newer committed sector */
				while (j < k && find_newer_committed_node(ic, &section_node[j])) {
					struct journal_entry *je2 = access_journal_entry(ic, i, j);

					journal_entry_set_unused(je2);
					remove_journal_node(ic, &section_node[j]);
					j++;
					sec += ic->sectors_per_block;
					offset += ic->sectors_per_block;
				}
				while (j < k && find_newer_committed_node(ic, &section_node[k - 1])) {
					struct journal_entry *je2 = access_journal_entry(ic, i, k - 1);

					journal_entry_set_unused(je2);
					remove_journal_node(ic, &section_node[k - 1]);
					k--;
				}
				if (j == k) {
					remove_range_unlocked(ic, &io->range);
					spin_unlock_irq(&ic->endio_wait.lock);
					mempool_free(io, &ic->journal_io_mempool);
					goto skip_io;
				}
				for (l = j; l < k; l++) {
					remove_journal_node(ic, &section_node[l]);
				}
			}
			spin_unlock_irq(&ic->endio_wait.lock);

			metadata_block = get_metadata_sector_and_offset(ic, area, offset, &metadata_offset);
			for (l = j; l < k; l++) {
				int r;
				struct journal_entry *je2 = access_journal_entry(ic, i, l);

				if (
#ifndef INTERNAL_VERIFY
				    unlikely(from_replay) &&
#endif
				    ic->internal_hash) {
					char test_tag[max(crypto_shash_digestsize(ic->internal_hash), ic->tag_size)];

					integrity_sector_checksum(ic, sec + ((l - j) << ic->sb->log2_sectors_per_block),
								  (char *)access_journal_data(ic, i, l), test_tag);
					if (unlikely(memcmp(test_tag, journal_entry_tag(ic, je2), ic->tag_size)))
						dm_integrity_io_error(ic, "tag mismatch when replaying journal", -EILSEQ);
				}

				journal_entry_set_unused(je2);
				r = dm_integrity_rw_tag(ic, journal_entry_tag(ic, je2), &metadata_block, &metadata_offset,
							ic->tag_size, TAG_WRITE);
				if (unlikely(r)) {
					dm_integrity_io_error(ic, "reading tags", r);
				}
			}

			atomic_inc(&comp.in_flight);
			copy_from_journal(ic, i, j << ic->sb->log2_sectors_per_block,
					  (k - j) << ic->sb->log2_sectors_per_block,
					  get_data_sector(ic, area, offset),
					  complete_copy_from_journal, io);
skip_io:
			j = next_loop;
		}
	}

	dm_bufio_write_dirty_buffers_async(ic->bufio);

	blk_finish_plug(&plug);

	complete_journal_op(&comp);
	wait_for_completion_io(&comp.comp);

	dm_integrity_flush_buffers(ic);
}

static void integrity_writer(struct work_struct *w)
{
	struct dm_integrity_c *ic = container_of(w, struct dm_integrity_c, writer_work);
	unsigned write_start, write_sections;

	unsigned prev_free_sectors;

	/* the following test is not needed, but it tests the replay code */
	if (unlikely(dm_suspended(ic->ti)) && !ic->meta_dev)
		return;

	spin_lock_irq(&ic->endio_wait.lock);
	write_start = ic->committed_section;
	write_sections = ic->n_committed_sections;
	spin_unlock_irq(&ic->endio_wait.lock);

	if (!write_sections)
		return;

	do_journal_write(ic, write_start, write_sections, false);

	spin_lock_irq(&ic->endio_wait.lock);

	ic->committed_section += write_sections;
	wraparound_section(ic, &ic->committed_section);
	ic->n_committed_sections -= write_sections;

	prev_free_sectors = ic->free_sectors;
	ic->free_sectors += write_sections * ic->journal_section_entries;
	if (unlikely(!prev_free_sectors))
		wake_up_locked(&ic->endio_wait);

	spin_unlock_irq(&ic->endio_wait.lock);
}

static void recalc_write_super(struct dm_integrity_c *ic)
{
	int r;

	dm_integrity_flush_buffers(ic);
	if (dm_integrity_failed(ic))
		return;

	sb_set_version(ic);
	r = sync_rw_sb(ic, REQ_OP_WRITE, 0);
	if (unlikely(r))
		dm_integrity_io_error(ic, "writing superblock", r);
}

static void integrity_recalc(struct work_struct *w)
{
	struct dm_integrity_c *ic = container_of(w, struct dm_integrity_c, recalc_work);
	struct dm_integrity_range range;
	struct dm_io_request io_req;
	struct dm_io_region io_loc;
	sector_t area, offset;
	sector_t metadata_block;
	unsigned metadata_offset;
	__u8 *t;
	unsigned i;
	int r;
	unsigned super_counter = 0;

	spin_lock_irq(&ic->endio_wait.lock);

next_chunk:

	if (unlikely(dm_suspended(ic->ti)))
		goto unlock_ret;

	range.logical_sector = le64_to_cpu(ic->sb->recalc_sector);
	if (unlikely(range.logical_sector >= ic->provided_data_sectors))
		goto unlock_ret;

	get_area_and_offset(ic, range.logical_sector, &area, &offset);
	range.n_sectors = min((sector_t)RECALC_SECTORS, ic->provided_data_sectors - range.logical_sector);
	if (!ic->meta_dev)
		range.n_sectors = min(range.n_sectors, (1U << ic->sb->log2_interleave_sectors) - (unsigned)offset);

	if (unlikely(!add_new_range(ic, &range, true)))
		wait_and_add_new_range(ic, &range);

	spin_unlock_irq(&ic->endio_wait.lock);

	if (unlikely(++super_counter == RECALC_WRITE_SUPER)) {
		recalc_write_super(ic);
		super_counter = 0;
	}

	if (unlikely(dm_integrity_failed(ic)))
		goto err;

	io_req.bi_op = REQ_OP_READ;
	io_req.bi_op_flags = 0;
	io_req.mem.type = DM_IO_VMA;
	io_req.mem.ptr.addr = ic->recalc_buffer;
	io_req.notify.fn = NULL;
	io_req.client = ic->io;
	io_loc.bdev = ic->dev->bdev;
	io_loc.sector = get_data_sector(ic, area, offset);
	io_loc.count = range.n_sectors;

	r = dm_io(&io_req, 1, &io_loc, NULL);
	if (unlikely(r)) {
		dm_integrity_io_error(ic, "reading data", r);
		goto err;
	}

	t = ic->recalc_tags;
	for (i = 0; i < range.n_sectors; i += ic->sectors_per_block) {
		integrity_sector_checksum(ic, range.logical_sector + i, ic->recalc_buffer + (i << SECTOR_SHIFT), t);
		t += ic->tag_size;
	}

	metadata_block = get_metadata_sector_and_offset(ic, area, offset, &metadata_offset);

	r = dm_integrity_rw_tag(ic, ic->recalc_tags, &metadata_block, &metadata_offset, t - ic->recalc_tags, TAG_WRITE);
	if (unlikely(r)) {
		dm_integrity_io_error(ic, "writing tags", r);
		goto err;
	}

	spin_lock_irq(&ic->endio_wait.lock);
	remove_range_unlocked(ic, &range);
	ic->sb->recalc_sector = cpu_to_le64(range.logical_sector + range.n_sectors);
	goto next_chunk;

err:
	remove_range(ic, &range);
	return;

unlock_ret:
	spin_unlock_irq(&ic->endio_wait.lock);

	recalc_write_super(ic);
}

static void init_journal(struct dm_integrity_c *ic, unsigned start_section,
			 unsigned n_sections, unsigned char commit_seq)
{
	unsigned i, j, n;

	if (!n_sections)
		return;

	for (n = 0; n < n_sections; n++) {
		i = start_section + n;
		wraparound_section(ic, &i);
		for (j = 0; j < ic->journal_section_sectors; j++) {
			struct journal_sector *js = access_journal(ic, i, j);
			memset(&js->entries, 0, JOURNAL_SECTOR_DATA);
			js->commit_id = dm_integrity_commit_id(ic, i, j, commit_seq);
		}
		for (j = 0; j < ic->journal_section_entries; j++) {
			struct journal_entry *je = access_journal_entry(ic, i, j);
			journal_entry_set_unused(je);
		}
	}

	write_journal(ic, start_section, n_sections);
}

static int find_commit_seq(struct dm_integrity_c *ic, unsigned i, unsigned j, commit_id_t id)
{
	unsigned char k;
	for (k = 0; k < N_COMMIT_IDS; k++) {
		if (dm_integrity_commit_id(ic, i, j, k) == id)
			return k;
	}
	dm_integrity_io_error(ic, "journal commit id", -EIO);
	return -EIO;
}

static void replay_journal(struct dm_integrity_c *ic)
{
	unsigned i, j;
	bool used_commit_ids[N_COMMIT_IDS];
	unsigned max_commit_id_sections[N_COMMIT_IDS];
	unsigned write_start, write_sections;
	unsigned continue_section;
	bool journal_empty;
	unsigned char unused, last_used, want_commit_seq;

	if (ic->mode == 'R')
		return;

	if (ic->journal_uptodate)
		return;

	last_used = 0;
	write_start = 0;

	if (!ic->just_formatted) {
		DEBUG_print("reading journal\n");
		rw_journal(ic, REQ_OP_READ, 0, 0, ic->journal_sections, NULL);
		if (ic->journal_io)
			DEBUG_bytes(lowmem_page_address(ic->journal_io[0].page), 64, "read journal");
		if (ic->journal_io) {
			struct journal_completion crypt_comp;
			crypt_comp.ic = ic;
			init_completion(&crypt_comp.comp);
			crypt_comp.in_flight = (atomic_t)ATOMIC_INIT(0);
			encrypt_journal(ic, false, 0, ic->journal_sections, &crypt_comp);
			wait_for_completion(&crypt_comp.comp);
		}
		DEBUG_bytes(lowmem_page_address(ic->journal[0].page), 64, "decrypted journal");
	}

	if (dm_integrity_failed(ic))
		goto clear_journal;

	journal_empty = true;
	memset(used_commit_ids, 0, sizeof used_commit_ids);
	memset(max_commit_id_sections, 0, sizeof max_commit_id_sections);
	for (i = 0; i < ic->journal_sections; i++) {
		for (j = 0; j < ic->journal_section_sectors; j++) {
			int k;
			struct journal_sector *js = access_journal(ic, i, j);
			k = find_commit_seq(ic, i, j, js->commit_id);
			if (k < 0)
				goto clear_journal;
			used_commit_ids[k] = true;
			max_commit_id_sections[k] = i;
		}
		if (journal_empty) {
			for (j = 0; j < ic->journal_section_entries; j++) {
				struct journal_entry *je = access_journal_entry(ic, i, j);
				if (!journal_entry_is_unused(je)) {
					journal_empty = false;
					break;
				}
			}
		}
	}

	if (!used_commit_ids[N_COMMIT_IDS - 1]) {
		unused = N_COMMIT_IDS - 1;
		while (unused && !used_commit_ids[unused - 1])
			unused--;
	} else {
		for (unused = 0; unused < N_COMMIT_IDS; unused++)
			if (!used_commit_ids[unused])
				break;
		if (unused == N_COMMIT_IDS) {
			dm_integrity_io_error(ic, "journal commit ids", -EIO);
			goto clear_journal;
		}
	}
	DEBUG_print("first unused commit seq %d [%d,%d,%d,%d]\n",
		    unused, used_commit_ids[0], used_commit_ids[1],
		    used_commit_ids[2], used_commit_ids[3]);

	last_used = prev_commit_seq(unused);
	want_commit_seq = prev_commit_seq(last_used);

	if (!used_commit_ids[want_commit_seq] && used_commit_ids[prev_commit_seq(want_commit_seq)])
		journal_empty = true;

	write_start = max_commit_id_sections[last_used] + 1;
	if (unlikely(write_start >= ic->journal_sections))
		want_commit_seq = next_commit_seq(want_commit_seq);
	wraparound_section(ic, &write_start);

	i = write_start;
	for (write_sections = 0; write_sections < ic->journal_sections; write_sections++) {
		for (j = 0; j < ic->journal_section_sectors; j++) {
			struct journal_sector *js = access_journal(ic, i, j);

			if (js->commit_id != dm_integrity_commit_id(ic, i, j, want_commit_seq)) {
				/*
				 * This could be caused by crash during writing.
				 * We won't replay the inconsistent part of the
				 * journal.
				 */
				DEBUG_print("commit id mismatch at position (%u, %u): %d != %d\n",
					    i, j, find_commit_seq(ic, i, j, js->commit_id), want_commit_seq);
				goto brk;
			}
		}
		i++;
		if (unlikely(i >= ic->journal_sections))
			want_commit_seq = next_commit_seq(want_commit_seq);
		wraparound_section(ic, &i);
	}
brk:

	if (!journal_empty) {
		DEBUG_print("replaying %u sections, starting at %u, commit seq %d\n",
			    write_sections, write_start, want_commit_seq);
		do_journal_write(ic, write_start, write_sections, true);
	}

	if (write_sections == ic->journal_sections && (ic->mode == 'J' || journal_empty)) {
		continue_section = write_start;
		ic->commit_seq = want_commit_seq;
		DEBUG_print("continuing from section %u, commit seq %d\n", write_start, ic->commit_seq);
	} else {
		unsigned s;
		unsigned char erase_seq;
clear_journal:
		DEBUG_print("clearing journal\n");

		erase_seq = prev_commit_seq(prev_commit_seq(last_used));
		s = write_start;
		init_journal(ic, s, 1, erase_seq);
		s++;
		wraparound_section(ic, &s);
		if (ic->journal_sections >= 2) {
			init_journal(ic, s, ic->journal_sections - 2, erase_seq);
			s += ic->journal_sections - 2;
			wraparound_section(ic, &s);
			init_journal(ic, s, 1, erase_seq);
		}

		continue_section = 0;
		ic->commit_seq = next_commit_seq(erase_seq);
	}

	ic->committed_section = continue_section;
	ic->n_committed_sections = 0;

	ic->uncommitted_section = continue_section;
	ic->n_uncommitted_sections = 0;

	ic->free_section = continue_section;
	ic->free_section_entry = 0;
	ic->free_sectors = ic->journal_entries;

	ic->journal_tree_root = RB_ROOT;
	for (i = 0; i < ic->journal_entries; i++)
		init_journal_node(&ic->journal_tree[i]);
}

static void dm_integrity_postsuspend(struct dm_target *ti)
{
	struct dm_integrity_c *ic = (struct dm_integrity_c *)ti->private;

	del_timer_sync(&ic->autocommit_timer);

	if (ic->recalc_wq)
		drain_workqueue(ic->recalc_wq);

	queue_work(ic->commit_wq, &ic->commit_work);
	drain_workqueue(ic->commit_wq);

	if (ic->mode == 'J') {
		if (ic->meta_dev)
			queue_work(ic->writer_wq, &ic->writer_work);
		drain_workqueue(ic->writer_wq);
		dm_integrity_flush_buffers(ic);
	}

	BUG_ON(!RB_EMPTY_ROOT(&ic->in_progress));

	ic->journal_uptodate = true;
}

static void dm_integrity_resume(struct dm_target *ti)
{
	struct dm_integrity_c *ic = (struct dm_integrity_c *)ti->private;

	replay_journal(ic);

	if (ic->recalc_wq && ic->sb->flags & cpu_to_le32(SB_FLAG_RECALCULATING)) {
		__u64 recalc_pos = le64_to_cpu(ic->sb->recalc_sector);
		if (recalc_pos < ic->provided_data_sectors) {
			queue_work(ic->recalc_wq, &ic->recalc_work);
		} else if (recalc_pos > ic->provided_data_sectors) {
			ic->sb->recalc_sector = cpu_to_le64(ic->provided_data_sectors);
			recalc_write_super(ic);
		}
	}
}

static void dm_integrity_status(struct dm_target *ti, status_type_t type,
				unsigned status_flags, char *result, unsigned maxlen)
{
	struct dm_integrity_c *ic = (struct dm_integrity_c *)ti->private;
	unsigned arg_count;
	size_t sz = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		DMEMIT("%llu %llu",
			(unsigned long long)atomic64_read(&ic->number_of_mismatches),
			(unsigned long long)ic->provided_data_sectors);
		if (ic->sb->flags & cpu_to_le32(SB_FLAG_RECALCULATING))
			DMEMIT(" %llu", (unsigned long long)le64_to_cpu(ic->sb->recalc_sector));
		else
			DMEMIT(" -");
		break;

	case STATUSTYPE_TABLE: {
		__u64 watermark_percentage = (__u64)(ic->journal_entries - ic->free_sectors_threshold) * 100;
		watermark_percentage += ic->journal_entries / 2;
		do_div(watermark_percentage, ic->journal_entries);
		arg_count = 5;
		arg_count += !!ic->meta_dev;
		arg_count += ic->sectors_per_block != 1;
		arg_count += !!(ic->sb->flags & cpu_to_le32(SB_FLAG_RECALCULATING));
		arg_count += !!ic->internal_hash_alg.alg_string;
		arg_count += !!ic->journal_crypt_alg.alg_string;
		arg_count += !!ic->journal_mac_alg.alg_string;
		DMEMIT("%s %llu %u %c %u", ic->dev->name, (unsigned long long)ic->start,
		       ic->tag_size, ic->mode, arg_count);
		if (ic->meta_dev)
			DMEMIT(" meta_device:%s", ic->meta_dev->name);
		if (ic->sectors_per_block != 1)
			DMEMIT(" block_size:%u", ic->sectors_per_block << SECTOR_SHIFT);
		if (ic->sb->flags & cpu_to_le32(SB_FLAG_RECALCULATING))
			DMEMIT(" recalculate");
		DMEMIT(" journal_sectors:%u", ic->initial_sectors - SB_SECTORS);
		DMEMIT(" interleave_sectors:%u", 1U << ic->sb->log2_interleave_sectors);
		DMEMIT(" buffer_sectors:%u", 1U << ic->log2_buffer_sectors);
		DMEMIT(" journal_watermark:%u", (unsigned)watermark_percentage);
		DMEMIT(" commit_time:%u", ic->autocommit_msec);

#define EMIT_ALG(a, n)							\
		do {							\
			if (ic->a.alg_string) {				\
				DMEMIT(" %s:%s", n, ic->a.alg_string);	\
				if (ic->a.key_string)			\
					DMEMIT(":%s", ic->a.key_string);\
			}						\
		} while (0)
		EMIT_ALG(internal_hash_alg, "internal_hash");
		EMIT_ALG(journal_crypt_alg, "journal_crypt");
		EMIT_ALG(journal_mac_alg, "journal_mac");
		break;
	}
	}
}

static int dm_integrity_iterate_devices(struct dm_target *ti,
					iterate_devices_callout_fn fn, void *data)
{
	struct dm_integrity_c *ic = ti->private;

	if (!ic->meta_dev)
		return fn(ti, ic->dev, ic->start + ic->initial_sectors + ic->metadata_run, ti->len, data);
	else
		return fn(ti, ic->dev, 0, ti->len, data);
}

static void dm_integrity_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct dm_integrity_c *ic = ti->private;

	if (ic->sectors_per_block > 1) {
		limits->logical_block_size = ic->sectors_per_block << SECTOR_SHIFT;
		limits->physical_block_size = ic->sectors_per_block << SECTOR_SHIFT;
		blk_limits_io_min(limits, ic->sectors_per_block << SECTOR_SHIFT);
	}
}

static void calculate_journal_section_size(struct dm_integrity_c *ic)
{
	unsigned sector_space = JOURNAL_SECTOR_DATA;

	ic->journal_sections = le32_to_cpu(ic->sb->journal_sections);
	ic->journal_entry_size = roundup(offsetof(struct journal_entry, last_bytes[ic->sectors_per_block]) + ic->tag_size,
					 JOURNAL_ENTRY_ROUNDUP);

	if (ic->sb->flags & cpu_to_le32(SB_FLAG_HAVE_JOURNAL_MAC))
		sector_space -= JOURNAL_MAC_PER_SECTOR;
	ic->journal_entries_per_sector = sector_space / ic->journal_entry_size;
	ic->journal_section_entries = ic->journal_entries_per_sector * JOURNAL_BLOCK_SECTORS;
	ic->journal_section_sectors = (ic->journal_section_entries << ic->sb->log2_sectors_per_block) + JOURNAL_BLOCK_SECTORS;
	ic->journal_entries = ic->journal_section_entries * ic->journal_sections;
}

static int calculate_device_limits(struct dm_integrity_c *ic)
{
	__u64 initial_sectors;

	calculate_journal_section_size(ic);
	initial_sectors = SB_SECTORS + (__u64)ic->journal_section_sectors * ic->journal_sections;
	if (initial_sectors + METADATA_PADDING_SECTORS >= ic->meta_device_sectors || initial_sectors > UINT_MAX)
		return -EINVAL;
	ic->initial_sectors = initial_sectors;

	if (!ic->meta_dev) {
		sector_t last_sector, last_area, last_offset;

		ic->metadata_run = roundup((__u64)ic->tag_size << (ic->sb->log2_interleave_sectors - ic->sb->log2_sectors_per_block),
					   (__u64)(1 << SECTOR_SHIFT << METADATA_PADDING_SECTORS)) >> SECTOR_SHIFT;
		if (!(ic->metadata_run & (ic->metadata_run - 1)))
			ic->log2_metadata_run = __ffs(ic->metadata_run);
		else
			ic->log2_metadata_run = -1;

		get_area_and_offset(ic, ic->provided_data_sectors - 1, &last_area, &last_offset);
		last_sector = get_data_sector(ic, last_area, last_offset);
		if (last_sector < ic->start || last_sector >= ic->meta_device_sectors)
			return -EINVAL;
	} else {
		__u64 meta_size = (ic->provided_data_sectors >> ic->sb->log2_sectors_per_block) * ic->tag_size;
		meta_size = (meta_size + ((1U << (ic->log2_buffer_sectors + SECTOR_SHIFT)) - 1))
				>> (ic->log2_buffer_sectors + SECTOR_SHIFT);
		meta_size <<= ic->log2_buffer_sectors;
		if (ic->initial_sectors + meta_size < ic->initial_sectors ||
		    ic->initial_sectors + meta_size > ic->meta_device_sectors)
			return -EINVAL;
		ic->metadata_run = 1;
		ic->log2_metadata_run = 0;
	}

	return 0;
}

static int initialize_superblock(struct dm_integrity_c *ic, unsigned journal_sectors, unsigned interleave_sectors)
{
	unsigned journal_sections;
	int test_bit;

	memset(ic->sb, 0, SB_SECTORS << SECTOR_SHIFT);
	memcpy(ic->sb->magic, SB_MAGIC, 8);
	ic->sb->integrity_tag_size = cpu_to_le16(ic->tag_size);
	ic->sb->log2_sectors_per_block = __ffs(ic->sectors_per_block);
	if (ic->journal_mac_alg.alg_string)
		ic->sb->flags |= cpu_to_le32(SB_FLAG_HAVE_JOURNAL_MAC);

	calculate_journal_section_size(ic);
	journal_sections = journal_sectors / ic->journal_section_sectors;
	if (!journal_sections)
		journal_sections = 1;

	if (!ic->meta_dev) {
		ic->sb->journal_sections = cpu_to_le32(journal_sections);
		if (!interleave_sectors)
			interleave_sectors = DEFAULT_INTERLEAVE_SECTORS;
		ic->sb->log2_interleave_sectors = __fls(interleave_sectors);
		ic->sb->log2_interleave_sectors = max((__u8)MIN_LOG2_INTERLEAVE_SECTORS, ic->sb->log2_interleave_sectors);
		ic->sb->log2_interleave_sectors = min((__u8)MAX_LOG2_INTERLEAVE_SECTORS, ic->sb->log2_interleave_sectors);

		ic->provided_data_sectors = 0;
		for (test_bit = fls64(ic->meta_device_sectors) - 1; test_bit >= 3; test_bit--) {
			__u64 prev_data_sectors = ic->provided_data_sectors;

			ic->provided_data_sectors |= (sector_t)1 << test_bit;
			if (calculate_device_limits(ic))
				ic->provided_data_sectors = prev_data_sectors;
		}
		if (!ic->provided_data_sectors)
			return -EINVAL;
	} else {
		ic->sb->log2_interleave_sectors = 0;
		ic->provided_data_sectors = ic->data_device_sectors;
		ic->provided_data_sectors &= ~(sector_t)(ic->sectors_per_block - 1);

try_smaller_buffer:
		ic->sb->journal_sections = cpu_to_le32(0);
		for (test_bit = fls(journal_sections) - 1; test_bit >= 0; test_bit--) {
			__u32 prev_journal_sections = le32_to_cpu(ic->sb->journal_sections);
			__u32 test_journal_sections = prev_journal_sections | (1U << test_bit);
			if (test_journal_sections > journal_sections)
				continue;
			ic->sb->journal_sections = cpu_to_le32(test_journal_sections);
			if (calculate_device_limits(ic))
				ic->sb->journal_sections = cpu_to_le32(prev_journal_sections);

		}
		if (!le32_to_cpu(ic->sb->journal_sections)) {
			if (ic->log2_buffer_sectors > 3) {
				ic->log2_buffer_sectors--;
				goto try_smaller_buffer;
			}
			return -EINVAL;
		}
	}

	ic->sb->provided_data_sectors = cpu_to_le64(ic->provided_data_sectors);

	sb_set_version(ic);

	return 0;
}

static void dm_integrity_set(struct dm_target *ti, struct dm_integrity_c *ic)
{
	struct gendisk *disk = dm_disk(dm_table_get_md(ti->table));
	struct blk_integrity bi;

	memset(&bi, 0, sizeof(bi));
	bi.profile = &dm_integrity_profile;
	bi.tuple_size = ic->tag_size;
	bi.tag_size = bi.tuple_size;
	bi.interval_exp = ic->sb->log2_sectors_per_block + SECTOR_SHIFT;

	blk_integrity_register(disk, &bi);
	blk_queue_max_integrity_segments(disk->queue, UINT_MAX);
}

static void dm_integrity_free_page_list(struct dm_integrity_c *ic, struct page_list *pl)
{
	unsigned i;

	if (!pl)
		return;
	for (i = 0; i < ic->journal_pages; i++)
		if (pl[i].page)
			__free_page(pl[i].page);
	kvfree(pl);
}

static struct page_list *dm_integrity_alloc_page_list(struct dm_integrity_c *ic)
{
	size_t page_list_desc_size = ic->journal_pages * sizeof(struct page_list);
	struct page_list *pl;
	unsigned i;

	pl = kvmalloc(page_list_desc_size, GFP_KERNEL | __GFP_ZERO);
	if (!pl)
		return NULL;

	for (i = 0; i < ic->journal_pages; i++) {
		pl[i].page = alloc_page(GFP_KERNEL);
		if (!pl[i].page) {
			dm_integrity_free_page_list(ic, pl);
			return NULL;
		}
		if (i)
			pl[i - 1].next = &pl[i];
	}

	return pl;
}

static void dm_integrity_free_journal_scatterlist(struct dm_integrity_c *ic, struct scatterlist **sl)
{
	unsigned i;
	for (i = 0; i < ic->journal_sections; i++)
		kvfree(sl[i]);
	kvfree(sl);
}

static struct scatterlist **dm_integrity_alloc_journal_scatterlist(struct dm_integrity_c *ic, struct page_list *pl)
{
	struct scatterlist **sl;
	unsigned i;

	sl = kvmalloc_array(ic->journal_sections,
			    sizeof(struct scatterlist *),
			    GFP_KERNEL | __GFP_ZERO);
	if (!sl)
		return NULL;

	for (i = 0; i < ic->journal_sections; i++) {
		struct scatterlist *s;
		unsigned start_index, start_offset;
		unsigned end_index, end_offset;
		unsigned n_pages;
		unsigned idx;

		page_list_location(ic, i, 0, &start_index, &start_offset);
		page_list_location(ic, i, ic->journal_section_sectors - 1, &end_index, &end_offset);

		n_pages = (end_index - start_index + 1);

		s = kvmalloc_array(n_pages, sizeof(struct scatterlist),
				   GFP_KERNEL);
		if (!s) {
			dm_integrity_free_journal_scatterlist(ic, sl);
			return NULL;
		}

		sg_init_table(s, n_pages);
		for (idx = start_index; idx <= end_index; idx++) {
			char *va = lowmem_page_address(pl[idx].page);
			unsigned start = 0, end = PAGE_SIZE;
			if (idx == start_index)
				start = start_offset;
			if (idx == end_index)
				end = end_offset + (1 << SECTOR_SHIFT);
			sg_set_buf(&s[idx - start_index], va + start, end - start);
		}

		sl[i] = s;
	}

	return sl;
}

static void free_alg(struct alg_spec *a)
{
	kzfree(a->alg_string);
	kzfree(a->key);
	memset(a, 0, sizeof *a);
}

static int get_alg_and_key(const char *arg, struct alg_spec *a, char **error, char *error_inval)
{
	char *k;

	free_alg(a);

	a->alg_string = kstrdup(strchr(arg, ':') + 1, GFP_KERNEL);
	if (!a->alg_string)
		goto nomem;

	k = strchr(a->alg_string, ':');
	if (k) {
		*k = 0;
		a->key_string = k + 1;
		if (strlen(a->key_string) & 1)
			goto inval;

		a->key_size = strlen(a->key_string) / 2;
		a->key = kmalloc(a->key_size, GFP_KERNEL);
		if (!a->key)
			goto nomem;
		if (hex2bin(a->key, a->key_string, a->key_size))
			goto inval;
	}

	return 0;
inval:
	*error = error_inval;
	return -EINVAL;
nomem:
	*error = "Out of memory for an argument";
	return -ENOMEM;
}

static int get_mac(struct crypto_shash **hash, struct alg_spec *a, char **error,
		   char *error_alg, char *error_key)
{
	int r;

	if (a->alg_string) {
		*hash = crypto_alloc_shash(a->alg_string, 0, CRYPTO_ALG_ASYNC);
		if (IS_ERR(*hash)) {
			*error = error_alg;
			r = PTR_ERR(*hash);
			*hash = NULL;
			return r;
		}

		if (a->key) {
			r = crypto_shash_setkey(*hash, a->key, a->key_size);
			if (r) {
				*error = error_key;
				return r;
			}
		} else if (crypto_shash_get_flags(*hash) & CRYPTO_TFM_NEED_KEY) {
			*error = error_key;
			return -ENOKEY;
		}
	}

	return 0;
}

static int create_journal(struct dm_integrity_c *ic, char **error)
{
	int r = 0;
	unsigned i;
	__u64 journal_pages, journal_desc_size, journal_tree_size;
	unsigned char *crypt_data = NULL, *crypt_iv = NULL;
	struct skcipher_request *req = NULL;

	ic->commit_ids[0] = cpu_to_le64(0x1111111111111111ULL);
	ic->commit_ids[1] = cpu_to_le64(0x2222222222222222ULL);
	ic->commit_ids[2] = cpu_to_le64(0x3333333333333333ULL);
	ic->commit_ids[3] = cpu_to_le64(0x4444444444444444ULL);

	journal_pages = roundup((__u64)ic->journal_sections * ic->journal_section_sectors,
				PAGE_SIZE >> SECTOR_SHIFT) >> (PAGE_SHIFT - SECTOR_SHIFT);
	journal_desc_size = journal_pages * sizeof(struct page_list);
	if (journal_pages >= totalram_pages - totalhigh_pages || journal_desc_size > ULONG_MAX) {
		*error = "Journal doesn't fit into memory";
		r = -ENOMEM;
		goto bad;
	}
	ic->journal_pages = journal_pages;

	ic->journal = dm_integrity_alloc_page_list(ic);
	if (!ic->journal) {
		*error = "Could not allocate memory for journal";
		r = -ENOMEM;
		goto bad;
	}
	if (ic->journal_crypt_alg.alg_string) {
		unsigned ivsize, blocksize;
		struct journal_completion comp;

		comp.ic = ic;
		ic->journal_crypt = crypto_alloc_skcipher(ic->journal_crypt_alg.alg_string, 0, 0);
		if (IS_ERR(ic->journal_crypt)) {
			*error = "Invalid journal cipher";
			r = PTR_ERR(ic->journal_crypt);
			ic->journal_crypt = NULL;
			goto bad;
		}
		ivsize = crypto_skcipher_ivsize(ic->journal_crypt);
		blocksize = crypto_skcipher_blocksize(ic->journal_crypt);

		if (ic->journal_crypt_alg.key) {
			r = crypto_skcipher_setkey(ic->journal_crypt, ic->journal_crypt_alg.key,
						   ic->journal_crypt_alg.key_size);
			if (r) {
				*error = "Error setting encryption key";
				goto bad;
			}
		}
		DEBUG_print("cipher %s, block size %u iv size %u\n",
			    ic->journal_crypt_alg.alg_string, blocksize, ivsize);

		ic->journal_io = dm_integrity_alloc_page_list(ic);
		if (!ic->journal_io) {
			*error = "Could not allocate memory for journal io";
			r = -ENOMEM;
			goto bad;
		}

		if (blocksize == 1) {
			struct scatterlist *sg;

			req = skcipher_request_alloc(ic->journal_crypt, GFP_KERNEL);
			if (!req) {
				*error = "Could not allocate crypt request";
				r = -ENOMEM;
				goto bad;
			}

			crypt_iv = kmalloc(ivsize, GFP_KERNEL);
			if (!crypt_iv) {
				*error = "Could not allocate iv";
				r = -ENOMEM;
				goto bad;
			}

			ic->journal_xor = dm_integrity_alloc_page_list(ic);
			if (!ic->journal_xor) {
				*error = "Could not allocate memory for journal xor";
				r = -ENOMEM;
				goto bad;
			}

			sg = kvmalloc_array(ic->journal_pages + 1,
					    sizeof(struct scatterlist),
					    GFP_KERNEL);
			if (!sg) {
				*error = "Unable to allocate sg list";
				r = -ENOMEM;
				goto bad;
			}
			sg_init_table(sg, ic->journal_pages + 1);
			for (i = 0; i < ic->journal_pages; i++) {
				char *va = lowmem_page_address(ic->journal_xor[i].page);
				clear_page(va);
				sg_set_buf(&sg[i], va, PAGE_SIZE);
			}
			sg_set_buf(&sg[i], &ic->commit_ids, sizeof ic->commit_ids);
			memset(crypt_iv, 0x00, ivsize);

			skcipher_request_set_crypt(req, sg, sg, PAGE_SIZE * ic->journal_pages + sizeof ic->commit_ids, crypt_iv);
			init_completion(&comp.comp);
			comp.in_flight = (atomic_t)ATOMIC_INIT(1);
			if (do_crypt(true, req, &comp))
				wait_for_completion(&comp.comp);
			kvfree(sg);
			r = dm_integrity_failed(ic);
			if (r) {
				*error = "Unable to encrypt journal";
				goto bad;
			}
			DEBUG_bytes(lowmem_page_address(ic->journal_xor[0].page), 64, "xor data");

			crypto_free_skcipher(ic->journal_crypt);
			ic->journal_crypt = NULL;
		} else {
			unsigned crypt_len = roundup(ivsize, blocksize);

			req = skcipher_request_alloc(ic->journal_crypt, GFP_KERNEL);
			if (!req) {
				*error = "Could not allocate crypt request";
				r = -ENOMEM;
				goto bad;
			}

			crypt_iv = kmalloc(ivsize, GFP_KERNEL);
			if (!crypt_iv) {
				*error = "Could not allocate iv";
				r = -ENOMEM;
				goto bad;
			}

			crypt_data = kmalloc(crypt_len, GFP_KERNEL);
			if (!crypt_data) {
				*error = "Unable to allocate crypt data";
				r = -ENOMEM;
				goto bad;
			}

			ic->journal_scatterlist = dm_integrity_alloc_journal_scatterlist(ic, ic->journal);
			if (!ic->journal_scatterlist) {
				*error = "Unable to allocate sg list";
				r = -ENOMEM;
				goto bad;
			}
			ic->journal_io_scatterlist = dm_integrity_alloc_journal_scatterlist(ic, ic->journal_io);
			if (!ic->journal_io_scatterlist) {
				*error = "Unable to allocate sg list";
				r = -ENOMEM;
				goto bad;
			}
			ic->sk_requests = kvmalloc_array(ic->journal_sections,
							 sizeof(struct skcipher_request *),
							 GFP_KERNEL | __GFP_ZERO);
			if (!ic->sk_requests) {
				*error = "Unable to allocate sk requests";
				r = -ENOMEM;
				goto bad;
			}
			for (i = 0; i < ic->journal_sections; i++) {
				struct scatterlist sg;
				struct skcipher_request *section_req;
				__u32 section_le = cpu_to_le32(i);

				memset(crypt_iv, 0x00, ivsize);
				memset(crypt_data, 0x00, crypt_len);
				memcpy(crypt_data, &section_le, min((size_t)crypt_len, sizeof(section_le)));

				sg_init_one(&sg, crypt_data, crypt_len);
				skcipher_request_set_crypt(req, &sg, &sg, crypt_len, crypt_iv);
				init_completion(&comp.comp);
				comp.in_flight = (atomic_t)ATOMIC_INIT(1);
				if (do_crypt(true, req, &comp))
					wait_for_completion(&comp.comp);

				r = dm_integrity_failed(ic);
				if (r) {
					*error = "Unable to generate iv";
					goto bad;
				}

				section_req = skcipher_request_alloc(ic->journal_crypt, GFP_KERNEL);
				if (!section_req) {
					*error = "Unable to allocate crypt request";
					r = -ENOMEM;
					goto bad;
				}
				section_req->iv = kmalloc_array(ivsize, 2,
								GFP_KERNEL);
				if (!section_req->iv) {
					skcipher_request_free(section_req);
					*error = "Unable to allocate iv";
					r = -ENOMEM;
					goto bad;
				}
				memcpy(section_req->iv + ivsize, crypt_data, ivsize);
				section_req->cryptlen = (size_t)ic->journal_section_sectors << SECTOR_SHIFT;
				ic->sk_requests[i] = section_req;
				DEBUG_bytes(crypt_data, ivsize, "iv(%u)", i);
			}
		}
	}

	for (i = 0; i < N_COMMIT_IDS; i++) {
		unsigned j;
retest_commit_id:
		for (j = 0; j < i; j++) {
			if (ic->commit_ids[j] == ic->commit_ids[i]) {
				ic->commit_ids[i] = cpu_to_le64(le64_to_cpu(ic->commit_ids[i]) + 1);
				goto retest_commit_id;
			}
		}
		DEBUG_print("commit id %u: %016llx\n", i, ic->commit_ids[i]);
	}

	journal_tree_size = (__u64)ic->journal_entries * sizeof(struct journal_node);
	if (journal_tree_size > ULONG_MAX) {
		*error = "Journal doesn't fit into memory";
		r = -ENOMEM;
		goto bad;
	}
	ic->journal_tree = kvmalloc(journal_tree_size, GFP_KERNEL);
	if (!ic->journal_tree) {
		*error = "Could not allocate memory for journal tree";
		r = -ENOMEM;
	}
bad:
	kfree(crypt_data);
	kfree(crypt_iv);
	skcipher_request_free(req);

	return r;
}

/*
 * Construct a integrity mapping
 *
 * Arguments:
 *	device
 *	offset from the start of the device
 *	tag size
 *	D - direct writes, J - journal writes, R - recovery mode
 *	number of optional arguments
 *	optional arguments:
 *		journal_sectors
 *		interleave_sectors
 *		buffer_sectors
 *		journal_watermark
 *		commit_time
 *		internal_hash
 *		journal_crypt
 *		journal_mac
 *		block_size
 */
static int dm_integrity_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
	struct dm_integrity_c *ic;
	char dummy;
	int r;
	unsigned extra_args;
	struct dm_arg_set as;
	static const struct dm_arg _args[] = {
		{0, 9, "Invalid number of feature args"},
	};
	unsigned journal_sectors, interleave_sectors, buffer_sectors, journal_watermark, sync_msec;
	bool recalculate;
	bool should_write_sb;
	__u64 threshold;
	unsigned long long start;

#define DIRECT_ARGUMENTS	4

	if (argc <= DIRECT_ARGUMENTS) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	ic = kzalloc(sizeof(struct dm_integrity_c), GFP_KERNEL);
	if (!ic) {
		ti->error = "Cannot allocate integrity context";
		return -ENOMEM;
	}
	ti->private = ic;
	ti->per_io_data_size = sizeof(struct dm_integrity_io);
	ic->ti = ti;

	ic->in_progress = RB_ROOT;
	INIT_LIST_HEAD(&ic->wait_list);
	init_waitqueue_head(&ic->endio_wait);
	bio_list_init(&ic->flush_bio_list);
	init_waitqueue_head(&ic->copy_to_journal_wait);
	init_completion(&ic->crypto_backoff);
	atomic64_set(&ic->number_of_mismatches, 0);

	r = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &ic->dev);
	if (r) {
		ti->error = "Device lookup failed";
		goto bad;
	}

	if (sscanf(argv[1], "%llu%c", &start, &dummy) != 1 || start != (sector_t)start) {
		ti->error = "Invalid starting offset";
		r = -EINVAL;
		goto bad;
	}
	ic->start = start;

	if (strcmp(argv[2], "-")) {
		if (sscanf(argv[2], "%u%c", &ic->tag_size, &dummy) != 1 || !ic->tag_size) {
			ti->error = "Invalid tag size";
			r = -EINVAL;
			goto bad;
		}
	}

	if (!strcmp(argv[3], "J") || !strcmp(argv[3], "D") || !strcmp(argv[3], "R"))
		ic->mode = argv[3][0];
	else {
		ti->error = "Invalid mode (expecting J, D, R)";
		r = -EINVAL;
		goto bad;
	}

	journal_sectors = 0;
	interleave_sectors = DEFAULT_INTERLEAVE_SECTORS;
	buffer_sectors = DEFAULT_BUFFER_SECTORS;
	journal_watermark = DEFAULT_JOURNAL_WATERMARK;
	sync_msec = DEFAULT_SYNC_MSEC;
	recalculate = false;
	ic->sectors_per_block = 1;

	as.argc = argc - DIRECT_ARGUMENTS;
	as.argv = argv + DIRECT_ARGUMENTS;
	r = dm_read_arg_group(_args, &as, &extra_args, &ti->error);
	if (r)
		goto bad;

	while (extra_args--) {
		const char *opt_string;
		unsigned val;
		opt_string = dm_shift_arg(&as);
		if (!opt_string) {
			r = -EINVAL;
			ti->error = "Not enough feature arguments";
			goto bad;
		}
		if (sscanf(opt_string, "journal_sectors:%u%c", &val, &dummy) == 1)
			journal_sectors = val ? val : 1;
		else if (sscanf(opt_string, "interleave_sectors:%u%c", &val, &dummy) == 1)
			interleave_sectors = val;
		else if (sscanf(opt_string, "buffer_sectors:%u%c", &val, &dummy) == 1)
			buffer_sectors = val;
		else if (sscanf(opt_string, "journal_watermark:%u%c", &val, &dummy) == 1 && val <= 100)
			journal_watermark = val;
		else if (sscanf(opt_string, "commit_time:%u%c", &val, &dummy) == 1)
			sync_msec = val;
		else if (!strncmp(opt_string, "meta_device:", strlen("meta_device:"))) {
			if (ic->meta_dev) {
				dm_put_device(ti, ic->meta_dev);
				ic->meta_dev = NULL;
			}
			r = dm_get_device(ti, strchr(opt_string, ':') + 1, dm_table_get_mode(ti->table), &ic->meta_dev);
			if (r) {
				ti->error = "Device lookup failed";
				goto bad;
			}
		} else if (sscanf(opt_string, "block_size:%u%c", &val, &dummy) == 1) {
			if (val < 1 << SECTOR_SHIFT ||
			    val > MAX_SECTORS_PER_BLOCK << SECTOR_SHIFT ||
			    (val & (val -1))) {
				r = -EINVAL;
				ti->error = "Invalid block_size argument";
				goto bad;
			}
			ic->sectors_per_block = val >> SECTOR_SHIFT;
		} else if (!strncmp(opt_string, "internal_hash:", strlen("internal_hash:"))) {
			r = get_alg_and_key(opt_string, &ic->internal_hash_alg, &ti->error,
					    "Invalid internal_hash argument");
			if (r)
				goto bad;
		} else if (!strncmp(opt_string, "journal_crypt:", strlen("journal_crypt:"))) {
			r = get_alg_and_key(opt_string, &ic->journal_crypt_alg, &ti->error,
					    "Invalid journal_crypt argument");
			if (r)
				goto bad;
		} else if (!strncmp(opt_string, "journal_mac:", strlen("journal_mac:"))) {
			r = get_alg_and_key(opt_string, &ic->journal_mac_alg,  &ti->error,
					    "Invalid journal_mac argument");
			if (r)
				goto bad;
		} else if (!strcmp(opt_string, "recalculate")) {
			recalculate = true;
		} else {
			r = -EINVAL;
			ti->error = "Invalid argument";
			goto bad;
		}
	}

	ic->data_device_sectors = i_size_read(ic->dev->bdev->bd_inode) >> SECTOR_SHIFT;
	if (!ic->meta_dev)
		ic->meta_device_sectors = ic->data_device_sectors;
	else
		ic->meta_device_sectors = i_size_read(ic->meta_dev->bdev->bd_inode) >> SECTOR_SHIFT;

	if (!journal_sectors) {
		journal_sectors = min((sector_t)DEFAULT_MAX_JOURNAL_SECTORS,
			ic->data_device_sectors >> DEFAULT_JOURNAL_SIZE_FACTOR);
	}

	if (!buffer_sectors)
		buffer_sectors = 1;
	ic->log2_buffer_sectors = min((int)__fls(buffer_sectors), 31 - SECTOR_SHIFT);

	r = get_mac(&ic->internal_hash, &ic->internal_hash_alg, &ti->error,
		    "Invalid internal hash", "Error setting internal hash key");
	if (r)
		goto bad;

	r = get_mac(&ic->journal_mac, &ic->journal_mac_alg, &ti->error,
		    "Invalid journal mac", "Error setting journal mac key");
	if (r)
		goto bad;

	if (!ic->tag_size) {
		if (!ic->internal_hash) {
			ti->error = "Unknown tag size";
			r = -EINVAL;
			goto bad;
		}
		ic->tag_size = crypto_shash_digestsize(ic->internal_hash);
	}
	if (ic->tag_size > MAX_TAG_SIZE) {
		ti->error = "Too big tag size";
		r = -EINVAL;
		goto bad;
	}
	if (!(ic->tag_size & (ic->tag_size - 1)))
		ic->log2_tag_size = __ffs(ic->tag_size);
	else
		ic->log2_tag_size = -1;

	ic->autocommit_jiffies = msecs_to_jiffies(sync_msec);
	ic->autocommit_msec = sync_msec;
	timer_setup(&ic->autocommit_timer, autocommit_fn, 0);

	ic->io = dm_io_client_create();
	if (IS_ERR(ic->io)) {
		r = PTR_ERR(ic->io);
		ic->io = NULL;
		ti->error = "Cannot allocate dm io";
		goto bad;
	}

	r = mempool_init_slab_pool(&ic->journal_io_mempool, JOURNAL_IO_MEMPOOL, journal_io_cache);
	if (r) {
		ti->error = "Cannot allocate mempool";
		goto bad;
	}

	ic->metadata_wq = alloc_workqueue("dm-integrity-metadata",
					  WQ_MEM_RECLAIM, METADATA_WORKQUEUE_MAX_ACTIVE);
	if (!ic->metadata_wq) {
		ti->error = "Cannot allocate workqueue";
		r = -ENOMEM;
		goto bad;
	}

	/*
	 * If this workqueue were percpu, it would cause bio reordering
	 * and reduced performance.
	 */
	ic->wait_wq = alloc_workqueue("dm-integrity-wait", WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	if (!ic->wait_wq) {
		ti->error = "Cannot allocate workqueue";
		r = -ENOMEM;
		goto bad;
	}

	ic->commit_wq = alloc_workqueue("dm-integrity-commit", WQ_MEM_RECLAIM, 1);
	if (!ic->commit_wq) {
		ti->error = "Cannot allocate workqueue";
		r = -ENOMEM;
		goto bad;
	}
	INIT_WORK(&ic->commit_work, integrity_commit);

	if (ic->mode == 'J') {
		ic->writer_wq = alloc_workqueue("dm-integrity-writer", WQ_MEM_RECLAIM, 1);
		if (!ic->writer_wq) {
			ti->error = "Cannot allocate workqueue";
			r = -ENOMEM;
			goto bad;
		}
		INIT_WORK(&ic->writer_work, integrity_writer);
	}

	ic->sb = alloc_pages_exact(SB_SECTORS << SECTOR_SHIFT, GFP_KERNEL);
	if (!ic->sb) {
		r = -ENOMEM;
		ti->error = "Cannot allocate superblock area";
		goto bad;
	}

	r = sync_rw_sb(ic, REQ_OP_READ, 0);
	if (r) {
		ti->error = "Error reading superblock";
		goto bad;
	}
	should_write_sb = false;
	if (memcmp(ic->sb->magic, SB_MAGIC, 8)) {
		if (ic->mode != 'R') {
			if (memchr_inv(ic->sb, 0, SB_SECTORS << SECTOR_SHIFT)) {
				r = -EINVAL;
				ti->error = "The device is not initialized";
				goto bad;
			}
		}

		r = initialize_superblock(ic, journal_sectors, interleave_sectors);
		if (r) {
			ti->error = "Could not initialize superblock";
			goto bad;
		}
		if (ic->mode != 'R')
			should_write_sb = true;
	}

	if (!ic->sb->version || ic->sb->version > SB_VERSION_2) {
		r = -EINVAL;
		ti->error = "Unknown version";
		goto bad;
	}
	if (le16_to_cpu(ic->sb->integrity_tag_size) != ic->tag_size) {
		r = -EINVAL;
		ti->error = "Tag size doesn't match the information in superblock";
		goto bad;
	}
	if (ic->sb->log2_sectors_per_block != __ffs(ic->sectors_per_block)) {
		r = -EINVAL;
		ti->error = "Block size doesn't match the information in superblock";
		goto bad;
	}
	if (!le32_to_cpu(ic->sb->journal_sections)) {
		r = -EINVAL;
		ti->error = "Corrupted superblock, journal_sections is 0";
		goto bad;
	}
	/* make sure that ti->max_io_len doesn't overflow */
	if (!ic->meta_dev) {
		if (ic->sb->log2_interleave_sectors < MIN_LOG2_INTERLEAVE_SECTORS ||
		    ic->sb->log2_interleave_sectors > MAX_LOG2_INTERLEAVE_SECTORS) {
			r = -EINVAL;
			ti->error = "Invalid interleave_sectors in the superblock";
			goto bad;
		}
	} else {
		if (ic->sb->log2_interleave_sectors) {
			r = -EINVAL;
			ti->error = "Invalid interleave_sectors in the superblock";
			goto bad;
		}
	}
	ic->provided_data_sectors = le64_to_cpu(ic->sb->provided_data_sectors);
	if (ic->provided_data_sectors != le64_to_cpu(ic->sb->provided_data_sectors)) {
		/* test for overflow */
		r = -EINVAL;
		ti->error = "The superblock has 64-bit device size, but the kernel was compiled with 32-bit sectors";
		goto bad;
	}
	if (!!(ic->sb->flags & cpu_to_le32(SB_FLAG_HAVE_JOURNAL_MAC)) != !!ic->journal_mac_alg.alg_string) {
		r = -EINVAL;
		ti->error = "Journal mac mismatch";
		goto bad;
	}

try_smaller_buffer:
	r = calculate_device_limits(ic);
	if (r) {
		if (ic->meta_dev) {
			if (ic->log2_buffer_sectors > 3) {
				ic->log2_buffer_sectors--;
				goto try_smaller_buffer;
			}
		}
		ti->error = "The device is too small";
		goto bad;
	}
	if (!ic->meta_dev)
		ic->log2_buffer_sectors = min(ic->log2_buffer_sectors, (__u8)__ffs(ic->metadata_run));

	if (ti->len > ic->provided_data_sectors) {
		r = -EINVAL;
		ti->error = "Not enough provided sectors for requested mapping size";
		goto bad;
	}


	threshold = (__u64)ic->journal_entries * (100 - journal_watermark);
	threshold += 50;
	do_div(threshold, 100);
	ic->free_sectors_threshold = threshold;

	DEBUG_print("initialized:\n");
	DEBUG_print("	integrity_tag_size %u\n", le16_to_cpu(ic->sb->integrity_tag_size));
	DEBUG_print("	journal_entry_size %u\n", ic->journal_entry_size);
	DEBUG_print("	journal_entries_per_sector %u\n", ic->journal_entries_per_sector);
	DEBUG_print("	journal_section_entries %u\n", ic->journal_section_entries);
	DEBUG_print("	journal_section_sectors %u\n", ic->journal_section_sectors);
	DEBUG_print("	journal_sections %u\n", (unsigned)le32_to_cpu(ic->sb->journal_sections));
	DEBUG_print("	journal_entries %u\n", ic->journal_entries);
	DEBUG_print("	log2_interleave_sectors %d\n", ic->sb->log2_interleave_sectors);
	DEBUG_print("	data_device_sectors 0x%llx\n", (unsigned long long)ic->data_device_sectors);
	DEBUG_print("	initial_sectors 0x%x\n", ic->initial_sectors);
	DEBUG_print("	metadata_run 0x%x\n", ic->metadata_run);
	DEBUG_print("	log2_metadata_run %d\n", ic->log2_metadata_run);
	DEBUG_print("	provided_data_sectors 0x%llx (%llu)\n", (unsigned long long)ic->provided_data_sectors,
		    (unsigned long long)ic->provided_data_sectors);
	DEBUG_print("	log2_buffer_sectors %u\n", ic->log2_buffer_sectors);

	if (recalculate && !(ic->sb->flags & cpu_to_le32(SB_FLAG_RECALCULATING))) {
		ic->sb->flags |= cpu_to_le32(SB_FLAG_RECALCULATING);
		ic->sb->recalc_sector = cpu_to_le64(0);
	}

	if (ic->sb->flags & cpu_to_le32(SB_FLAG_RECALCULATING)) {
		if (!ic->internal_hash) {
			r = -EINVAL;
			ti->error = "Recalculate is only valid with internal hash";
			goto bad;
		}
		ic->recalc_wq = alloc_workqueue("dm-intergrity-recalc", WQ_MEM_RECLAIM, 1);
		if (!ic->recalc_wq ) {
			ti->error = "Cannot allocate workqueue";
			r = -ENOMEM;
			goto bad;
		}
		INIT_WORK(&ic->recalc_work, integrity_recalc);
		ic->recalc_buffer = vmalloc(RECALC_SECTORS << SECTOR_SHIFT);
		if (!ic->recalc_buffer) {
			ti->error = "Cannot allocate buffer for recalculating";
			r = -ENOMEM;
			goto bad;
		}
		ic->recalc_tags = kvmalloc_array(RECALC_SECTORS >> ic->sb->log2_sectors_per_block,
						 ic->tag_size, GFP_KERNEL);
		if (!ic->recalc_tags) {
			ti->error = "Cannot allocate tags for recalculating";
			r = -ENOMEM;
			goto bad;
		}
	}

	ic->bufio = dm_bufio_client_create(ic->meta_dev ? ic->meta_dev->bdev : ic->dev->bdev,
			1U << (SECTOR_SHIFT + ic->log2_buffer_sectors), 1, 0, NULL, NULL);
	if (IS_ERR(ic->bufio)) {
		r = PTR_ERR(ic->bufio);
		ti->error = "Cannot initialize dm-bufio";
		ic->bufio = NULL;
		goto bad;
	}
	dm_bufio_set_sector_offset(ic->bufio, ic->start + ic->initial_sectors);

	if (ic->mode != 'R') {
		r = create_journal(ic, &ti->error);
		if (r)
			goto bad;
	}

	if (should_write_sb) {
		int r;

		init_journal(ic, 0, ic->journal_sections, 0);
		r = dm_integrity_failed(ic);
		if (unlikely(r)) {
			ti->error = "Error initializing journal";
			goto bad;
		}
		r = sync_rw_sb(ic, REQ_OP_WRITE, REQ_FUA);
		if (r) {
			ti->error = "Error initializing superblock";
			goto bad;
		}
		ic->just_formatted = true;
	}

	if (!ic->meta_dev) {
		r = dm_set_target_max_io_len(ti, 1U << ic->sb->log2_interleave_sectors);
		if (r)
			goto bad;
	}

	if (!ic->internal_hash)
		dm_integrity_set(ti, ic);

	ti->num_flush_bios = 1;
	ti->flush_supported = true;

	return 0;
bad:
	dm_integrity_dtr(ti);
	return r;
}

static void dm_integrity_dtr(struct dm_target *ti)
{
	struct dm_integrity_c *ic = ti->private;

	BUG_ON(!RB_EMPTY_ROOT(&ic->in_progress));
	BUG_ON(!list_empty(&ic->wait_list));

	if (ic->metadata_wq)
		destroy_workqueue(ic->metadata_wq);
	if (ic->wait_wq)
		destroy_workqueue(ic->wait_wq);
	if (ic->commit_wq)
		destroy_workqueue(ic->commit_wq);
	if (ic->writer_wq)
		destroy_workqueue(ic->writer_wq);
	if (ic->recalc_wq)
		destroy_workqueue(ic->recalc_wq);
	if (ic->recalc_buffer)
		vfree(ic->recalc_buffer);
	if (ic->recalc_tags)
		kvfree(ic->recalc_tags);
	if (ic->bufio)
		dm_bufio_client_destroy(ic->bufio);
	mempool_exit(&ic->journal_io_mempool);
	if (ic->io)
		dm_io_client_destroy(ic->io);
	if (ic->dev)
		dm_put_device(ti, ic->dev);
	if (ic->meta_dev)
		dm_put_device(ti, ic->meta_dev);
	dm_integrity_free_page_list(ic, ic->journal);
	dm_integrity_free_page_list(ic, ic->journal_io);
	dm_integrity_free_page_list(ic, ic->journal_xor);
	if (ic->journal_scatterlist)
		dm_integrity_free_journal_scatterlist(ic, ic->journal_scatterlist);
	if (ic->journal_io_scatterlist)
		dm_integrity_free_journal_scatterlist(ic, ic->journal_io_scatterlist);
	if (ic->sk_requests) {
		unsigned i;

		for (i = 0; i < ic->journal_sections; i++) {
			struct skcipher_request *req = ic->sk_requests[i];
			if (req) {
				kzfree(req->iv);
				skcipher_request_free(req);
			}
		}
		kvfree(ic->sk_requests);
	}
	kvfree(ic->journal_tree);
	if (ic->sb)
		free_pages_exact(ic->sb, SB_SECTORS << SECTOR_SHIFT);

	if (ic->internal_hash)
		crypto_free_shash(ic->internal_hash);
	free_alg(&ic->internal_hash_alg);

	if (ic->journal_crypt)
		crypto_free_skcipher(ic->journal_crypt);
	free_alg(&ic->journal_crypt_alg);

	if (ic->journal_mac)
		crypto_free_shash(ic->journal_mac);
	free_alg(&ic->journal_mac_alg);

	kfree(ic);
}

static struct target_type integrity_target = {
	.name			= "integrity",
	.version		= {1, 2, 0},
	.module			= THIS_MODULE,
	.features		= DM_TARGET_SINGLETON | DM_TARGET_INTEGRITY,
	.ctr			= dm_integrity_ctr,
	.dtr			= dm_integrity_dtr,
	.map			= dm_integrity_map,
	.postsuspend		= dm_integrity_postsuspend,
	.resume			= dm_integrity_resume,
	.status			= dm_integrity_status,
	.iterate_devices	= dm_integrity_iterate_devices,
	.io_hints		= dm_integrity_io_hints,
};

int __init dm_integrity_init(void)
{
	int r;

	journal_io_cache = kmem_cache_create("integrity_journal_io",
					     sizeof(struct journal_io), 0, 0, NULL);
	if (!journal_io_cache) {
		DMERR("can't allocate journal io cache");
		return -ENOMEM;
	}

	r = dm_register_target(&integrity_target);

	if (r < 0)
		DMERR("register failed %d", r);

	return r;
}

void dm_integrity_exit(void)
{
	dm_unregister_target(&integrity_target);
	kmem_cache_destroy(journal_io_cache);
}

module_init(dm_integrity_init);
module_exit(dm_integrity_exit);

MODULE_AUTHOR("Milan Broz");
MODULE_AUTHOR("Mikulas Patocka");
MODULE_DESCRIPTION(DM_NAME " target for integrity tags extension");
MODULE_LICENSE("GPL");
