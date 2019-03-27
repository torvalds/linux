///////////////////////////////////////////////////////////////////////////////
//
/// \file       index.c
/// \brief      Handling of .xz Indexes and some other Stream information
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "index.h"
#include "stream_flags_common.h"


/// \brief      How many Records to allocate at once
///
/// This should be big enough to avoid making lots of tiny allocations
/// but small enough to avoid too much unused memory at once.
#define INDEX_GROUP_SIZE 512


/// \brief      How many Records can be allocated at once at maximum
#define PREALLOC_MAX ((SIZE_MAX - sizeof(index_group)) / sizeof(index_record))


/// \brief      Base structure for index_stream and index_group structures
typedef struct index_tree_node_s index_tree_node;
struct index_tree_node_s {
	/// Uncompressed start offset of this Stream (relative to the
	/// beginning of the file) or Block (relative to the beginning
	/// of the Stream)
	lzma_vli uncompressed_base;

	/// Compressed start offset of this Stream or Block
	lzma_vli compressed_base;

	index_tree_node *parent;
	index_tree_node *left;
	index_tree_node *right;
};


/// \brief      AVL tree to hold index_stream or index_group structures
typedef struct {
	/// Root node
	index_tree_node *root;

	/// Leftmost node. Since the tree will be filled sequentially,
	/// this won't change after the first node has been added to
	/// the tree.
	index_tree_node *leftmost;

	/// The rightmost node in the tree. Since the tree is filled
	/// sequentially, this is always the node where to add the new data.
	index_tree_node *rightmost;

	/// Number of nodes in the tree
	uint32_t count;

} index_tree;


typedef struct {
	lzma_vli uncompressed_sum;
	lzma_vli unpadded_sum;
} index_record;


typedef struct {
	/// Every Record group is part of index_stream.groups tree.
	index_tree_node node;

	/// Number of Blocks in this Stream before this group.
	lzma_vli number_base;

	/// Number of Records that can be put in records[].
	size_t allocated;

	/// Index of the last Record in use.
	size_t last;

	/// The sizes in this array are stored as cumulative sums relative
	/// to the beginning of the Stream. This makes it possible to
	/// use binary search in lzma_index_locate().
	///
	/// Note that the cumulative summing is done specially for
	/// unpadded_sum: The previous value is rounded up to the next
	/// multiple of four before adding the Unpadded Size of the new
	/// Block. The total encoded size of the Blocks in the Stream
	/// is records[last].unpadded_sum in the last Record group of
	/// the Stream.
	///
	/// For example, if the Unpadded Sizes are 39, 57, and 81, the
	/// stored values are 39, 97 (40 + 57), and 181 (100 + 181).
	/// The total encoded size of these Blocks is 184.
	///
	/// This is a flexible array, because it makes easy to optimize
	/// memory usage in case someone concatenates many Streams that
	/// have only one or few Blocks.
	index_record records[];

} index_group;


typedef struct {
	/// Every index_stream is a node in the tree of Sreams.
	index_tree_node node;

	/// Number of this Stream (first one is 1)
	uint32_t number;

	/// Total number of Blocks before this Stream
	lzma_vli block_number_base;

	/// Record groups of this Stream are stored in a tree.
	/// It's a T-tree with AVL-tree balancing. There are
	/// INDEX_GROUP_SIZE Records per node by default.
	/// This keeps the number of memory allocations reasonable
	/// and finding a Record is fast.
	index_tree groups;

	/// Number of Records in this Stream
	lzma_vli record_count;

	/// Size of the List of Records field in this Stream. This is used
	/// together with record_count to calculate the size of the Index
	/// field and thus the total size of the Stream.
	lzma_vli index_list_size;

	/// Stream Flags of this Stream. This is meaningful only if
	/// the Stream Flags have been told us with lzma_index_stream_flags().
	/// Initially stream_flags.version is set to UINT32_MAX to indicate
	/// that the Stream Flags are unknown.
	lzma_stream_flags stream_flags;

	/// Amount of Stream Padding after this Stream. This defaults to
	/// zero and can be set with lzma_index_stream_padding().
	lzma_vli stream_padding;

} index_stream;


struct lzma_index_s {
	/// AVL-tree containing the Stream(s). Often there is just one
	/// Stream, but using a tree keeps lookups fast even when there
	/// are many concatenated Streams.
	index_tree streams;

	/// Uncompressed size of all the Blocks in the Stream(s)
	lzma_vli uncompressed_size;

	/// Total size of all the Blocks in the Stream(s)
	lzma_vli total_size;

	/// Total number of Records in all Streams in this lzma_index
	lzma_vli record_count;

	/// Size of the List of Records field if all the Streams in this
	/// lzma_index were packed into a single Stream (makes it simpler to
	/// take many .xz files and combine them into a single Stream).
	///
	/// This value together with record_count is needed to calculate
	/// Backward Size that is stored into Stream Footer.
	lzma_vli index_list_size;

