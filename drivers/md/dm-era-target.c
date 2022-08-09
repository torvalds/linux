// SPDX-License-Identifier: GPL-2.0-only
#include "dm.h"
#include "persistent-data/dm-transaction-manager.h"
#include "persistent-data/dm-bitset.h"
#include "persistent-data/dm-space-map.h"

#include <linux/dm-io.h>
#include <linux/dm-kcopyd.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define DM_MSG_PREFIX "era"

#define SUPERBLOCK_LOCATION 0
#define SUPERBLOCK_MAGIC 2126579579
#define SUPERBLOCK_CSUM_XOR 146538381
#define MIN_ERA_VERSION 1
#define MAX_ERA_VERSION 1
#define INVALID_WRITESET_ROOT SUPERBLOCK_LOCATION
#define MIN_BLOCK_SIZE 8

/*----------------------------------------------------------------
 * Writeset
 *--------------------------------------------------------------*/
struct writeset_metadata {
	uint32_t nr_bits;
	dm_block_t root;
};

struct writeset {
	struct writeset_metadata md;

	/*
	 * An in core copy of the bits to save constantly doing look ups on
	 * disk.
	 */
	unsigned long *bits;
};

/*
 * This does not free off the on disk bitset as this will normally be done
 * after digesting into the era array.
 */
static void writeset_free(struct writeset *ws)
{
	vfree(ws->bits);
	ws->bits = NULL;
}

static int setup_on_disk_bitset(struct dm_disk_bitset *info,
				unsigned nr_bits, dm_block_t *root)
{
	int r;

	r = dm_bitset_empty(info, root);
	if (r)
		return r;

	return dm_bitset_resize(info, *root, 0, nr_bits, false, root);
}

static size_t bitset_size(unsigned nr_bits)
{
	return sizeof(unsigned long) * dm_div_up(nr_bits, BITS_PER_LONG);
}

/*
 * Allocates memory for the in core bitset.
 */
static int writeset_alloc(struct writeset *ws, dm_block_t nr_blocks)
{
	ws->bits = vzalloc(bitset_size(nr_blocks));
	if (!ws->bits) {
		DMERR("%s: couldn't allocate in memory bitset", __func__);
		return -ENOMEM;
	}

	return 0;
}

/*
 * Wipes the in-core bitset, and creates a new on disk bitset.
 */
static int writeset_init(struct dm_disk_bitset *info, struct writeset *ws,
			 dm_block_t nr_blocks)
{
	int r;

	memset(ws->bits, 0, bitset_size(nr_blocks));

	ws->md.nr_bits = nr_blocks;
	r = setup_on_disk_bitset(info, ws->md.nr_bits, &ws->md.root);
	if (r) {
		DMERR("%s: setup_on_disk_bitset failed", __func__);
		return r;
	}

	return 0;
}

static bool writeset_marked(struct writeset *ws, dm_block_t block)
{
	return test_bit(block, ws->bits);
}

static int writeset_marked_on_disk(struct dm_disk_bitset *info,
				   struct writeset_metadata *m, dm_block_t block,
				   bool *result)
{
	dm_block_t old = m->root;

	/*
	 * The bitset was flushed when it was archived, so we know there'll
	 * be no change to the root.
	 */
	int r = dm_bitset_test_bit(info, m->root, block, &m->root, result);
	if (r) {
		DMERR("%s: dm_bitset_test_bit failed", __func__);
		return r;
	}

	BUG_ON(m->root != old);

	return r;
}

/*
 * Returns < 0 on error, 0 if the bit wasn't previously set, 1 if it was.
 */
static int writeset_test_and_set(struct dm_disk_bitset *info,
				 struct writeset *ws, uint32_t block)
{
	int r;

	if (!test_bit(block, ws->bits)) {
		r = dm_bitset_set_bit(info, ws->md.root, block, &ws->md.root);
		if (r) {
			/* FIXME: fail mode */
			return r;
		}

		return 0;
	}

	return 1;
}

/*----------------------------------------------------------------
 * On disk metadata layout
 *--------------------------------------------------------------*/
#define SPACE_MAP_ROOT_SIZE 128
#define UUID_LEN 16

struct writeset_disk {
	__le32 nr_bits;
	__le64 root;
} __packed;

struct superblock_disk {
	__le32 csum;
	__le32 flags;
	__le64 blocknr;

	__u8 uuid[UUID_LEN];
	__le64 magic;
	__le32 version;

	__u8 metadata_space_map_root[SPACE_MAP_ROOT_SIZE];

	__le32 data_block_size;
	__le32 metadata_block_size;
	__le32 nr_blocks;

	__le32 current_era;
	struct writeset_disk current_writeset;

	/*
	 * Only these two fields are valid within the metadata snapshot.
	 */
	__le64 writeset_tree_root;
	__le64 era_array_root;

	__le64 metadata_snap;
} __packed;

/*----------------------------------------------------------------
 * Superblock validation
 *--------------------------------------------------------------*/
static void sb_prepare_for_write(struct dm_block_validator *v,
				 struct dm_block *b,
				 size_t sb_block_size)
{
	struct superblock_disk *disk = dm_block_data(b);

	disk->blocknr = cpu_to_le64(dm_block_location(b));
	disk->csum = cpu_to_le32(dm_bm_checksum(&disk->flags,
						sb_block_size - sizeof(__le32),
						SUPERBLOCK_CSUM_XOR));
}

static int check_metadata_version(struct superblock_disk *disk)
{
	uint32_t metadata_version = le32_to_cpu(disk->version);
	if (metadata_version < MIN_ERA_VERSION || metadata_version > MAX_ERA_VERSION) {
		DMERR("Era metadata version %u found, but only versions between %u and %u supported.",
		      metadata_version, MIN_ERA_VERSION, MAX_ERA_VERSION);
		return -EINVAL;
	}

	return 0;
}

