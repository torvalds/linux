/* -*- buffer-read-only: t -*- vi: set ro:
 *
 * Prototypes for autoopts
 * Generated Sat Apr 25 09:53:18 PDT 2015
 */
#ifndef AUTOOPTS_PROTO_H_GUARD
#define AUTOOPTS_PROTO_H_GUARD 1

/*
 *  Extracted from alias.c
 */
static tSuccess
too_many_occurrences(tOptions * opts, tOptDesc * od);

/*
 *  Extracted from autoopts.c
 */
static void *
ao_malloc(size_t sz);

static void *
ao_realloc(void *p, size_t sz);

static char *
ao_strdup(char const *str);

static tSuccess
handle_opt(tOptions * opts, tOptState * o_st);

static tSuccess
next_opt(tOptions * opts, tOptState * o_st);

static tSuccess
regular_opts(tOptions * opts);

/*
 *  Extracted from check.c
 */
static bool
is_consistent(tOptions * pOpts);

/*
 *  Extracted from configfile.c
 */
static void
intern_file_load(tOptions * opts);

static char const *
parse_attrs(tOptions * opts, char const * txt, tOptionLoadMode * pMode,
            tOptionValue * pType);

/*
 *  Extracted from env.c
 */
static void
doPrognameEnv(tOptions * pOpts, teEnvPresetType type);

static void
env_presets(tOptions * pOpts, teEnvPresetType type);

/*
 *  Extracted from find.c
 */
static tSuccess
opt_find_long(tOptions * opts, char const * opt_name, tOptState * state);

static tSuccess
opt_find_short(tOptions * pOpts, uint_t optValue, tOptState * pOptState);

static tSuccess
get_opt_arg(tOptions * opts, tOptState * o_st);

static tSuccess
find_opt(tOptions * opts, tOptState * o_st);

/*
 *  Extracted from init.c
 */
static tSuccess
validate_struct(tOptions * opts, char const * pname);

static tSuccess
immediate_opts(tOptions * opts);

static bool
ao_initialize(tOptions * opts, int a_ct, char ** a_v);

/*
 *  Extracted from load.c
 */
static void
munge_str(char * txt, tOptionLoadMode mode);

static void
load_opt_line(tOptions * opts, tOptState * opt_state, char * line,
              tDirection direction, tOptionLoadMode load_mode );

/*
 *  Extracted from makeshell.c
 */
static noreturn void
option_exits(int exit_code);

static noreturn void
ao_bug(char const * msg);

static void
fserr_warn(char const * prog, char const * op, char const * fname);

static noreturn void
fserr_exit(char const * prog, char const * op, char const * fname);

/*
 *  Extracted from nested.c
 */
static void
unload_arg_list(tArgList * arg_list);

static tOptionValue *
optionLoadNested(char const * text, char const * name, size_t nm_len);

static int
get_special_char(char const ** ppz, int * ct);

static void
emit_special_char(FILE * fp, int ch);

/*
 *  Extracted from sort.c
 */
static void
optionSort(tOptions * opts);

/*
 *  Extracted from stack.c
 */
static void
addArgListEntry(void ** ppAL, void * entry);

/*
 *  Extracted from usage.c
 */
static void
set_usage_flags(tOptions * opts, char const * flg_txt);

#endif /* AUTOOPTS_PROTO_H_GUARD */