	/// How many Records to allocate at once in lzma_index_append().
	/// This defaults to INDEX_GROUP_SIZE but can be overriden with
	/// lzma_index_prealloc().
	size_t prealloc;

	/// Bitmask indicating what integrity check types have been used
	/// as set by lzma_index_stream_flags(). The bit of the last Stream
	/// is not included here, since it is possible to change it by
	/// calling lzma_index_stream_flags() again.
	uint32_t checks;
};


static void
index_tree_init(index_tree *tree)
{
	tree->root = NULL;
	tree->leftmost = NULL;
	tree->rightmost = NULL;
	tree->count = 0;
	return;
}


/// Helper for index_tree_end()
static void
index_tree_node_end(index_tree_node *node, const lzma_allocator *allocator,
		void (*free_func)(void *node, const lzma_allocator *allocator))
{
	// The tree won't ever be very huge, so recursion should be fine.
	// 20 levels in the tree is likely quite a lot already in practice.
	if (node->left != NULL)
		index_tree_node_end(node->left, allocator, free_func);

	if (node->right != NULL)
		index_tree_node_end(node->right, allocator, free_func);

	free_func(node, allocator);
	return;
}


/// Free the memory allocated for a tree. Each node is freed using the
/// given free_func which is either &lzma_free or &index_stream_end.
/// The latter is used to free the Record groups from each index_stream
/// before freeing the index_stream itself.
static void
index_tree_end(index_tree *tree, const lzma_allocator *allocator,
		void (*free_func)(void *node, const lzma_allocator *allocator))
{
	assert(free_func != NULL);

	if (tree->root != NULL)
		index_tree_node_end(tree->root, allocator, free_func);

	return;
}


/// Add a new node to the tree. node->uncompressed_base and
/// node->compressed_base must have been set by the caller already.
static void
index_tree_append(index_tree *tree, index_tree_node *node)
{
	node->parent = tree->rightmost;
	node->left = NULL;
	node->right = NULL;

	++tree->count;

	// Handle the special case of adding the first node.
	if (tree->root == NULL) {
		tree->root = node;
		tree->leftmost = node;
		tree->rightmost = node;
		return;
	}

	// The tree is always filled sequentially.
	assert(tree->rightmost->uncompressed_base <= node->uncompressed_base);
	assert(tree->rightmost->compressed_base < node->compressed_base);

	// Add the new node after the rightmost node. It's the correct
	// place due to the reason above.
	tree->rightmost->right = node;
	tree->rightmost = node;

	// Balance the AVL-tree if needed. We don't need to keep the balance
	// factors in nodes, because we always fill the tree sequentially,
	// and thus know the state of the tree just by looking at the node
	// count. From the node count we can calculate how many steps to go
	// up in the tree to find the rotation root.
	uint32_t up = tree->count ^ (UINT32_C(1) << bsr32(tree->count));
	if (up != 0) {
		// Locate the root node for the rotation.
		up = ctz32(tree->count) + 2;
		do {
			node = node->parent;
		} while (--up > 0);

		// Rotate left using node as the rotation root.
		index_tree_node *pivot = node->right;

		if (node->parent == NULL) {
			tree->root = pivot;
		} else {
			assert(node->parent->right == node);
			node->parent->right = pivot;
		}

		pivot->parent = node->parent;

		node->right = pivot->left;
		if (node->right != NULL)
			node->right->parent = node;

		pivot->left = node;
		node->parent = pivot;
	}

	return;
}


/// Get the next node in the tree. Return NULL if there are no more nodes.
static void *
index_tree_next(const index_tree_node *node)
{
	if (node->right != NULL) {
		node = node->right;
		while (node->left != NULL)
			node = node->left;

		return (void *)(node);
	}

	while (node->parent != NULL && node->parent->right == node)
		node = node->parent;

	return (void *)(node->parent);
}


/// Locate a node that contains the given uncompressed offset. It is
/// caller's job to check that target is not bigger than the uncompressed
/// size of the tree (the last node would be returned in that case still).
static void *
index_tree_locate(const index_tree *tree, lzma_vli target)
{
	const index_tree_node *result = NULL;
	const index_tree_node *node = tree->root;

	assert(tree->leftmost == NULL
			|| tree->leftmost->uncompressed_base == 0);

	// Consecutive nodes may have the same uncompressed_base.
	// We must pick the rightmost one.
	while (node != NULL) {
		if (node->uncompressed_base > target) {
			node = node->left;
		} else {
			result = node;
			node = node->right;
		}
	}

	return (void *)(result);
}