static int sb_check(struct dm_block_validator *v,
		    struct dm_block *b,
		    size_t sb_block_size)
{
	struct superblock_disk *disk = dm_block_data(b);
	__le32 csum_le;

	if (dm_block_location(b) != le64_to_cpu(disk->blocknr)) {
		DMERR("sb_check failed: blocknr %llu: wanted %llu",
		      le64_to_cpu(disk->blocknr),
		      (unsigned long long)dm_block_location(b));
		return -ENOTBLK;
	}

	if (le64_to_cpu(disk->magic) != SUPERBLOCK_MAGIC) {
		DMERR("sb_check failed: magic %llu: wanted %llu",
		      le64_to_cpu(disk->magic),
		      (unsigned long long) SUPERBLOCK_MAGIC);
		return -EILSEQ;
	}

	csum_le = cpu_to_le32(dm_bm_checksum(&disk->flags,
					     sb_block_size - sizeof(__le32),
					     SUPERBLOCK_CSUM_XOR));
	if (csum_le != disk->csum) {
		DMERR("sb_check failed: csum %u: wanted %u",
		      le32_to_cpu(csum_le), le32_to_cpu(disk->csum));
		return -EILSEQ;
	}

	return check_metadata_version(disk);
}

static struct dm_block_validator sb_validator = {
	.name = "superblock",
	.prepare_for_write = sb_prepare_for_write,
	.check = sb_check
};

/*----------------------------------------------------------------
 * Low level metadata handling
 *--------------------------------------------------------------*/
#define DM_ERA_METADATA_BLOCK_SIZE 4096
#define ERA_MAX_CONCURRENT_LOCKS 5

struct era_metadata {
	struct block_device *bdev;
	struct dm_block_manager *bm;
	struct dm_space_map *sm;
	struct dm_transaction_manager *tm;

	dm_block_t block_size;
	uint32_t nr_blocks;

	uint32_t current_era;

	/*
	 * We preallocate 2 writesets.  When an era rolls over we
	 * switch between them. This means the allocation is done at
	 * preresume time, rather than on the io path.
	 */
	struct writeset writesets[2];
	struct writeset *current_writeset;

	dm_block_t writeset_tree_root;
	dm_block_t era_array_root;

	struct dm_disk_bitset bitset_info;
	struct dm_btree_info writeset_tree_info;
	struct dm_array_info era_array_info;

	dm_block_t metadata_snap;

	/*
	 * A flag that is set whenever a writeset has been archived.
	 */
	bool archived_writesets;

	/*
	 * Reading the space map root can fail, so we read it into this
	 * buffer before the superblock is locked and updated.
	 */
	__u8 metadata_space_map_root[SPACE_MAP_ROOT_SIZE];
};

static int superblock_read_lock(struct era_metadata *md,
				struct dm_block **sblock)
{
	return dm_bm_read_lock(md->bm, SUPERBLOCK_LOCATION,
			       &sb_validator, sblock);
}

static int superblock_lock_zero(struct era_metadata *md,
				struct dm_block **sblock)
{
	return dm_bm_write_lock_zero(md->bm, SUPERBLOCK_LOCATION,
				     &sb_validator, sblock);
}

static int superblock_lock(struct era_metadata *md,
			   struct dm_block **sblock)
{
	return dm_bm_write_lock(md->bm, SUPERBLOCK_LOCATION,
				&sb_validator, sblock);
}

/* FIXME: duplication with cache and thin */
static int superblock_all_zeroes(struct dm_block_manager *bm, bool *result)
{
	int r;
	unsigned i;
	struct dm_block *b;
	__le64 *data_le, zero = cpu_to_le64(0);
	unsigned sb_block_size = dm_bm_block_size(bm) / sizeof(__le64);

	/*
	 * We can't use a validator here - it may be all zeroes.
	 */
	r = dm_bm_read_lock(bm, SUPERBLOCK_LOCATION, NULL, &b);
	if (r)
		return r;

	data_le = dm_block_data(b);
	*result = true;
	for (i = 0; i < sb_block_size; i++) {
		if (data_le[i] != zero) {
			*result = false;
			break;
		}
	}

	dm_bm_unlock(b);

	return 0;
}

/*----------------------------------------------------------------*/

static void ws_pack(const struct writeset_metadata *core, struct writeset_disk *disk)
{
	disk->nr_bits = cpu_to_le32(core->nr_bits);
	disk->root = cpu_to_le64(core->root);
}

static void ws_unpack(const struct writeset_disk *disk, struct writeset_metadata *core)
{
	core->nr_bits = le32_to_cpu(disk->nr_bits);
	core->root = le64_to_cpu(disk->root);
}

static void ws_inc(void *context, const void *value, unsigned count)
{
	struct era_metadata *md = context;
	struct writeset_disk ws_d;
	dm_block_t b;
	unsigned i;

	for (i = 0; i < count; i++) {
		memcpy(&ws_d, value + (i * sizeof(ws_d)), sizeof(ws_d));
		b = le64_to_cpu(ws_d.root);
		dm_tm_inc(md->tm, b);
	}
}

static void ws_dec(void *context, const void *value, unsigned count)
{
	struct era_metadata *md = context;
	struct writeset_disk ws_d;
	dm_block_t b;
	unsigned i;

	for (i = 0; i < count; i++) {
		memcpy(&ws_d, value + (i * sizeof(ws_d)), sizeof(ws_d));
		b = le64_to_cpu(ws_d.root);
		dm_bitset_del(&md->bitset_info, b);
	}
}

static int ws_eq(void *context, const void *value1, const void *value2)
{
	return !memcmp(value1, value2, sizeof(struct writeset_disk));
}

/*----------------------------------------------------------------*/

static void setup_writeset_tree_info(struct era_metadata *md)
{
	struct dm_btree_value_type *vt = &md->writeset_tree_info.value_type;
	md->writeset_tree_info.tm = md->tm;
	md->writeset_tree_info.levels = 1;
	vt->context = md;
	vt->size = sizeof(struct writeset_disk);
	vt->inc = ws_inc;
	vt->dec = ws_dec;
	vt->equal = ws_eq;
}

static void setup_era_array_info(struct era_metadata *md)

