/*
 * Common modutils related functions for busybox
 *
 * Copyright (C) 2008 by Timo Teras <timo.teras@iki.fi>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

#ifndef MODUTILS_H
#define MODUTILS_H 1

#include "libbb.h"

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

/* linux/include/linux/module.h has 64, but this is also used
 * internally for the maximum alias name length, which can be quite long */
#define MODULE_NAME_LEN 256
#define MODULE_HASH_SIZE 256

typedef struct module_entry {
	struct module_entry *next;
	char *name, *modname;
	llist_t *deps;
	IF_MODPROBE(
		llist_t *realnames;
		unsigned flags;
		const char *probed_name; /* verbatim as seen on cmdline */
		char *options; /* options from config files */
	)
	IF_DEPMOD(
		llist_t *aliases;
		llist_t *symbols;
		struct module_entry *dnext, *dprev;
	)
} module_entry;

typedef struct module_db {
	module_entry *buckets[MODULE_HASH_SIZE];
} module_db;

#define moddb_foreach_module(db, module, index) \
	for ((index) = 0; (index) < MODULE_HASH_SIZE; (index)++) \
		for (module = (db)->buckets[index]; module; module = module->next)

module_entry *moddb_get(module_db *db, const char *s) FAST_FUNC;
module_entry *moddb_get_or_create(module_db *db, const char *s) FAST_FUNC;
void moddb_free(module_db *db) FAST_FUNC;

void replace(char *s, char what, char with) FAST_FUNC;
int string_to_llist(char *string, llist_t **llist, const char *delim) FAST_FUNC;
char *filename2modname(const char *filename, char *modname) FAST_FUNC;
#if ENABLE_FEATURE_CMDLINE_MODULE_OPTIONS
char *parse_cmdline_module_options(char **argv, int quote_spaces) FAST_FUNC;
#else
# define parse_cmdline_module_options(argv, quote_spaces) ""
#endif

/* insmod for 2.4 and modprobe's options (insmod 2.6 has no options at all): */
#define INSMOD_OPTS \
	"vqs" \
	IF_FEATURE_2_4_MODULES("Lfkx" IF_FEATURE_INSMOD_LOAD_MAP("m"))
#define INSMOD_ARGS /* (was meant to support -o NAME) , NULL */

enum {
	INSMOD_OPT_VERBOSE      = (1 << 0),
	INSMOD_OPT_SILENT       = (1 << 1),
	INSMOD_OPT_SYSLOG       = (1 << 2),
	//INSMOD_OPT_OUTPUTNAME = (1 << x) - not supported yet
	INSMOD_OPT_LOCK         = (1 << 3) * ENABLE_FEATURE_2_4_MODULES,
	INSMOD_OPT_FORCE        = (1 << 4) * ENABLE_FEATURE_2_4_MODULES,
	INSMOD_OPT_KERNELD      = (1 << 5) * ENABLE_FEATURE_2_4_MODULES,
	INSMOD_OPT_NO_EXPORT    = (1 << 6) * ENABLE_FEATURE_2_4_MODULES,
	INSMOD_OPT_PRINT_MAP    = (1 << 7) * ENABLE_FEATURE_INSMOD_LOAD_MAP,
	INSMOD_OPT_UNUSED       =
		(INSMOD_OPT_PRINT_MAP ? INSMOD_OPT_PRINT_MAP
		: INSMOD_OPT_NO_EXPORT ? INSMOD_OPT_NO_EXPORT
		: INSMOD_OPT_SYSLOG
		) << 1
};

#if ENABLE_FEATURE_INSMOD_TRY_MMAP
void* FAST_FUNC try_to_mmap_module(const char *filename, size_t *image_size_p);
#else
# define try_to_mmap_module(filename, image_size) NULL
#endif

/* Return:
 * 0 on success,
 * -errno on open/read error,
 * errno on init_module() error
 */
int FAST_FUNC bb_init_module(const char *module, const char *options);
/* Return:
 * 0 on success,
 * errno on init_module() error
 */
int FAST_FUNC bb_delete_module(const char *module, unsigned int flags);
/* Translates error return to a string */
const char *moderror(int err) FAST_FUNC;

#if ENABLE_FEATURE_2_4_MODULES
int FAST_FUNC bb_init_module_24(const char *module, const char *options);
#endif

POP_SAVED_FUNCTION_VISIBILITY

#endif