/// Allocate and initialize a new Stream using the given base offsets.
static index_stream *
index_stream_init(lzma_vli compressed_base, lzma_vli uncompressed_base,
		uint32_t stream_number, lzma_vli block_number_base,
		const lzma_allocator *allocator)
{
	index_stream *s = lzma_alloc(sizeof(index_stream), allocator);
	if (s == NULL)
		return NULL;

	s->node.uncompressed_base = uncompressed_base;
	s->node.compressed_base = compressed_base;
	s->node.parent = NULL;
	s->node.left = NULL;
	s->node.right = NULL;

	s->number = stream_number;
	s->block_number_base = block_number_base;

	index_tree_init(&s->groups);

	s->record_count = 0;
	s->index_list_size = 0;
	s->stream_flags.version = UINT32_MAX;
	s->stream_padding = 0;

	return s;
}


/// Free the memory allocated for a Stream and its Record groups.
static void
index_stream_end(void *node, const lzma_allocator *allocator)
{
	index_stream *s = node;
	index_tree_end(&s->groups, allocator, &lzma_free);
	lzma_free(s, allocator);
	return;
}


static lzma_index *
index_init_plain(const lzma_allocator *allocator)
{
	lzma_index *i = lzma_alloc(sizeof(lzma_index), allocator);
	if (i != NULL) {
		index_tree_init(&i->streams);
		i->uncompressed_size = 0;
		i->total_size = 0;
		i->record_count = 0;
		i->index_list_size = 0;
		i->prealloc = INDEX_GROUP_SIZE;
		i->checks = 0;
	}

	return i;
}


extern LZMA_API(lzma_index *)
lzma_index_init(const lzma_allocator *allocator)
{
	lzma_index *i = index_init_plain(allocator);
	if (i == NULL)
		return NULL;

	index_stream *s = index_stream_init(0, 0, 1, 0, allocator);
	if (s == NULL) {
		lzma_free(i, allocator);
		return NULL;
	}

	index_tree_append(&i->streams, &s->node);

	return i;
}


extern LZMA_API(void)
lzma_index_end(lzma_index *i, const lzma_allocator *allocator)
{
	// NOTE: If you modify this function, check also the bottom
	// of lzma_index_cat().
	if (i != NULL) {
		index_tree_end(&i->streams, allocator, &index_stream_end);
		lzma_free(i, allocator);
	}

	return;
}


extern void
lzma_index_prealloc(lzma_index *i, lzma_vli records)
{
	if (records > PREALLOC_MAX)
		records = PREALLOC_MAX;

	i->prealloc = (size_t)(records);
	return;
}


extern LZMA_API(uint64_t)
lzma_index_memusage(lzma_vli streams, lzma_vli blocks)
{
	// This calculates an upper bound that is only a little bit
	// bigger than the exact maximum memory usage with the given
	// parameters.

	// Typical malloc() overhead is 2 * sizeof(void *) but we take
	// a little bit extra just in case. Using LZMA_MEMUSAGE_BASE
	// instead would give too inaccurate estimate.
	const size_t alloc_overhead = 4 * sizeof(void *);

	// Amount of memory needed for each Stream base structures.
	// We assume that every Stream has at least one Block and
	// thus at least one group.
	const size_t stream_base = sizeof(index_stream)
			+ sizeof(index_group) + 2 * alloc_overhead;

	// Amount of memory needed per group.
	const size_t group_base = sizeof(index_group)
			+ INDEX_GROUP_SIZE * sizeof(index_record)
			+ alloc_overhead;

	// Number of groups. There may actually be more, but that overhead
	// has been taken into account in stream_base already.
	const lzma_vli groups
			= (blocks + INDEX_GROUP_SIZE - 1) / INDEX_GROUP_SIZE;

	// Memory used by index_stream and index_group structures.
	const uint64_t streams_mem = streams * stream_base;
	const uint64_t groups_mem = groups * group_base;

	// Memory used by the base structure.
	const uint64_t index_base = sizeof(lzma_index) + alloc_overhead;

	// Validate the arguments and catch integer overflows.
	// Maximum number of Streams is "only" UINT32_MAX, because
	// that limit is used by the tree containing the Streams.
	const uint64_t limit = UINT64_MAX - index_base;
	if (streams == 0 || streams > UINT32_MAX || blocks > LZMA_VLI_MAX
			|| streams > limit / stream_base
			|| groups > limit / group_base
			|| limit - streams_mem < groups_mem)
		return UINT64_MAX;

	return index_base + streams_mem + groups_mem;
}


extern LZMA_API(uint64_t)
lzma_index_memused(const lzma_index *i)
{
	return lzma_index_memusage(i->streams.count, i->record_count);
}


extern LZMA_API(lzma_vli)
lzma_index_block_count(const lzma_index *i)
{
	return i->record_count;
}


extern LZMA_API(lzma_vli)
lzma_index_stream_count(const lzma_index *i)
{
	return i->streams.count;
}


