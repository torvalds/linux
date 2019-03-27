#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/bin.h"
#include "jemalloc/internal/witness.h"

const bin_info_t bin_infos[NBINS] = {
#define BIN_INFO_bin_yes(reg_size, slab_size, nregs)			\
	{reg_size, slab_size, nregs, BITMAP_INFO_INITIALIZER(nregs)},
#define BIN_INFO_bin_no(reg_size, slab_size, nregs)
#define SC(index, lg_grp, lg_delta, ndelta, psz, bin, pgs,		\
    lg_delta_lookup)							\
	BIN_INFO_bin_##bin((1U<<lg_grp) + (ndelta<<lg_delta),		\
	    (pgs << LG_PAGE), (pgs << LG_PAGE) / ((1U<<lg_grp) +	\
	    (ndelta<<lg_delta)))
	SIZE_CLASSES
#undef BIN_INFO_bin_yes
#undef BIN_INFO_bin_no
#undef SC
};

bool
bin_init(bin_t *bin) {
	if (malloc_mutex_init(&bin->lock, "bin", WITNESS_RANK_BIN,
	    malloc_mutex_rank_exclusive)) {
		return true;
	}
	bin->slabcur = NULL;
	extent_heap_new(&bin->slabs_nonfull);
	extent_list_init(&bin->slabs_full);
	if (config_stats) {
		memset(&bin->stats, 0, sizeof(bin_stats_t));
	}
	return false;
}

void
bin_prefork(tsdn_t *tsdn, bin_t *bin) {
	malloc_mutex_prefork(tsdn, &bin->lock);
}

void
bin_postfork_parent(tsdn_t *tsdn, bin_t *bin) {
	malloc_mutex_postfork_parent(tsdn, &bin->lock);
}

void
bin_postfork_child(tsdn_t *tsdn, bin_t *bin) {
	malloc_mutex_postfork_child(tsdn, &bin->lock);
}
