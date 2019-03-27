///////////////////////////////////////////////////////////////////////////////
//
/// \file       args.c
/// \brief      Argument parsing
///
/// \note       Filter-specific options parsing is in options.c.
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "private.h"

#include "getopt.h"
#include <ctype.h>


bool opt_stdout = false;
bool opt_force = false;
bool opt_keep_original = false;
bool opt_robot = false;
bool opt_ignore_check = false;

// We don't modify or free() this, but we need to assign it in some
// non-const pointers.
const char stdin_filename[] = "(stdin)";


/// Parse and set the memory usage limit for compression and/or decompression.
static void
parse_memlimit(const char *name, const char *name_percentage, char *str,
		bool set_compress, bool set_decompress)
{
	bool is_percentage = false;
	uint64_t value;

	const size_t len = strlen(str);
	if (len > 0 && str[len - 1] == '%') {
		str[len - 1] = '\0';
		is_percentage = true;
		value = str_to_uint64(name_percentage, str, 1, 100);
	} else {
		// On 32-bit systems, SIZE_MAX would make more sense than
		// UINT64_MAX. But use UINT64_MAX still so that scripts
		// that assume > 4 GiB values don't break.
		value = str_to_uint64(name, str, 0, UINT64_MAX);
	}

	hardware_memlimit_set(
			value, set_compress, set_decompress, is_percentage);
	return;
}


static void
parse_block_list(char *str)
{
	// It must be non-empty and not begin with a comma.
	if (str[0] == '\0' || str[0] == ',')
		message_fatal(_("%s: Invalid argument to --block-list"), str);

	// Count the number of comma-separated strings.
	size_t count = 1;
	for (size_t i = 0; str[i] != '\0'; ++i)
		if (str[i] == ',')
			++count;

	// Prevent an unlikely integer overflow.
	if (count > SIZE_MAX / sizeof(uint64_t) - 1)
		message_fatal(_("%s: Too many arguments to --block-list"),
				str);

	// Allocate memory to hold all the sizes specified.
	// If --block-list was specified already, its value is forgotten.
	free(opt_block_list);
	opt_block_list = xmalloc((count + 1) * sizeof(uint64_t));

	for (size_t i = 0; i < count; ++i) {
		// Locate the next comma and replace it with \0.
		char *p = strchr(str, ',');
		if (p != NULL)
			*p = '\0';

		if (str[0] == '\0') {
			// There is no string, that is, a comma follows
			// another comma. Use the previous value.
			//
			// NOTE: We checked earler that the first char
			// of the whole list cannot be a comma.
			assert(i > 0);
			opt_block_list[i] = opt_block_list[i - 1];
		} else {
			opt_block_list[i] = str_to_uint64("block-list", str,
					0, UINT64_MAX);

			// Zero indicates no more new Blocks.
			if (opt_block_list[i] == 0) {
				if (i + 1 != count)
					message_fatal(_("0 can only be used "
							"as the last element "
							"in --block-list"));

				opt_block_list[i] = UINT64_MAX;
			}
		}

		str = p + 1;
	}

	// Terminate the array.
	opt_block_list[count] = 0;
	return;
}