extern LZMA_API(lzma_vli)
lzma_index_size(const lzma_index *i)
{
	return index_size(i->record_count, i->index_list_size);
}


extern LZMA_API(lzma_vli)
lzma_index_total_size(const lzma_index *i)
{
	return i->total_size;
}


extern LZMA_API(lzma_vli)
lzma_index_stream_size(const lzma_index *i)
{
	// Stream Header + Blocks + Index + Stream Footer
	return LZMA_STREAM_HEADER_SIZE + i->total_size
			+ index_size(i->record_count, i->index_list_size)
			+ LZMA_STREAM_HEADER_SIZE;
}


static lzma_vli
index_file_size(lzma_vli compressed_base, lzma_vli unpadded_sum,
		lzma_vli record_count, lzma_vli index_list_size,
		lzma_vli stream_padding)
{
	// Earlier Streams and Stream Paddings + Stream Header
	// + Blocks + Index + Stream Footer + Stream Padding
	//
	// This might go over LZMA_VLI_MAX due to too big unpadded_sum
	// when this function is used in lzma_index_append().
	lzma_vli file_size = compressed_base + 2 * LZMA_STREAM_HEADER_SIZE
			+ stream_padding + vli_ceil4(unpadded_sum);
	if (file_size > LZMA_VLI_MAX)
		return LZMA_VLI_UNKNOWN;

	// The same applies here.
	file_size += index_size(record_count, index_list_size);
	if (file_size > LZMA_VLI_MAX)
		return LZMA_VLI_UNKNOWN;

	return file_size;
}


extern LZMA_API(lzma_vli)
lzma_index_file_size(const lzma_index *i)
{
	const index_stream *s = (const index_stream *)(i->streams.rightmost);
	const index_group *g = (const index_group *)(s->groups.rightmost);
	return index_file_size(s->node.compressed_base,
			g == NULL ? 0 : g->records[g->last].unpadded_sum,
			s->record_count, s->index_list_size,
			s->stream_padding);
}


extern LZMA_API(lzma_vli)
lzma_index_uncompressed_size(const lzma_index *i)
{
	return i->uncompressed_size;
}


extern LZMA_API(uint32_t)
lzma_index_checks(const lzma_index *i)
{
	uint32_t checks = i->checks;

	// Get the type of the Check of the last Stream too.
	const index_stream *s = (const index_stream *)(i->streams.rightmost);
	if (s->stream_flags.version != UINT32_MAX)
		checks |= UINT32_C(1) << s->stream_flags.check;

	return checks;
}


extern uint32_t
lzma_index_padding_size(const lzma_index *i)
{
	return (LZMA_VLI_C(4) - index_size_unpadded(
			i->record_count, i->index_list_size)) & 3;
}