{
	struct dm_btree_value_type vt;
	vt.context = NULL;
	vt.size = sizeof(__le32);
	vt.inc = NULL;
	vt.dec = NULL;
	vt.equal = NULL;

	dm_array_info_init(&md->era_array_info, md->tm, &vt);
}

static void setup_infos(struct era_metadata *md)
{
	dm_disk_bitset_init(md->tm, &md->bitset_info);
	setup_writeset_tree_info(md);
	setup_era_array_info(md);
}

/*----------------------------------------------------------------*/

static int create_fresh_metadata(struct era_metadata *md)
{
	int r;

	r = dm_tm_create_with_sm(md->bm, SUPERBLOCK_LOCATION,
				 &md->tm, &md->sm);
	if (r < 0) {
		DMERR("dm_tm_create_with_sm failed");
		return r;
	}

	setup_infos(md);

	r = dm_btree_empty(&md->writeset_tree_info, &md->writeset_tree_root);
	if (r) {
		DMERR("couldn't create new writeset tree");
		goto bad;
	}

	r = dm_array_empty(&md->era_array_info, &md->era_array_root);
	if (r) {
		DMERR("couldn't create era array");
		goto bad;
	}

	return 0;

bad:
	dm_sm_destroy(md->sm);
	dm_tm_destroy(md->tm);

	return r;
}

static int save_sm_root(struct era_metadata *md)
{
	int r;
	size_t metadata_len;

	r = dm_sm_root_size(md->sm, &metadata_len);
	if (r < 0)
		return r;

	return dm_sm_copy_root(md->sm, &md->metadata_space_map_root,
			       metadata_len);
}

static void copy_sm_root(struct era_metadata *md, struct superblock_disk *disk)
{
	memcpy(&disk->metadata_space_map_root,
	       &md->metadata_space_map_root,
	       sizeof(md->metadata_space_map_root));
}

/*
 * Writes a superblock, including the static fields that don't get updated
 * with every commit (possible optimisation here).  'md' should be fully
 * constructed when this is called.
 */
static void prepare_superblock(struct era_metadata *md, struct superblock_disk *disk)
{
	disk->magic = cpu_to_le64(SUPERBLOCK_MAGIC);
	disk->flags = cpu_to_le32(0ul);

	/* FIXME: can't keep blanking the uuid (uuid is currently unused though) */
	memset(disk->uuid, 0, sizeof(disk->uuid));
	disk->version = cpu_to_le32(MAX_ERA_VERSION);

	copy_sm_root(md, disk);

	disk->data_block_size = cpu_to_le32(md->block_size);
	disk->metadata_block_size = cpu_to_le32(DM_ERA_METADATA_BLOCK_SIZE >> SECTOR_SHIFT);
	disk->nr_blocks = cpu_to_le32(md->nr_blocks);
	disk->current_era = cpu_to_le32(md->current_era);

	ws_pack(&md->current_writeset->md, &disk->current_writeset);
	disk->writeset_tree_root = cpu_to_le64(md->writeset_tree_root);
	disk->era_array_root = cpu_to_le64(md->era_array_root);
	disk->metadata_snap = cpu_to_le64(md->metadata_snap);
}

static int write_superblock(struct era_metadata *md)
{
	int r;
	struct dm_block *sblock;
	struct superblock_disk *disk;

	r = save_sm_root(md);
	if (r) {
		DMERR("%s: save_sm_root failed", __func__);
		return r;
	}

	r = superblock_lock_zero(md, &sblock);
	if (r)
		return r;

	disk = dm_block_data(sblock);
	prepare_superblock(md, disk);

	return dm_tm_commit(md->tm, sblock);
}

/*
 * Assumes block_size and the infos are set.
 */
static int format_metadata(struct era_metadata *md)
{
	int r;

	r = create_fresh_metadata(md);
	if (r)
		return r;

	r = write_superblock(md);
	if (r) {
		dm_sm_destroy(md->sm);
		dm_tm_destroy(md->tm);
		return r;
	}

	return 0;
}

static int open_metadata(struct era_metadata *md)
{
	int r;
	struct dm_block *sblock;
	struct superblock_disk *disk;

	r = superblock_read_lock(md, &sblock);
	if (r) {
		DMERR("couldn't read_lock superblock");
		return r;
	}

	disk = dm_block_data(sblock);

	/* Verify the data block size hasn't changed */
	if (le32_to_cpu(disk->data_block_size) != md->block_size) {
		DMERR("changing the data block size (from %u to %llu) is not supported",
		      le32_to_cpu(disk->data_block_size), md->block_size);
		r = -EINVAL;
		goto bad;
	}

	r = dm_tm_open_with_sm(md->bm, SUPERBLOCK_LOCATION,
			       disk->metadata_space_map_root,
			       sizeof(disk->metadata_space_map_root),
			       &md->tm, &md->sm);
	if (r) {
		DMERR("dm_tm_open_with_sm failed");
		goto bad;
	}

	setup_infos(md);

	md->nr_blocks = le32_to_cpu(disk->nr_blocks);
	md->current_era = le32_to_cpu(disk->current_era);

	ws_unpack(&disk->current_writeset, &md->current_writeset->md);
	md->writeset_tree_root = le64_to_cpu(disk->writeset_tree_root);
	md->era_array_root = le64_to_cpu(disk->era_array_root);
	md->metadata_snap = le64_to_cpu(disk->metadata_snap);
	md->archived_writesets = true;

	dm_bm_unlock(sblock);

	return 0;

bad:
	dm_bm_unlock(sblock);
	return r;
}

static int open_or_format_metadata(struct era_metadata *md,
				   bool may_format)
{
	int r;
	bool unformatted = false;

	r = superblock_all_zeroes(md->bm, &unformatted);
	if (r)
		return r;

	if (unformatted)
		return may_format ? format_metadata(md) : -EPERM;

	return open_metadata(md);
}