static void
parse_real(args_info *args, int argc, char **argv)
{
	enum {
		OPT_X86 = INT_MIN,
		OPT_POWERPC,
		OPT_IA64,
		OPT_ARM,
		OPT_ARMTHUMB,
		OPT_SPARC,
		OPT_DELTA,
		OPT_LZMA1,
		OPT_LZMA2,

		OPT_SINGLE_STREAM,
		OPT_NO_SPARSE,
		OPT_FILES,
		OPT_FILES0,
		OPT_BLOCK_SIZE,
		OPT_BLOCK_LIST,
		OPT_MEM_COMPRESS,
		OPT_MEM_DECOMPRESS,
		OPT_NO_ADJUST,
		OPT_INFO_MEMORY,
		OPT_ROBOT,
		OPT_FLUSH_TIMEOUT,
		OPT_IGNORE_CHECK,
	};

	static const char short_opts[]
			= "cC:defF:hHlkM:qQrS:tT:vVz0123456789";

	static const struct option long_opts[] = {
		// Operation mode
		{ "compress",     no_argument,       NULL,  'z' },
		{ "decompress",   no_argument,       NULL,  'd' },
		{ "uncompress",   no_argument,       NULL,  'd' },
		{ "test",         no_argument,       NULL,  't' },
		{ "list",         no_argument,       NULL,  'l' },

		// Operation modifiers
		{ "keep",         no_argument,       NULL,  'k' },
		{ "force",        no_argument,       NULL,  'f' },
		{ "stdout",       no_argument,       NULL,  'c' },
		{ "to-stdout",    no_argument,       NULL,  'c' },
		{ "single-stream", no_argument,      NULL,  OPT_SINGLE_STREAM },
		{ "no-sparse",    no_argument,       NULL,  OPT_NO_SPARSE },
		{ "suffix",       required_argument, NULL,  'S' },
		// { "recursive",      no_argument,       NULL,  'r' }, // TODO
		{ "files",        optional_argument, NULL,  OPT_FILES },
		{ "files0",       optional_argument, NULL,  OPT_FILES0 },

		// Basic compression settings
		{ "format",       required_argument, NULL,  'F' },
		{ "check",        required_argument, NULL,  'C' },
		{ "ignore-check", no_argument,       NULL,  OPT_IGNORE_CHECK },
		{ "block-size",   required_argument, NULL,  OPT_BLOCK_SIZE },
		{ "block-list",  required_argument, NULL,  OPT_BLOCK_LIST },
		{ "memlimit-compress",   required_argument, NULL, OPT_MEM_COMPRESS },
		{ "memlimit-decompress", required_argument, NULL, OPT_MEM_DECOMPRESS },
		{ "memlimit",     required_argument, NULL,  'M' },
		{ "memory",       required_argument, NULL,  'M' }, // Old alias
		{ "no-adjust",    no_argument,       NULL,  OPT_NO_ADJUST },
		{ "threads",      required_argument, NULL,  'T' },
		{ "flush-timeout", required_argument, NULL, OPT_FLUSH_TIMEOUT },

		{ "extreme",      no_argument,       NULL,  'e' },
		{ "fast",         no_argument,       NULL,  '0' },
		{ "best",         no_argument,       NULL,  '9' },

		// Filters
		{ "lzma1",        optional_argument, NULL,  OPT_LZMA1 },
		{ "lzma2",        optional_argument, NULL,  OPT_LZMA2 },
		{ "x86",          optional_argument, NULL,  OPT_X86 },
		{ "powerpc",      optional_argument, NULL,  OPT_POWERPC },
		{ "ia64",         optional_argument, NULL,  OPT_IA64 },
		{ "arm",          optional_argument, NULL,  OPT_ARM },
		{ "armthumb",     optional_argument, NULL,  OPT_ARMTHUMB },
		{ "sparc",        optional_argument, NULL,  OPT_SPARC },
		{ "delta",        optional_argument, NULL,  OPT_DELTA },

		// Other options
		{ "quiet",        no_argument,       NULL,  'q' },
		{ "verbose",      no_argument,       NULL,  'v' },
		{ "no-warn",      no_argument,       NULL,  'Q' },
		{ "robot",        no_argument,       NULL,  OPT_ROBOT },
		{ "info-memory",  no_argument,       NULL,  OPT_INFO_MEMORY },
		{ "help",         no_argument,       NULL,  'h' },
		{ "long-help",    no_argument,       NULL,  'H' },
		{ "version",      no_argument,       NULL,  'V' },

		{ NULL,           0,                 NULL,   0 }
	};

	int c;

	while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL))
			!= -1) {
		switch (c) {
		// Compression preset (also for decompression if --format=raw)
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			coder_set_preset(c - '0');
			break;

		// --memlimit-compress
		case OPT_MEM_COMPRESS:
			parse_memlimit("memlimit-compress",
					"memlimit-compress%", optarg,
					true, false);
			break;

		// --memlimit-decompress
		case OPT_MEM_DECOMPRESS:
			parse_memlimit("memlimit-decompress",
					"memlimit-decompress%", optarg,
					false, true);
			break;

		// --memlimit
		case 'M':
			parse_memlimit("memlimit", "memlimit%", optarg,
					true, true);
			break;

		// --suffix
		case 'S':
			suffix_set(optarg);
			break;

		case 'T':
			// The max is from src/liblzma/common/common.h.
			hardware_threads_set(str_to_uint64("threads",
					optarg, 0, 16384));
			break;

		// --version
		case 'V':
			// This doesn't return.
			message_version();

		// --stdout
		case 'c':
			opt_stdout = true;
			break;

		// --decompress
		case 'd':
			opt_mode = MODE_DECOMPRESS;
			break;

		// --extreme
		case 'e':
			coder_set_extreme();
			break;

		// --force
		case 'f':
			opt_force = true;
			break;

		// --info-memory
		case OPT_INFO_MEMORY:
			// This doesn't return.
			hardware_memlimit_show();

		// --help
		case 'h':
			// This doesn't return.
			message_help(false);

		// --long-help
		case 'H':
			// This doesn't return.
			message_help(true);

		// --list
		case 'l':
			opt_mode = MODE_LIST;
			break;

		// --keep
		case 'k':
			opt_keep_original = true;
			break;

		// --quiet
		case 'q':
			message_verbosity_decrease();
			break;

		case 'Q':
			set_exit_no_warn();
			break;

		case 't':
			opt_mode = MODE_TEST;
			break;

		// --verbose
		case 'v':
			message_verbosity_increase();
			break;

		// --robot
		case OPT_ROBOT:
			opt_robot = true;

			// This is to make sure that floating point numbers
			// always have a dot as decimal separator.
			setlocale(LC_NUMERIC, "C");
			break;

		case 'z':
			opt_mode = MODE_COMPRESS;
			break;

		// Filter setup

		case OPT_X86:
			coder_add_filter(LZMA_FILTER_X86,
					options_bcj(optarg));
			break;

		case OPT_POWERPC:
			coder_add_filter(LZMA_FILTER_POWERPC,
					options_bcj(optarg));
			break;

		case OPT_IA64:
			coder_add_filter(LZMA_FILTER_IA64,
					options_bcj(optarg));
			break;

		case OPT_ARM:
			coder_add_filter(LZMA_FILTER_ARM,
					options_bcj(optarg));
			break;

		case OPT_ARMTHUMB:
			coder_add_filter(LZMA_FILTER_ARMTHUMB,
					options_bcj(optarg));
			break;

		case OPT_SPARC:
			coder_add_filter(LZMA_FILTER_SPARC,
					options_bcj(optarg));
			break;

		case OPT_DELTA:
			coder_add_filter(LZMA_FILTER_DELTA,
					options_delta(optarg));
			break;

		case OPT_LZMA1:
			coder_add_filter(LZMA_FILTER_LZMA1,
					options_lzma(optarg));
			break;

		case OPT_LZMA2:
			coder_add_filter(LZMA_FILTER_LZMA2,
					options_lzma(optarg));
			break;

		// Other

		// --format
		case 'F': {
			// Just in case, support both "lzma" and "alone" since
			// the latter was used for forward compatibility in
			// LZMA Utils 4.32.x.
			static const struct {
				char str[8];
				enum format_type format;
			} types[] = {
				{ "auto",   FORMAT_AUTO },
				{ "xz",     FORMAT_XZ },
				{ "lzma",   FORMAT_LZMA },
				{ "alone",  FORMAT_LZMA },
				// { "gzip",   FORMAT_GZIP },
				// { "gz",     FORMAT_GZIP },
				{ "raw",    FORMAT_RAW },
			};

			size_t i = 0;
			while (strcmp(types[i].str, optarg) != 0)
				if (++i == ARRAY_SIZE(types))
					message_fatal(_("%s: Unknown file "
							"format type"),
							optarg);

			opt_format = types[i].format;
			break;
		}

		// --check
		case 'C': {
			static const struct {
				char str[8];
				lzma_check check;
			} types[] = {
				{ "none",   LZMA_CHECK_NONE },
				{ "crc32",  LZMA_CHECK_CRC32 },
				{ "crc64",  LZMA_CHECK_CRC64 },
				{ "sha256", LZMA_CHECK_SHA256 },
			};

			size_t i = 0;
			while (strcmp(types[i].str, optarg) != 0) {
				if (++i == ARRAY_SIZE(types))
					message_fatal(_("%s: Unsupported "
							"integrity "
							"check type"), optarg);
			}

			// Use a separate check in case we are using different
			// liblzma than what was used to compile us.
			if (!lzma_check_is_supported(types[i].check))
				message_fatal(_("%s: Unsupported integrity "
						"check type"), optarg);

			coder_set_check(types[i].check);
			break;
		}

		case OPT_IGNORE_CHECK:
			opt_ignore_check = true;
			break;

		case OPT_BLOCK_SIZE:
			opt_block_size = str_to_uint64("block-size", optarg,
					0, LZMA_VLI_MAX);
			break;

		case OPT_BLOCK_LIST: {
			parse_block_list(optarg);
			break;
		}

		case OPT_SINGLE_STREAM:
			opt_single_stream = true;
			break;

		case OPT_NO_SPARSE:
			io_no_sparse();
			break;

		case OPT_FILES:
			args->files_delim = '\n';

		// Fall through

		case OPT_FILES0:
			if (args->files_name != NULL)
				message_fatal(_("Only one file can be "
						"specified with `--files' "
						"or `--files0'."));

			if (optarg == NULL) {
				args->files_name = (char *)stdin_filename;
				args->files_file = stdin;
			} else {
				args->files_name = optarg;
				args->files_file = fopen(optarg,
						c == OPT_FILES ? "r" : "rb");
				if (args->files_file == NULL)
					message_fatal("%s: %s", optarg,
							strerror(errno));
			}

			break;

		case OPT_NO_ADJUST:
			opt_auto_adjust = false;
			break;

		case OPT_FLUSH_TIMEOUT:
			opt_flush_timeout = str_to_uint64("flush-timeout",
					optarg, 0, UINT64_MAX);
			break;

		default:
			message_try_help();
			tuklib_exit(E_ERROR, E_ERROR, false);
		}
	}

	return;
}