extern LZMA_API(lzma_ret)
lzma_index_stream_flags(lzma_index *i, const lzma_stream_flags *stream_flags)
{
	if (i == NULL || stream_flags == NULL)
		return LZMA_PROG_ERROR;

	// Validate the Stream Flags.
	return_if_error(lzma_stream_flags_compare(
			stream_flags, stream_flags));

	index_stream *s = (index_stream *)(i->streams.rightmost);
	s->stream_flags = *stream_flags;

	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_index_stream_padding(lzma_index *i, lzma_vli stream_padding)
{
	if (i == NULL || stream_padding > LZMA_VLI_MAX
			|| (stream_padding & 3) != 0)
		return LZMA_PROG_ERROR;

	index_stream *s = (index_stream *)(i->streams.rightmost);

	// Check that the new value won't make the file grow too big.
	const lzma_vli old_stream_padding = s->stream_padding;
	s->stream_padding = 0;
	if (lzma_index_file_size(i) + stream_padding > LZMA_VLI_MAX) {
		s->stream_padding = old_stream_padding;
		return LZMA_DATA_ERROR;
	}

	s->stream_padding = stream_padding;
	return LZMA_OK;
}


extern LZMA_API(lzma_ret)
lzma_index_append(lzma_index *i, const lzma_allocator *allocator,
		lzma_vli unpadded_size, lzma_vli uncompressed_size)
{
	// Validate.
	if (i == NULL || unpadded_size < UNPADDED_SIZE_MIN
			|| unpadded_size > UNPADDED_SIZE_MAX
			|| uncompressed_size > LZMA_VLI_MAX)
		return LZMA_PROG_ERROR;

	index_stream *s = (index_stream *)(i->streams.rightmost);
	index_group *g = (index_group *)(s->groups.rightmost);

	const lzma_vli compressed_base = g == NULL ? 0
			: vli_ceil4(g->records[g->last].unpadded_sum);
	const lzma_vli uncompressed_base = g == NULL ? 0
			: g->records[g->last].uncompressed_sum;
	const uint32_t index_list_size_add = lzma_vli_size(unpadded_size)
			+ lzma_vli_size(uncompressed_size);

	// Check that the file size will stay within limits.
	if (index_file_size(s->node.compressed_base,
			compressed_base + unpadded_size, s->record_count + 1,
			s->index_list_size + index_list_size_add,
			s->stream_padding) == LZMA_VLI_UNKNOWN)
		return LZMA_DATA_ERROR;

	// The size of the Index field must not exceed the maximum value
	// that can be stored in the Backward Size field.
	if (index_size(i->record_count + 1,
			i->index_list_size + index_list_size_add)
			> LZMA_BACKWARD_SIZE_MAX)
		return LZMA_DATA_ERROR;

	if (g != NULL && g->last + 1 < g->allocated) {
		// There is space in the last group at least for one Record.
		++g->last;
	} else {
		// We need to allocate a new group.
		g = lzma_alloc(sizeof(index_group)
				+ i->prealloc * sizeof(index_record),
				allocator);
		if (g == NULL)
			return LZMA_MEM_ERROR;

		g->last = 0;
		g->allocated = i->prealloc;

		// Reset prealloc so that if the application happens to
		// add new Records, the allocation size will be sane.
		i->prealloc = INDEX_GROUP_SIZE;

		// Set the start offsets of this group.
		g->node.uncompressed_base = uncompressed_base;
		g->node.compressed_base = compressed_base;
		g->number_base = s->record_count + 1;

		// Add the new group to the Stream.
		index_tree_append(&s->groups, &g->node);
	}

	// Add the new Record to the group.
	g->records[g->last].uncompressed_sum
			= uncompressed_base + uncompressed_size;
	g->records[g->last].unpadded_sum
			= compressed_base + unpadded_size;

	// Update the totals.
	++s->record_count;
	s->index_list_size += index_list_size_add;

	i->total_size += vli_ceil4(unpadded_size);
	i->uncompressed_size += uncompressed_size;
	++i->record_count;
	i->index_list_size += index_list_size_add;

	return LZMA_OK;
}


/// Structure to pass info to index_cat_helper()
typedef struct {
	/// Uncompressed size of the destination
	lzma_vli uncompressed_size;

	/// Compressed file size of the destination
	lzma_vli file_size;

	/// Same as above but for Block numbers
	lzma_vli block_number_add;

	/// Number of Streams that were in the destination index before we
	/// started appending new Streams from the source index. This is
	/// used to fix the Stream numbering.
	uint32_t stream_number_add;

	/// Destination index' Stream tree
	index_tree *streams;

} index_cat_info;


/// Add the Stream nodes from the source index to dest using recursion.
/// Simplest iterative traversal of the source tree wouldn't work, because
/// we update the pointers in nodes when moving them to the destination tree.
static void
index_cat_helper(const index_cat_info *info, index_stream *this)
{
	index_stream *left = (index_stream *)(this->node.left);
	index_stream *right = (index_stream *)(this->node.right);

	if (left != NULL)
		index_cat_helper(info, left);

	this->node.uncompressed_base += info->uncompressed_size;
	this->node.compressed_base += info->file_size;
	this->number += info->stream_number_add;
	this->block_number_base += info->block_number_add;
	index_tree_append(info->streams, &this->node);

	if (right != NULL)
		index_cat_helper(info, right);

	return;
}


extern LZMA_API(lzma_ret)
lzma_index_cat(lzma_index *restrict dest, lzma_index *restrict src,
		const lzma_allocator *allocator)
{
	const lzma_vli dest_file_size = lzma_index_file_size(dest);

	// Check that we don't exceed the file size limits.
	if (dest_file_size + lzma_index_file_size(src) > LZMA_VLI_MAX
			|| dest->uncompressed_size + src->uncompressed_size
				> LZMA_VLI_MAX)
		return LZMA_DATA_ERROR;

	// Check that the encoded size of the combined lzma_indexes stays
	// within limits. In theory, this should be done only if we know
	// that the user plans to actually combine the Streams and thus
	// construct a single Index (probably rare). However, exceeding
	// this limit is quite theoretical, so we do this check always
	// to simplify things elsewhere.
	{
		const lzma_vli dest_size = index_size_unpadded(
				dest->record_count, dest->index_list_size);
		const lzma_vli src_size = index_size_unpadded(
				src->record_count, src->index_list_size);
		if (vli_ceil4(dest_size + src_size) > LZMA_BACKWARD_SIZE_MAX)
			return LZMA_DATA_ERROR;
	}

	// Optimize the last group to minimize memory usage. Allocation has
	// to be done before modifying dest or src.
	{
		index_stream *s = (index_stream *)(dest->streams.rightmost);
		index_group *g = (index_group *)(s->groups.rightmost);
		if (g != NULL && g->last + 1 < g->allocated) {
			assert(g->node.left == NULL);
			assert(g->node.right == NULL);

			index_group *newg = lzma_alloc(sizeof(index_group)
					+ (g->last + 1)
					* sizeof(index_record),
					allocator);
			if (newg == NULL)
				return LZMA_MEM_ERROR;

			newg->node = g->node;
			newg->allocated = g->last + 1;
			newg->last = g->last;
			newg->number_base = g->number_base;

			memcpy(newg->records, g->records, newg->allocated
					* sizeof(index_record));

			if (g->node.parent != NULL) {
				assert(g->node.parent->right == &g->node);
				g->node.parent->right = &newg->node;
			}

			if (s->groups.leftmost == &g->node) {
				assert(s->groups.root == &g->node);
				s->groups.leftmost = &newg->node;
				s->groups.root = &newg->node;
			}

			if (s->groups.rightmost == &g->node)
				s->groups.rightmost = &newg->node;

			lzma_free(g, allocator);

			// NOTE: newg isn't leaked here because
			// newg == (void *)&newg->node.
		}
	}

	// Add all the Streams from src to dest. Update the base offsets
	// of each Stream from src.
	const index_cat_info info = {
		.uncompressed_size = dest->uncompressed_size,
		.file_size = dest_file_size,
		.stream_number_add = dest->streams.count,
		.block_number_add = dest->record_count,
		.streams = &dest->streams,
	};
	index_cat_helper(&info, (index_stream *)(src->streams.root));

	// Update info about all the combined Streams.
	dest->uncompressed_size += src->uncompressed_size;
	dest->total_size += src->total_size;
	dest->record_count += src->record_count;
	dest->index_list_size += src->index_list_size;
	dest->checks = lzma_index_checks(dest) | src->checks;

	// There's nothing else left in src than the base structure.
	lzma_free(src, allocator);

	return LZMA_OK;
}


/// Duplicate an index_stream.
static index_stream *
index_dup_stream(const index_stream *src, const lzma_allocator *allocator)
{
	// Catch a somewhat theoretical integer overflow.
	if (src->record_count > PREALLOC_MAX)
		return NULL;

	// Allocate and initialize a new Stream.
	index_stream *dest = index_stream_init(src->node.compressed_base,
			src->node.uncompressed_base, src->number,
			src->block_number_base, allocator);
	if (dest == NULL)
		return NULL;

	// Copy the overall information.
	dest->record_count = src->record_count;
	dest->index_list_size = src->index_list_size;
	dest->stream_flags = src->stream_flags;
	dest->stream_padding = src->stream_padding;

	// Return if there are no groups to duplicate.
	if (src->groups.leftmost == NULL)
		return dest;

	// Allocate memory for the Records. We put all the Records into
	// a single group. It's simplest and also tends to make
	// lzma_index_locate() a little bit faster with very big Indexes.
	index_group *destg = lzma_alloc(sizeof(index_group)
			+ src->record_count * sizeof(index_record),
			allocator);
	if (destg == NULL) {
		index_stream_end(dest, allocator);
		return NULL;
	}

	// Initialize destg.
	destg->node.uncompressed_base = 0;
	destg->node.compressed_base = 0;
	destg->number_base = 1;
	destg->allocated = src->record_count;
	destg->last = src->record_count - 1;

	// Go through all the groups in src and copy the Records into destg.
	const index_group *srcg = (const index_group *)(src->groups.leftmost);
	size_t i = 0;
	do {
		memcpy(destg->records + i, srcg->records,
				(srcg->last + 1) * sizeof(index_record));
		i += srcg->last + 1;
		srcg = index_tree_next(&srcg->node);
	} while (srcg != NULL);

	assert(i == destg->allocated);

	// Add the group to the new Stream.
	index_tree_append(&dest->groups, &destg->node);

	return dest;
}


extern LZMA_API(lzma_index *)
lzma_index_dup(const lzma_index *src, const lzma_allocator *allocator)
{
	// Allocate the base structure (no initial Stream).
	lzma_index *dest = index_init_plain(allocator);
	if (dest == NULL)
		return NULL;

	// Copy the totals.
	dest->uncompressed_size = src->uncompressed_size;
	dest->total_size = src->total_size;
	dest->record_count = src->record_count;
	dest->index_list_size = src->index_list_size;

	// Copy the Streams and the groups in them.
	const index_stream *srcstream
			= (const index_stream *)(src->streams.leftmost);
	do {
		index_stream *deststream = index_dup_stream(
				srcstream, allocator);
		if (deststream == NULL) {
			lzma_index_end(dest, allocator);
			return NULL;
		}

		index_tree_append(&dest->streams, &deststream->node);

		srcstream = index_tree_next(&srcstream->node);
	} while (srcstream != NULL);

	return dest;
}


/// Indexing for lzma_index_iter.internal[]
enum {
	ITER_INDEX,
	ITER_STREAM,
	ITER_GROUP,
	ITER_RECORD,
	ITER_METHOD,
};


/// Values for lzma_index_iter.internal[ITER_METHOD].s
enum {
	ITER_METHOD_NORMAL,
	ITER_METHOD_NEXT,
	ITER_METHOD_LEFTMOST,
};


static void
iter_set_info(lzma_index_iter *iter)
{
	const lzma_index *i = iter->internal[ITER_INDEX].p;
	const index_stream *stream = iter->internal[ITER_STREAM].p;
	const index_group *group = iter->internal[ITER_GROUP].p;
	const size_t record = iter->internal[ITER_RECORD].s;

	// lzma_index_iter.internal must not contain a pointer to the last
	// group in the index, because that may be reallocated by
	// lzma_index_cat().
	if (group == NULL) {
		// There are no groups.
		assert(stream->groups.root == NULL);
		iter->internal[ITER_METHOD].s = ITER_METHOD_LEFTMOST;

	} else if (i->streams.rightmost != &stream->node
			|| stream->groups.rightmost != &group->node) {
		// The group is not not the last group in the index.
		iter->internal[ITER_METHOD].s = ITER_METHOD_NORMAL;

	} else if (stream->groups.leftmost != &group->node) {
		// The group isn't the only group in the Stream, thus we
		// know that it must have a parent group i.e. it's not
		// the root node.
		assert(stream->groups.root != &group->node);
		assert(group->node.parent->right == &group->node);
		iter->internal[ITER_METHOD].s = ITER_METHOD_NEXT;
		iter->internal[ITER_GROUP].p = group->node.parent;

	} else {
		// The Stream has only one group.
		assert(stream->groups.root == &group->node);
		assert(group->node.parent == NULL);
		iter->internal[ITER_METHOD].s = ITER_METHOD_LEFTMOST;
		iter->internal[ITER_GROUP].p = NULL;
	}

	// NOTE: lzma_index_iter.stream.number is lzma_vli but we use uint32_t
	// internally.
	iter->stream.number = stream->number;
	iter->stream.block_count = stream->record_count;
	iter->stream.compressed_offset = stream->node.compressed_base;
	iter->stream.uncompressed_offset = stream->node.uncompressed_base;

	// iter->stream.flags will be NULL if the Stream Flags haven't been
	// set with lzma_index_stream_flags().
	iter->stream.flags = stream->stream_flags.version == UINT32_MAX
			? NULL : &stream->stream_flags;
	iter->stream.padding = stream->stream_padding;

	if (stream->groups.rightmost == NULL) {
		// Stream has no Blocks.
		iter->stream.compressed_size = index_size(0, 0)
				+ 2 * LZMA_STREAM_HEADER_SIZE;
		iter->stream.uncompressed_size = 0;
	} else {
		const index_group *g = (const index_group *)(
				stream->groups.rightmost);

		// Stream Header + Stream Footer + Index + Blocks
		iter->stream.compressed_size = 2 * LZMA_STREAM_HEADER_SIZE
				+ index_size(stream->record_count,
					stream->index_list_size)
				+ vli_ceil4(g->records[g->last].unpadded_sum);
		iter->stream.uncompressed_size
				= g->records[g->last].uncompressed_sum;
	}

	if (group != NULL) {
		iter->block.number_in_stream = group->number_base + record;
		iter->block.number_in_file = iter->block.number_in_stream
				+ stream->block_number_base;

		iter->block.compressed_stream_offset
				= record == 0 ? group->node.compressed_base
				: vli_ceil4(group->records[
					record - 1].unpadded_sum);
		iter->block.uncompressed_stream_offset
				= record == 0 ? group->node.uncompressed_base
				: group->records[record - 1].uncompressed_sum;

		iter->block.uncompressed_size
				= group->records[record].uncompressed_sum
				- iter->block.uncompressed_stream_offset;
		iter->block.unpadded_size
				= group->records[record].unpadded_sum
				- iter->block.compressed_stream_offset;
		iter->block.total_size = vli_ceil4(iter->block.unpadded_size);

		iter->block.compressed_stream_offset
				+= LZMA_STREAM_HEADER_SIZE;

		iter->block.compressed_file_offset
				= iter->block.compressed_stream_offset
				+ iter->stream.compressed_offset;
		iter->block.uncompressed_file_offset
				= iter->block.uncompressed_stream_offset
				+ iter->stream.uncompressed_offset;
	}

	return;
}


extern LZMA_API(void)
lzma_index_iter_init(lzma_index_iter *iter, const lzma_index *i)
{
	iter->internal[ITER_INDEX].p = i;
	lzma_index_iter_rewind(iter);
	return;
}


extern LZMA_API(void)
lzma_index_iter_rewind(lzma_index_iter *iter)
{
	iter->internal[ITER_STREAM].p = NULL;
	iter->internal[ITER_GROUP].p = NULL;
	iter->internal[ITER_RECORD].s = 0;
	iter->internal[ITER_METHOD].s = ITER_METHOD_NORMAL;
	return;
}


extern LZMA_API(lzma_bool)
lzma_index_iter_next(lzma_index_iter *iter, lzma_index_iter_mode mode)
{
	// Catch unsupported mode values.
	if ((unsigned int)(mode) > LZMA_INDEX_ITER_NONEMPTY_BLOCK)
		return true;

	const lzma_index *i = iter->internal[ITER_INDEX].p;
	const index_stream *stream = iter->internal[ITER_STREAM].p;
	const index_group *group = NULL;
	size_t record = iter->internal[ITER_RECORD].s;

	// If we are being asked for the next Stream, leave group to NULL
	// so that the rest of the this function thinks that this Stream
	// has no groups and will thus go to the next Stream.
	if (mode != LZMA_INDEX_ITER_STREAM) {
		// Get the pointer to the current group. See iter_set_inf()
		// for explanation.
		switch (iter->internal[ITER_METHOD].s) {
		case ITER_METHOD_NORMAL:
			group = iter->internal[ITER_GROUP].p;
			break;

		case ITER_METHOD_NEXT:
			group = index_tree_next(iter->internal[ITER_GROUP].p);
			break;

		case ITER_METHOD_LEFTMOST:
			group = (const index_group *)(
					stream->groups.leftmost);
			break;
		}
	}

again:
	if (stream == NULL) {
		// We at the beginning of the lzma_index.
		// Locate the first Stream.
		stream = (const index_stream *)(i->streams.leftmost);
		if (mode >= LZMA_INDEX_ITER_BLOCK) {
			// Since we are being asked to return information
			// about the first a Block, skip Streams that have
			// no Blocks.
			while (stream->groups.leftmost == NULL) {
				stream = index_tree_next(&stream->node);
				if (stream == NULL)
					return true;
			}
		}

		// Start from the first Record in the Stream.
		group = (const index_group *)(stream->groups.leftmost);
		record = 0;

	} else if (group != NULL && record < group->last) {
		// The next Record is in the same group.
		++record;

	} else {
		// This group has no more Records or this Stream has
		// no Blocks at all.
		record = 0;

		// If group is not NULL, this Stream has at least one Block
		// and thus at least one group. Find the next group.
		if (group != NULL)
			group = index_tree_next(&group->node);

		if (group == NULL) {
			// This Stream has no more Records. Find the next
			// Stream. If we are being asked to return information
			// about a Block, we skip empty Streams.
			do {
				stream = index_tree_next(&stream->node);
				if (stream == NULL)
					return true;
			} while (mode >= LZMA_INDEX_ITER_BLOCK
					&& stream->groups.leftmost == NULL);

			group = (const index_group *)(
					stream->groups.leftmost);
		}
	}

	if (mode == LZMA_INDEX_ITER_NONEMPTY_BLOCK) {
		// We need to look for the next Block again if this Block
		// is empty.
		if (record == 0) {
			if (group->node.uncompressed_base
					== group->records[0].uncompressed_sum)
				goto again;
		} else if (group->records[record - 1].uncompressed_sum
				== group->records[record].uncompressed_sum) {
			goto again;
		}
	}

	iter->internal[ITER_STREAM].p = stream;
	iter->internal[ITER_GROUP].p = group;
	iter->internal[ITER_RECORD].s = record;

	iter_set_info(iter);

	return false;
}


extern LZMA_API(lzma_bool)
lzma_index_iter_locate(lzma_index_iter *iter, lzma_vli target)
{
	const lzma_index *i = iter->internal[ITER_INDEX].p;

	// If the target is past the end of the file, return immediately.
	if (i->uncompressed_size <= target)
		return true;

	// Locate the Stream containing the target offset.
	const index_stream *stream = index_tree_locate(&i->streams, target);
	assert(stream != NULL);
	target -= stream->node.uncompressed_base;

	// Locate the group containing the target offset.
	const index_group *group = index_tree_locate(&stream->groups, target);
	assert(group != NULL);

	// Use binary search to locate the exact Record. It is the first
	// Record whose uncompressed_sum is greater than target.
	// This is because we want the rightmost Record that fullfills the
	// search criterion. It is possible that there are empty Blocks;
	// we don't want to return them.
	size_t left = 0;
	size_t right = group->last;

	while (left < right) {
		const size_t pos = left + (right - left) / 2;
		if (group->records[pos].uncompressed_sum <= target)
			left = pos + 1;
		else
			right = pos;
	}

	iter->internal[ITER_STREAM].p = stream;
	iter->internal[ITER_GROUP].p = group;
	iter->internal[ITER_RECORD].s = left;

	iter_set_info(iter);

	return false;
}