static int create_persistent_data_objects(struct era_metadata *md,
					  bool may_format)
{
	int r;

	md->bm = dm_block_manager_create(md->bdev, DM_ERA_METADATA_BLOCK_SIZE,
					 ERA_MAX_CONCURRENT_LOCKS);
	if (IS_ERR(md->bm)) {
		DMERR("could not create block manager");
		return PTR_ERR(md->bm);
	}

	r = open_or_format_metadata(md, may_format);
	if (r)
		dm_block_manager_destroy(md->bm);

	return r;
}

static void destroy_persistent_data_objects(struct era_metadata *md)
{
	dm_sm_destroy(md->sm);
	dm_tm_destroy(md->tm);
	dm_block_manager_destroy(md->bm);
}

/*
 * This waits until all era_map threads have picked up the new filter.
 */
static void swap_writeset(struct era_metadata *md, struct writeset *new_writeset)
{
	rcu_assign_pointer(md->current_writeset, new_writeset);
	synchronize_rcu();
}

/*----------------------------------------------------------------
 * Writesets get 'digested' into the main era array.
 *
 * We're using a coroutine here so the worker thread can do the digestion,
 * thus avoiding synchronisation of the metadata.  Digesting a whole
 * writeset in one go would cause too much latency.
 *--------------------------------------------------------------*/
struct digest {
	uint32_t era;
	unsigned nr_bits, current_bit;
	struct writeset_metadata writeset;
	__le32 value;
	struct dm_disk_bitset info;

	int (*step)(struct era_metadata *, struct digest *);
};

static int metadata_digest_lookup_writeset(struct era_metadata *md,
					   struct digest *d);

static int metadata_digest_remove_writeset(struct era_metadata *md,
					   struct digest *d)
{
	int r;
	uint64_t key = d->era;

	r = dm_btree_remove(&md->writeset_tree_info, md->writeset_tree_root,
			    &key, &md->writeset_tree_root);
	if (r) {
		DMERR("%s: dm_btree_remove failed", __func__);
		return r;
	}

	d->step = metadata_digest_lookup_writeset;
	return 0;
}

#define INSERTS_PER_STEP 100

static int metadata_digest_transcribe_writeset(struct era_metadata *md,
					       struct digest *d)
{
	int r;
	bool marked;
	unsigned b, e = min(d->current_bit + INSERTS_PER_STEP, d->nr_bits);

	for (b = d->current_bit; b < e; b++) {
		r = writeset_marked_on_disk(&d->info, &d->writeset, b, &marked);
		if (r) {
			DMERR("%s: writeset_marked_on_disk failed", __func__);
			return r;
		}

		if (!marked)
			continue;

		__dm_bless_for_disk(&d->value);
		r = dm_array_set_value(&md->era_array_info, md->era_array_root,
				       b, &d->value, &md->era_array_root);
		if (r) {
			DMERR("%s: dm_array_set_value failed", __func__);
			return r;
		}
	}

	if (b == d->nr_bits)
		d->step = metadata_digest_remove_writeset;
	else
		d->current_bit = b;

	return 0;
}

static int metadata_digest_lookup_writeset(struct era_metadata *md,
					   struct digest *d)
{
	int r;
	uint64_t key;
	struct writeset_disk disk;

	r = dm_btree_find_lowest_key(&md->writeset_tree_info,
				     md->writeset_tree_root, &key);
	if (r < 0)
		return r;

	d->era = key;

	r = dm_btree_lookup(&md->writeset_tree_info,
			    md->writeset_tree_root, &key, &disk);
	if (r) {
		if (r == -ENODATA) {
			d->step = NULL;
			return 0;
		}

		DMERR("%s: dm_btree_lookup failed", __func__);
		return r;
	}

	ws_unpack(&disk, &d->writeset);
	d->value = cpu_to_le32(key);

	/*
	 * We initialise another bitset info to avoid any caching side effects
	 * with the previous one.
	 */
	dm_disk_bitset_init(md->tm, &d->info);

	d->nr_bits = min(d->writeset.nr_bits, md->nr_blocks);
	d->current_bit = 0;
	d->step = metadata_digest_transcribe_writeset;

	return 0;
}

static int metadata_digest_start(struct era_metadata *md, struct digest *d)
{
	if (d->step)
		return 0;

	memset(d, 0, sizeof(*d));
	d->step = metadata_digest_lookup_writeset;

	return 0;
}

/*----------------------------------------------------------------
 * High level metadata interface.  Target methods should use these, and not
 * the lower level ones.
 *--------------------------------------------------------------*/
static struct era_metadata *metadata_open(struct block_device *bdev,
					  sector_t block_size,
					  bool may_format)
{
	int r;
	struct era_metadata *md = kzalloc(sizeof(*md), GFP_KERNEL);

	if (!md)
		return NULL;

	md->bdev = bdev;
	md->block_size = block_size;

	md->writesets[0].md.root = INVALID_WRITESET_ROOT;
	md->writesets[1].md.root = INVALID_WRITESET_ROOT;
	md->current_writeset = &md->writesets[0];

	r = create_persistent_data_objects(md, may_format);
	if (r) {
		kfree(md);
		return ERR_PTR(r);
	}

	return md;
}

static void metadata_close(struct era_metadata *md)
{
	writeset_free(&md->writesets[0]);
	writeset_free(&md->writesets[1]);
	destroy_persistent_data_objects(md);
	kfree(md);
}

static bool valid_nr_blocks(dm_block_t n)
{
	/*
	 * dm_bitset restricts us to 2^32.  test_bit & co. restrict us
	 * further to 2^31 - 1
	 */
	return n < (1ull << 31);
}