static void
parse_environment(args_info *args, char *argv0, const char *varname)
{
	char *env = getenv(varname);
	if (env == NULL)
		return;

	// We modify the string, so make a copy of it.
	env = xstrdup(env);

	// Calculate the number of arguments in env. argc stats at one
	// to include space for the program name.
	int argc = 1;
	bool prev_was_space = true;
	for (size_t i = 0; env[i] != '\0'; ++i) {
		// NOTE: Cast to unsigned char is needed so that correct
		// value gets passed to isspace(), which expects
		// unsigned char cast to int. Casting to int is done
		// automatically due to integer promotion, but we need to
		// force char to unsigned char manually. Otherwise 8-bit
		// characters would get promoted to wrong value if
		// char is signed.
		if (isspace((unsigned char)env[i])) {
			prev_was_space = true;
		} else if (prev_was_space) {
			prev_was_space = false;

			// Keep argc small enough to fit into a signed int
			// and to keep it usable for memory allocation.
			if (++argc == my_min(
					INT_MAX, SIZE_MAX / sizeof(char *)))
				message_fatal(_("The environment variable "
						"%s contains too many "
						"arguments"), varname);
		}
	}

	// Allocate memory to hold pointers to the arguments. Add one to get
	// space for the terminating NULL (if some systems happen to need it).
	char **argv = xmalloc(((size_t)(argc) + 1) * sizeof(char *));
	argv[0] = argv0;
	argv[argc] = NULL;

	// Go through the string again. Split the arguments using '\0'
	// characters and add pointers to the resulting strings to argv.
	argc = 1;
	prev_was_space = true;
	for (size_t i = 0; env[i] != '\0'; ++i) {
		if (isspace((unsigned char)env[i])) {
			prev_was_space = true;
			env[i] = '\0';
		} else if (prev_was_space) {
			prev_was_space = false;
			argv[argc++] = env + i;
		}
	}

	// Parse the argument list we got from the environment. All non-option
	// arguments i.e. filenames are ignored.
	parse_real(args, argc, argv);

	// Reset the state of the getopt_long() so that we can parse the
	// command line options too. There are two incompatible ways to
	// do it.
#ifdef HAVE_OPTRESET
	// BSD
	optind = 1;
	optreset = 1;
#else
	// GNU, Solaris
	optind = 0;
#endif

	// We don't need the argument list from environment anymore.
	free(argv);
	free(env);

	return;
}


