///////////////////////////////////////////////////////////////////////////////
//
/// \file       lzmainfo.c
/// \brief      lzmainfo tool for compatibility with LZMA Utils
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "sysdefs.h"
#include <stdio.h>
#include <errno.h>

#include "lzma.h"
#include "getopt.h"
#include "tuklib_gettext.h"
#include "tuklib_progname.h"
#include "tuklib_exit.h"

#ifdef TUKLIB_DOSLIKE
#	include <fcntl.h>
#	include <io.h>
#endif


static void lzma_attribute((__noreturn__))
help(void)
{
	printf(
_("Usage: %s [--help] [--version] [FILE]...\n"
"Show information stored in the .lzma file header"), progname);

	printf(_(
"\nWith no FILE, or when FILE is -, read standard input.\n"));
	printf("\n");

	printf(_("Report bugs to <%s> (in English or Finnish).\n"),
			PACKAGE_BUGREPORT);
	printf(_("%s home page: <%s>\n"), PACKAGE_NAME, PACKAGE_URL);

	tuklib_exit(EXIT_SUCCESS, EXIT_FAILURE, true);
}


static void lzma_attribute((__noreturn__))
version(void)
{
	puts("lzmainfo (" PACKAGE_NAME ") " LZMA_VERSION_STRING);
	tuklib_exit(EXIT_SUCCESS, EXIT_FAILURE, true);
}


/// Parse command line options.
static void
parse_args(int argc, char **argv)
{
	enum {
		OPT_HELP,
		OPT_VERSION,
	};

	static const struct option long_opts[] = {
		{ "help",    no_argument, NULL, OPT_HELP },
		{ "version", no_argument, NULL, OPT_VERSION },
		{ NULL,      0,           NULL, 0 }
	};

	int c;
	while ((c = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
		switch (c) {
		case OPT_HELP:
			help();

		case OPT_VERSION:
			version();

		default:
			exit(EXIT_FAILURE);
		}
	}

	return;
}


/// Primitive base-2 logarithm for integers
static uint32_t
my_log2(uint32_t n)
{
	uint32_t e;
	for (e = 0; n > 1; ++e, n /= 2) ;
	return e;
}


/// Parse the .lzma header and display information about it.
static bool
lzmainfo(const char *name, FILE *f)
{
	uint8_t buf[13];
	const size_t size = fread(buf, 1, sizeof(buf), f);
	if (size != 13) {
		fprintf(stderr, "%s: %s: %s\n", progname, name,
				ferror(f) ? strerror(errno)
				: _("File is too small to be a .lzma file"));
		return true;
	}

	lzma_filter filter = { .id = LZMA_FILTER_LZMA1 };

	// Parse the first five bytes.
	switch (lzma_properties_decode(&filter, NULL, buf, 5)) {
	case LZMA_OK:
		break;

	case LZMA_OPTIONS_ERROR:
		fprintf(stderr, "%s: %s: %s\n", progname, name,
				_("Not a .lzma file"));
		return true;

	case LZMA_MEM_ERROR:
		fprintf(stderr, "%s: %s\n", progname, strerror(ENOMEM));
		exit(EXIT_FAILURE);

	default:
		fprintf(stderr, "%s: %s\n", progname,
				_("Internal error (bug)"));
		exit(EXIT_FAILURE);
	}

	// Uncompressed size
	uint64_t uncompressed_size = 0;
	for (size_t i = 0; i < 8; ++i)
		uncompressed_size |= (uint64_t)(buf[5 + i]) << (i * 8);

	// Display the results. We don't want to translate these and also
	// will use MB instead of MiB, because someone could be parsing
	// this output and we don't want to break that when people move
	// from LZMA Utils to XZ Utils.
	if (f != stdin)
		printf("%s\n", name);

	printf("Uncompressed size:             ");
	if (uncompressed_size == UINT64_MAX)
		printf("Unknown");
	else
		printf("%" PRIu64 " MB (%" PRIu64 " bytes)",
				(uncompressed_size + 512 * 1024)
					/ (1024 * 1024),
				uncompressed_size);

	lzma_options_lzma *opt = filter.options;

	printf("\nDictionary size:               "
			"%" PRIu32 " MB (2^%" PRIu32 " bytes)\n"
			"Literal context bits (lc):     %" PRIu32 "\n"
			"Literal pos bits (lp):         %" PRIu32 "\n"
			"Number of pos bits (pb):       %" PRIu32 "\n",
			(opt->dict_size + 512 * 1024) / (1024 * 1024),
			my_log2(opt->dict_size), opt->lc, opt->lp, opt->pb);

	free(opt);

	return false;
}


extern int
main(int argc, char **argv)
{
	tuklib_progname_init(argv);
	tuklib_gettext_init(PACKAGE, LOCALEDIR);

	parse_args(argc, argv);

#ifdef TUKLIB_DOSLIKE
	setmode(fileno(stdin), O_BINARY);
#endif

	int ret = EXIT_SUCCESS;

	// We print empty lines around the output only when reading from
	// files specified on the command line. This is due to how
	// LZMA Utils did it.
	if (optind == argc) {
		if (lzmainfo("(stdin)", stdin))
			ret = EXIT_FAILURE;
	} else {
		printf("\n");

		do {
			if (strcmp(argv[optind], "-") == 0) {
				if (lzmainfo("(stdin)", stdin))
					ret = EXIT_FAILURE;
			} else {
				FILE *f = fopen(argv[optind], "r");
				if (f == NULL) {
					ret = EXIT_FAILURE;
					fprintf(stderr, "%s: %s: %s\n",
							progname,
							argv[optind],
							strerror(errno));
					continue;
				}

				if (lzmainfo(argv[optind], f))
					ret = EXIT_FAILURE;

				printf("\n");
				fclose(f);
			}
		} while (++optind < argc);
	}

	tuklib_exit(ret, EXIT_FAILURE, true);
}