static int metadata_resize(struct era_metadata *md, void *arg)
{
	int r;
	dm_block_t *new_size = arg;
	__le32 value;

	if (!valid_nr_blocks(*new_size)) {
		DMERR("Invalid number of origin blocks %llu",
		      (unsigned long long) *new_size);
		return -EINVAL;
	}

	writeset_free(&md->writesets[0]);
	writeset_free(&md->writesets[1]);

	r = writeset_alloc(&md->writesets[0], *new_size);
	if (r) {
		DMERR("%s: writeset_alloc failed for writeset 0", __func__);
		return r;
	}

	r = writeset_alloc(&md->writesets[1], *new_size);
	if (r) {
		DMERR("%s: writeset_alloc failed for writeset 1", __func__);
		writeset_free(&md->writesets[0]);
		return r;
	}

	value = cpu_to_le32(0u);
	__dm_bless_for_disk(&value);
	r = dm_array_resize(&md->era_array_info, md->era_array_root,
			    md->nr_blocks, *new_size,
			    &value, &md->era_array_root);
	if (r) {
		DMERR("%s: dm_array_resize failed", __func__);
		writeset_free(&md->writesets[0]);
		writeset_free(&md->writesets[1]);
		return r;
	}

	md->nr_blocks = *new_size;
	return 0;
}

static int metadata_era_archive(struct era_metadata *md)
{
	int r;
	uint64_t keys[1];
	struct writeset_disk value;

	r = dm_bitset_flush(&md->bitset_info, md->current_writeset->md.root,
			    &md->current_writeset->md.root);
	if (r) {
		DMERR("%s: dm_bitset_flush failed", __func__);
		return r;
	}

	ws_pack(&md->current_writeset->md, &value);

	keys[0] = md->current_era;
	__dm_bless_for_disk(&value);
	r = dm_btree_insert(&md->writeset_tree_info, md->writeset_tree_root,
			    keys, &value, &md->writeset_tree_root);
	if (r) {
		DMERR("%s: couldn't insert writeset into btree", __func__);
		/* FIXME: fail mode */
		return r;
	}

	md->current_writeset->md.root = INVALID_WRITESET_ROOT;
	md->archived_writesets = true;

	return 0;
}

static struct writeset *next_writeset(struct era_metadata *md)
{
	return (md->current_writeset == &md->writesets[0]) ?
		&md->writesets[1] : &md->writesets[0];
}

static int metadata_new_era(struct era_metadata *md)
{
	int r;
	struct writeset *new_writeset = next_writeset(md);

	r = writeset_init(&md->bitset_info, new_writeset, md->nr_blocks);
	if (r) {
		DMERR("%s: writeset_init failed", __func__);
		return r;
	}

	swap_writeset(md, new_writeset);
	md->current_era++;

	return 0;
}

static int metadata_era_rollover(struct era_metadata *md)
{
	int r;

	if (md->current_writeset->md.root != INVALID_WRITESET_ROOT) {
		r = metadata_era_archive(md);
		if (r) {
			DMERR("%s: metadata_archive_era failed", __func__);
			/* FIXME: fail mode? */
			return r;
		}
	}

	r = metadata_new_era(md);
	if (r) {
		DMERR("%s: new era failed", __func__);
		/* FIXME: fail mode */
		return r;
	}

	return 0;
}

static bool metadata_current_marked(struct era_metadata *md, dm_block_t block)
{
	bool r;
	struct writeset *ws;

	rcu_read_lock();
	ws = rcu_dereference(md->current_writeset);
	r = writeset_marked(ws, block);
	rcu_read_unlock();

	return r;
}

static int metadata_commit(struct era_metadata *md)
{
	int r;
	struct dm_block *sblock;

	if (md->current_writeset->md.root != INVALID_WRITESET_ROOT) {
		r = dm_bitset_flush(&md->bitset_info, md->current_writeset->md.root,
				    &md->current_writeset->md.root);
		if (r) {
			DMERR("%s: bitset flush failed", __func__);
			return r;
		}
	}

	r = dm_tm_pre_commit(md->tm);
	if (r) {
		DMERR("%s: pre commit failed", __func__);
		return r;
	}

	r = save_sm_root(md);
	if (r) {
		DMERR("%s: save_sm_root failed", __func__);
		return r;
	}

	r = superblock_lock(md, &sblock);
	if (r) {
		DMERR("%s: superblock lock failed", __func__);
		return r;
	}

	prepare_superblock(md, dm_block_data(sblock));

	return dm_tm_commit(md->tm, sblock);
}

static int metadata_checkpoint(struct era_metadata *md)
{
	/*
	 * For now we just rollover, but later I want to put a check in to
	 * avoid this if the filter is still pretty fresh.
	 */
	return metadata_era_rollover(md);
}

/*
 * Metadata snapshots allow userland to access era data.
 */
static int metadata_take_snap(struct era_metadata *md)
{
	int r, inc;
	struct dm_block *clone;

	if (md->metadata_snap != SUPERBLOCK_LOCATION) {
		DMERR("%s: metadata snapshot already exists", __func__);
		return -EINVAL;
	}

	r = metadata_era_rollover(md);
	if (r) {
		DMERR("%s: era rollover failed", __func__);
		return r;
	}

	r = metadata_commit(md);
	if (r) {
		DMERR("%s: pre commit failed", __func__);
		return r;
	}

	r = dm_sm_inc_block(md->sm, SUPERBLOCK_LOCATION);
	if (r) {
		DMERR("%s: couldn't increment superblock", __func__);
		return r;
	}

	r = dm_tm_shadow_block(md->tm, SUPERBLOCK_LOCATION,
			       &sb_validator, &clone, &inc);
	if (r) {
		DMERR("%s: couldn't shadow superblock", __func__);
		dm_sm_dec_block(md->sm, SUPERBLOCK_LOCATION);
		return r;
	}
	BUG_ON(!inc);

	r = dm_sm_inc_block(md->sm, md->writeset_tree_root);
	if (r) {
		DMERR("%s: couldn't inc writeset tree root", __func__);
		dm_tm_unlock(md->tm, clone);
		return r;
	}

	r = dm_sm_inc_block(md->sm, md->era_array_root);
	if (r) {
		DMERR("%s: couldn't inc era tree root", __func__);
		dm_sm_dec_block(md->sm, md->writeset_tree_root);
		dm_tm_unlock(md->tm, clone);
		return r;
	}

	md->metadata_snap = dm_block_location(clone);

	dm_tm_unlock(md->tm, clone);

	return 0;
}