extern void
args_parse(args_info *args, int argc, char **argv)
{
	// Initialize those parts of *args that we need later.
	args->files_name = NULL;
	args->files_file = NULL;
	args->files_delim = '\0';

	// Check how we were called.
	{
		// Remove the leading path name, if any.
		const char *name = strrchr(argv[0], '/');
		if (name == NULL)
			name = argv[0];
		else
			++name;

		// NOTE: It's possible that name[0] is now '\0' if argv[0]
		// is weird, but it doesn't matter here.

		// Look for full command names instead of substrings like
		// "un", "cat", and "lz" to reduce possibility of false
		// positives when the programs have been renamed.
		if (strstr(name, "xzcat") != NULL) {
			opt_mode = MODE_DECOMPRESS;
			opt_stdout = true;
		} else if (strstr(name, "unxz") != NULL) {
			opt_mode = MODE_DECOMPRESS;
		} else if (strstr(name, "lzcat") != NULL) {
			opt_format = FORMAT_LZMA;
			opt_mode = MODE_DECOMPRESS;
			opt_stdout = true;
		} else if (strstr(name, "unlzma") != NULL) {
			opt_format = FORMAT_LZMA;
			opt_mode = MODE_DECOMPRESS;
		} else if (strstr(name, "lzma") != NULL) {
			opt_format = FORMAT_LZMA;
		}
	}

	// First the flags from the environment
	parse_environment(args, argv[0], "XZ_DEFAULTS");
	parse_environment(args, argv[0], "XZ_OPT");

	// Then from the command line
	parse_real(args, argc, argv);

	// If encoder or decoder support was omitted at build time,
	// show an error now so that the rest of the code can rely on
	// that whatever is in opt_mode is also supported.
#ifndef HAVE_ENCODERS
	if (opt_mode == MODE_COMPRESS)
		message_fatal(_("Compression support was disabled "
				"at build time"));
#endif
#ifndef HAVE_DECODERS
	// Even MODE_LIST cannot work without decoder support so MODE_COMPRESS
	// is the only valid choice.
	if (opt_mode != MODE_COMPRESS)
		message_fatal(_("Decompression support was disabled "
				"at build time"));
#endif

	// Never remove the source file when the destination is not on disk.
	// In test mode the data is written nowhere, but setting opt_stdout
	// will make the rest of the code behave well.
	if (opt_stdout || opt_mode == MODE_TEST) {
		opt_keep_original = true;
		opt_stdout = true;
	}

	// When compressing, if no --format flag was used, or it
	// was --format=auto, we compress to the .xz format.
	if (opt_mode == MODE_COMPRESS && opt_format == FORMAT_AUTO)
		opt_format = FORMAT_XZ;

	// Compression settings need to be validated (options themselves and
	// their memory usage) when compressing to any file format. It has to
	// be done also when uncompressing raw data, since for raw decoding
	// the options given on the command line are used to know what kind
	// of raw data we are supposed to decode.
	if (opt_mode == MODE_COMPRESS || opt_format == FORMAT_RAW)
		coder_set_compression_settings();

	// If no filenames are given, use stdin.
	if (argv[optind] == NULL && args->files_name == NULL) {
		// We don't modify or free() the "-" constant. The caller
		// modifies this so don't make the struct itself const.
		static char *names_stdin[2] = { (char *)"-", NULL };
		args->arg_names = names_stdin;
		args->arg_count = 1;
	} else {
		// We got at least one filename from the command line, or
		// --files or --files0 was specified.
		args->arg_names = argv + optind;
		args->arg_count = argc - optind;
	}

	return;
}


#ifndef NDEBUG
extern void
args_free(void)
{
	free(opt_block_list);
	return;
}
#endif