static int metadata_drop_snap(struct era_metadata *md)
{
	int r;
	dm_block_t location;
	struct dm_block *clone;
	struct superblock_disk *disk;

	if (md->metadata_snap == SUPERBLOCK_LOCATION) {
		DMERR("%s: no snap to drop", __func__);
		return -EINVAL;
	}

	r = dm_tm_read_lock(md->tm, md->metadata_snap, &sb_validator, &clone);
	if (r) {
		DMERR("%s: couldn't read lock superblock clone", __func__);
		return r;
	}

	/*
	 * Whatever happens now we'll commit with no record of the metadata
	 * snap.
	 */
	md->metadata_snap = SUPERBLOCK_LOCATION;

	disk = dm_block_data(clone);
	r = dm_btree_del(&md->writeset_tree_info,
			 le64_to_cpu(disk->writeset_tree_root));
	if (r) {
		DMERR("%s: error deleting writeset tree clone", __func__);
		dm_tm_unlock(md->tm, clone);
		return r;
	}

	r = dm_array_del(&md->era_array_info, le64_to_cpu(disk->era_array_root));
	if (r) {
		DMERR("%s: error deleting era array clone", __func__);
		dm_tm_unlock(md->tm, clone);
		return r;
	}

	location = dm_block_location(clone);
	dm_tm_unlock(md->tm, clone);

	return dm_sm_dec_block(md->sm, location);
}

struct metadata_stats {
	dm_block_t used;
	dm_block_t total;
	dm_block_t snap;
	uint32_t era;
};

static int metadata_get_stats(struct era_metadata *md, void *ptr)
{
	int r;
	struct metadata_stats *s = ptr;
	dm_block_t nr_free, nr_total;

	r = dm_sm_get_nr_free(md->sm, &nr_free);
	if (r) {
		DMERR("dm_sm_get_nr_free returned %d", r);
		return r;
	}

	r = dm_sm_get_nr_blocks(md->sm, &nr_total);
	if (r) {
		DMERR("dm_pool_get_metadata_dev_size returned %d", r);
		return r;
	}

	s->used = nr_total - nr_free;
	s->total = nr_total;
	s->snap = md->metadata_snap;
	s->era = md->current_era;

	return 0;
}

/*----------------------------------------------------------------*/

struct era {
	struct dm_target *ti;

	struct dm_dev *metadata_dev;
	struct dm_dev *origin_dev;

	dm_block_t nr_blocks;
	uint32_t sectors_per_block;
	int sectors_per_block_shift;
	struct era_metadata *md;

	struct workqueue_struct *wq;
	struct work_struct worker;

	spinlock_t deferred_lock;
	struct bio_list deferred_bios;

	spinlock_t rpc_lock;
	struct list_head rpc_calls;

	struct digest digest;
	atomic_t suspended;
};

struct rpc {
	struct list_head list;

	int (*fn0)(struct era_metadata *);
	int (*fn1)(struct era_metadata *, void *);
	void *arg;
	int result;

	struct completion complete;
};

/*----------------------------------------------------------------
 * Remapping.
 *---------------------------------------------------------------*/
static bool block_size_is_power_of_two(struct era *era)
{
	return era->sectors_per_block_shift >= 0;
}

static dm_block_t get_block(struct era *era, struct bio *bio)
{
	sector_t block_nr = bio->bi_iter.bi_sector;

	if (!block_size_is_power_of_two(era))
		(void) sector_div(block_nr, era->sectors_per_block);
	else
		block_nr >>= era->sectors_per_block_shift;

	return block_nr;
}

static void remap_to_origin(struct era *era, struct bio *bio)
{
	bio_set_dev(bio, era->origin_dev->bdev);
}

/*----------------------------------------------------------------
 * Worker thread
 *--------------------------------------------------------------*/
static void wake_worker(struct era *era)
{
	if (!atomic_read(&era->suspended))
		queue_work(era->wq, &era->worker);
}

static void process_old_eras(struct era *era)
{
	int r;

	if (!era->digest.step)
		return;

	r = era->digest.step(era->md, &era->digest);
	if (r < 0) {
		DMERR("%s: digest step failed, stopping digestion", __func__);
		era->digest.step = NULL;

	} else if (era->digest.step)
		wake_worker(era);
}

static void process_deferred_bios(struct era *era)
{
	int r;
	struct bio_list deferred_bios, marked_bios;
	struct bio *bio;
	struct blk_plug plug;
	bool commit_needed = false;
	bool failed = false;
	struct writeset *ws = era->md->current_writeset;

	bio_list_init(&deferred_bios);
	bio_list_init(&marked_bios);

	spin_lock(&era->deferred_lock);
	bio_list_merge(&deferred_bios, &era->deferred_bios);
	bio_list_init(&era->deferred_bios);
	spin_unlock(&era->deferred_lock);

	if (bio_list_empty(&deferred_bios))
		return;

	while ((bio = bio_list_pop(&deferred_bios))) {
		r = writeset_test_and_set(&era->md->bitset_info, ws,
					  get_block(era, bio));
		if (r < 0) {
			/*
			 * This is bad news, we need to rollback.
			 * FIXME: finish.
			 */
			failed = true;
		} else if (r == 0)
			commit_needed = true;

		bio_list_add(&marked_bios, bio);
	}

	if (commit_needed) {
		r = metadata_commit(era->md);
		if (r)
			failed = true;
	}

	if (failed)
		while ((bio = bio_list_pop(&marked_bios)))
			bio_io_error(bio);
	else {
		blk_start_plug(&plug);
		while ((bio = bio_list_pop(&marked_bios))) {
			/*
			 * Only update the in-core writeset if the on-disk one
			 * was updated too.
			 */
			if (commit_needed)
				set_bit(get_block(era, bio), ws->bits);
			submit_bio_noacct(bio);
		}
		blk_finish_plug(&plug);
	}
}

static void process_rpc_calls(struct era *era)
{
	int r;
	bool need_commit = false;
	struct list_head calls;
	struct rpc *rpc, *tmp;

	INIT_LIST_HEAD(&calls);
	spin_lock(&era->rpc_lock);
	list_splice_init(&era->rpc_calls, &calls);
	spin_unlock(&era->rpc_lock);

	list_for_each_entry_safe(rpc, tmp, &calls, list) {
		rpc->result = rpc->fn0 ? rpc->fn0(era->md) : rpc->fn1(era->md, rpc->arg);
		need_commit = true;
	}

	if (need_commit) {
		r = metadata_commit(era->md);
		if (r)
			list_for_each_entry_safe(rpc, tmp, &calls, list)
				rpc->result = r;
	}

	list_for_each_entry_safe(rpc, tmp, &calls, list)
		complete(&rpc->complete);
}

static void kick_off_digest(struct era *era)
{
	if (era->md->archived_writesets) {
		era->md->archived_writesets = false;
		metadata_digest_start(era->md, &era->digest);
	}
}

static void do_work(struct work_struct *ws)
{
	struct era *era = container_of(ws, struct era, worker);

	kick_off_digest(era);
	process_old_eras(era);
	process_deferred_bios(era);
	process_rpc_calls(era);
}

static void defer_bio(struct era *era, struct bio *bio)
{
	spin_lock(&era->deferred_lock);
	bio_list_add(&era->deferred_bios, bio);
	spin_unlock(&era->deferred_lock);

	wake_worker(era);
}

/*
 * Make an rpc call to the worker to change the metadata.
 */
static int perform_rpc(struct era *era, struct rpc *rpc)
{
	rpc->result = 0;
	init_completion(&rpc->complete);

	spin_lock(&era->rpc_lock);
	list_add(&rpc->list, &era->rpc_calls);
	spin_unlock(&era->rpc_lock);

	wake_worker(era);
	wait_for_completion(&rpc->complete);

	return rpc->result;
}

static int in_worker0(struct era *era, int (*fn)(struct era_metadata *))
{
	struct rpc rpc;
	rpc.fn0 = fn;
	rpc.fn1 = NULL;

	return perform_rpc(era, &rpc);
}

static int in_worker1(struct era *era,
		      int (*fn)(struct era_metadata *, void *), void *arg)
{
	struct rpc rpc;
	rpc.fn0 = NULL;
	rpc.fn1 = fn;
	rpc.arg = arg;

	return perform_rpc(era, &rpc);
}

static void start_worker(struct era *era)
{
	atomic_set(&era->suspended, 0);
}

static void stop_worker(struct era *era)
{
	atomic_set(&era->suspended, 1);
	flush_workqueue(era->wq);
}

/*----------------------------------------------------------------
 * Target methods
 *--------------------------------------------------------------*/
static void era_destroy(struct era *era)
{
	if (era->md)
		metadata_close(era->md);

	if (era->wq)
		destroy_workqueue(era->wq);

	if (era->origin_dev)
		dm_put_device(era->ti, era->origin_dev);

	if (era->metadata_dev)
		dm_put_device(era->ti, era->metadata_dev);

	kfree(era);
}

static dm_block_t calc_nr_blocks(struct era *era)
{
	return dm_sector_div_up(era->ti->len, era->sectors_per_block);
}

static bool valid_block_size(dm_block_t block_size)
{
	bool greater_than_zero = block_size > 0;
	bool multiple_of_min_block_size = (block_size & (MIN_BLOCK_SIZE - 1)) == 0;

	return greater_than_zero && multiple_of_min_block_size;
}

/*
 * <metadata dev> <data dev> <data block size (sectors)>
 */
static int era_ctr(struct dm_target *ti, unsigned argc, char **argv)
{
	int r;
	char dummy;
	struct era *era;
	struct era_metadata *md;

	if (argc != 3) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	era = kzalloc(sizeof(*era), GFP_KERNEL);
	if (!era) {
		ti->error = "Error allocating era structure";
		return -ENOMEM;
	}

	era->ti = ti;

	r = dm_get_device(ti, argv[0], FMODE_READ | FMODE_WRITE, &era->metadata_dev);
	if (r) {
		ti->error = "Error opening metadata device";
		era_destroy(era);
		return -EINVAL;
	}

	r = dm_get_device(ti, argv[1], FMODE_READ | FMODE_WRITE, &era->origin_dev);
	if (r) {
		ti->error = "Error opening data device";
		era_destroy(era);
		return -EINVAL;
	}

	r = sscanf(argv[2], "%u%c", &era->sectors_per_block, &dummy);
	if (r != 1) {
		ti->error = "Error parsing block size";
		era_destroy(era);
		return -EINVAL;
	}

	r = dm_set_target_max_io_len(ti, era->sectors_per_block);
	if (r) {
		ti->error = "could not set max io len";
		era_destroy(era);
		return -EINVAL;
	}

	if (!valid_block_size(era->sectors_per_block)) {
		ti->error = "Invalid block size";
		era_destroy(era);
		return -EINVAL;
	}
	if (era->sectors_per_block & (era->sectors_per_block - 1))
		era->sectors_per_block_shift = -1;
	else
		era->sectors_per_block_shift = __ffs(era->sectors_per_block);

	md = metadata_open(era->metadata_dev->bdev, era->sectors_per_block, true);
	if (IS_ERR(md)) {
		ti->error = "Error reading metadata";
		era_destroy(era);
		return PTR_ERR(md);
	}
	era->md = md;

	era->wq = alloc_ordered_workqueue("dm-" DM_MSG_PREFIX, WQ_MEM_RECLAIM);
	if (!era->wq) {
		ti->error = "could not create workqueue for metadata object";
		era_destroy(era);
		return -ENOMEM;
	}
	INIT_WORK(&era->worker, do_work);

	spin_lock_init(&era->deferred_lock);
	bio_list_init(&era->deferred_bios);

	spin_lock_init(&era->rpc_lock);
	INIT_LIST_HEAD(&era->rpc_calls);

	ti->private = era;
	ti->num_flush_bios = 1;
	ti->flush_supported = true;

	ti->num_discard_bios = 1;

	return 0;
}

static void era_dtr(struct dm_target *ti)
{
	era_destroy(ti->private);
}

static int era_map(struct dm_target *ti, struct bio *bio)
{
	struct era *era = ti->private;
	dm_block_t block = get_block(era, bio);

	/*
	 * All bios get remapped to the origin device.  We do this now, but
	 * it may not get issued until later.  Depending on whether the
	 * block is marked in this era.
	 */
	remap_to_origin(era, bio);

	/*
	 * REQ_PREFLUSH bios carry no data, so we're not interested in them.
	 */
	if (!(bio->bi_opf & REQ_PREFLUSH) &&
	    (bio_data_dir(bio) == WRITE) &&
	    !metadata_current_marked(era->md, block)) {
		defer_bio(era, bio);
		return DM_MAPIO_SUBMITTED;
	}

	return DM_MAPIO_REMAPPED;
}

static void era_postsuspend(struct dm_target *ti)
{
	int r;
	struct era *era = ti->private;

	r = in_worker0(era, metadata_era_archive);
	if (r) {
		DMERR("%s: couldn't archive current era", __func__);
		/* FIXME: fail mode */
	}

	stop_worker(era);
}

static int era_preresume(struct dm_target *ti)
{
	int r;
	struct era *era = ti->private;
	dm_block_t new_size = calc_nr_blocks(era);

	if (era->nr_blocks != new_size) {
		r = metadata_resize(era->md, &new_size);
		if (r) {
			DMERR("%s: metadata_resize failed", __func__);
			return r;
		}

		r = metadata_commit(era->md);
		if (r) {
			DMERR("%s: metadata_commit failed", __func__);
			return r;
		}

		era->nr_blocks = new_size;
	}

	start_worker(era);

	r = in_worker0(era, metadata_era_rollover);
	if (r) {
		DMERR("%s: metadata_era_rollover failed", __func__);
		return r;
	}

	return 0;
}

/*
 * Status format:
 *
 * <metadata block size> <#used metadata blocks>/<#total metadata blocks>
 * <current era> <held metadata root | '-'>
 */
static void era_status(struct dm_target *ti, status_type_t type,
		       unsigned status_flags, char *result, unsigned maxlen)
{
	int r;
	struct era *era = ti->private;
	ssize_t sz = 0;
	struct metadata_stats stats;
	char buf[BDEVNAME_SIZE];

	switch (type) {
	case STATUSTYPE_INFO:
		r = in_worker1(era, metadata_get_stats, &stats);
		if (r)
			goto err;

		DMEMIT("%u %llu/%llu %u",
		       (unsigned) (DM_ERA_METADATA_BLOCK_SIZE >> SECTOR_SHIFT),
		       (unsigned long long) stats.used,
		       (unsigned long long) stats.total,
		       (unsigned) stats.era);

		if (stats.snap != SUPERBLOCK_LOCATION)
			DMEMIT(" %llu", stats.snap);
		else
			DMEMIT(" -");
		break;

	case STATUSTYPE_TABLE:
		format_dev_t(buf, era->metadata_dev->bdev->bd_dev);
		DMEMIT("%s ", buf);
		format_dev_t(buf, era->origin_dev->bdev->bd_dev);
		DMEMIT("%s %u", buf, era->sectors_per_block);
		break;

	case STATUSTYPE_IMA:
		*result = '\0';
		break;
	}

	return;

err:
	DMEMIT("Error");
}

static int era_message(struct dm_target *ti, unsigned argc, char **argv,
		       char *result, unsigned maxlen)
{
	struct era *era = ti->private;

	if (argc != 1) {
		DMERR("incorrect number of message arguments");
		return -EINVAL;
	}

	if (!strcasecmp(argv[0], "checkpoint"))
		return in_worker0(era, metadata_checkpoint);

	if (!strcasecmp(argv[0], "take_metadata_snap"))
		return in_worker0(era, metadata_take_snap);

	if (!strcasecmp(argv[0], "drop_metadata_snap"))
		return in_worker0(era, metadata_drop_snap);

	DMERR("unsupported message '%s'", argv[0]);
	return -EINVAL;
}

static sector_t get_dev_size(struct dm_dev *dev)
{
	return bdev_nr_sectors(dev->bdev);
}

static int era_iterate_devices(struct dm_target *ti,
			       iterate_devices_callout_fn fn, void *data)
{
	struct era *era = ti->private;
	return fn(ti, era->origin_dev, 0, get_dev_size(era->origin_dev), data);
}

static void era_io_hints(struct dm_target *ti, struct queue_limits *limits)
{
	struct era *era = ti->private;
	uint64_t io_opt_sectors = limits->io_opt >> SECTOR_SHIFT;

	/*
	 * If the system-determined stacked limits are compatible with the
	 * era device's blocksize (io_opt is a factor) do not override them.
	 */
	if (io_opt_sectors < era->sectors_per_block ||
	    do_div(io_opt_sectors, era->sectors_per_block)) {
		blk_limits_io_min(limits, 0);
		blk_limits_io_opt(limits, era->sectors_per_block << SECTOR_SHIFT);
	}
}

/*----------------------------------------------------------------*/

static struct target_type era_target = {
	.name = "era",
	.version = {1, 0, 0},
	.module = THIS_MODULE,
	.ctr = era_ctr,
	.dtr = era_dtr,
	.map = era_map,
	.postsuspend = era_postsuspend,
	.preresume = era_preresume,
	.status = era_status,
	.message = era_message,
	.iterate_devices = era_iterate_devices,
	.io_hints = era_io_hints
};

static int __init dm_era_init(void)
{
	int r;

	r = dm_register_target(&era_target);
	if (r) {
		DMERR("era target registration failed: %d", r);
		return r;
	}

	return 0;
}

static void __exit dm_era_exit(void)
{
	dm_unregister_target(&era_target);
}

module_init(dm_era_init);
module_exit(dm_era_exit);

MODULE_DESCRIPTION(DM_NAME " era target");
MODULE_AUTHOR("Joe Thornber <ejt@redhat.com>");
MODULE_LICENSE("GPL");
