///////////////////////////////////////////////////////////////////////////////
//
/// \file       suffix.c
/// \brief      Checks filename suffix and creates the destination filename
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "private.h"

#ifdef __DJGPP__
#	include <fcntl.h>
#endif

// For case-insensitive filename suffix on case-insensitive systems
#if defined(TUKLIB_DOSLIKE) || defined(__VMS)
#	define strcmp strcasecmp
#endif


static char *custom_suffix = NULL;


/// \brief      Test if the char is a directory separator
static bool
is_dir_sep(char c)
{
#ifdef TUKLIB_DOSLIKE
	return c == '/' || c == '\\' || c == ':';
#else
	return c == '/';
#endif
}


/// \brief      Test if the string contains a directory separator
static bool
has_dir_sep(const char *str)
{
#ifdef TUKLIB_DOSLIKE
	return strpbrk(str, "/\\:") != NULL;
#else
	return strchr(str, '/') != NULL;
#endif
}


#ifdef __DJGPP__
/// \brief      Test for special suffix used for 8.3 short filenames (SFN)
///
/// \return     If str matches *.?- or *.??-, true is returned. Otherwise
///             false is returned.
static bool
has_sfn_suffix(const char *str, size_t len)
{
	if (len >= 4 && str[len - 1] == '-' && str[len - 2] != '.'
			&& !is_dir_sep(str[len - 2])) {
		// *.?-
		if (str[len - 3] == '.')
			return !is_dir_sep(str[len - 4]);

		// *.??-
		if (len >= 5 && !is_dir_sep(str[len - 3])
				&& str[len - 4] == '.')
			return !is_dir_sep(str[len - 5]);
	}

	return false;
}
#endif


/// \brief      Checks if src_name has given compressed_suffix
///
/// \param      suffix      Filename suffix to look for
/// \param      src_name    Input filename
/// \param      src_len     strlen(src_name)
///
/// \return     If src_name has the suffix, src_len - strlen(suffix) is
///             returned. It's always a positive integer. Otherwise zero
///             is returned.
static size_t
test_suffix(const char *suffix, const char *src_name, size_t src_len)
{
	const size_t suffix_len = strlen(suffix);

	// The filename must have at least one character in addition to
	// the suffix. src_name may contain path to the filename, so we
	// need to check for directory separator too.
	if (src_len <= suffix_len
			|| is_dir_sep(src_name[src_len - suffix_len - 1]))
		return 0;

	if (strcmp(suffix, src_name + src_len - suffix_len) == 0)
		return src_len - suffix_len;

	return 0;
}


/// \brief      Removes the filename suffix of the compressed file
///
/// \return     Name of the uncompressed file, or NULL if file has unknown
///             suffix.
static char *
uncompressed_name(const char *src_name, const size_t src_len)
{
	static const struct {
		const char *compressed;
		const char *uncompressed;
	} suffixes[] = {
		{ ".xz",    "" },
		{ ".txz",   ".tar" }, // .txz abbreviation for .txt.gz is rare.
		{ ".lzma",  "" },
#ifdef __DJGPP__
		{ ".lzm",   "" },
#endif
		{ ".tlz",   ".tar" },
		// { ".gz",    "" },
		// { ".tgz",   ".tar" },
	};

	const char *new_suffix = "";
	size_t new_len = 0;

	if (opt_format == FORMAT_RAW) {
		// Don't check for known suffixes when --format=raw was used.
		if (custom_suffix == NULL) {
			message_error(_("%s: With --format=raw, "
					"--suffix=.SUF is required unless "
					"writing to stdout"), src_name);
			return NULL;
		}
	} else {
		for (size_t i = 0; i < ARRAY_SIZE(suffixes); ++i) {
			new_len = test_suffix(suffixes[i].compressed,
					src_name, src_len);
			if (new_len != 0) {
				new_suffix = suffixes[i].uncompressed;
				break;
			}
		}

#ifdef __DJGPP__
		// Support also *.?- -> *.? and *.??- -> *.?? on DOS.
		// This is done also when long filenames are available
		// to keep it easy to decompress files created when
		// long filename support wasn't available.
		if (new_len == 0 && has_sfn_suffix(src_name, src_len)) {
			new_suffix = "";
			new_len = src_len - 1;
		}
#endif
	}

	if (new_len == 0 && custom_suffix != NULL)
		new_len = test_suffix(custom_suffix, src_name, src_len);

	if (new_len == 0) {
		message_warning(_("%s: Filename has an unknown suffix, "
				"skipping"), src_name);
		return NULL;
	}

	const size_t new_suffix_len = strlen(new_suffix);
	char *dest_name = xmalloc(new_len + new_suffix_len + 1);

	memcpy(dest_name, src_name, new_len);
	memcpy(dest_name + new_len, new_suffix, new_suffix_len);
	dest_name[new_len + new_suffix_len] = '\0';

	return dest_name;
}


/// This message is needed in multiple places in compressed_name(),
/// so the message has been put into its own function.
static void
msg_suffix(const char *src_name, const char *suffix)
{
	message_warning(_("%s: File already has `%s' suffix, skipping"),
			src_name, suffix);
	return;
}


/// \brief      Appends suffix to src_name
///
/// In contrast to uncompressed_name(), we check only suffixes that are valid
/// for the specified file format.
static char *
compressed_name(const char *src_name, size_t src_len)
{
	// The order of these must match the order in args.h.
	static const char *const all_suffixes[][4] = {
		{
			".xz",
			".txz",
			NULL
		}, {
			".lzma",
#ifdef __DJGPP__
			".lzm",
#endif
			".tlz",
			NULL
/*
		}, {
			".gz",
			".tgz",
			NULL
*/
		}, {
			// --format=raw requires specifying the suffix
			// manually or using stdout.
			NULL
		}
	};

	// args.c ensures this.
	assert(opt_format != FORMAT_AUTO);

	const size_t format = opt_format - 1;
	const char *const *suffixes = all_suffixes[format];

	// Look for known filename suffixes and refuse to compress them.
	for (size_t i = 0; suffixes[i] != NULL; ++i) {
		if (test_suffix(suffixes[i], src_name, src_len) != 0) {
			msg_suffix(src_name, suffixes[i]);
			return NULL;
		}
	}

#ifdef __DJGPP__
	// Recognize also the special suffix that is used when long
	// filename (LFN) support isn't available. This suffix is
	// recognized on LFN systems too.
	if (opt_format == FORMAT_XZ && has_sfn_suffix(src_name, src_len)) {
		msg_suffix(src_name, "-");
		return NULL;
	}
#endif

	if (custom_suffix != NULL) {
		if (test_suffix(custom_suffix, src_name, src_len) != 0) {
			msg_suffix(src_name, custom_suffix);
			return NULL;
		}
	}

	// TODO: Hmm, maybe it would be better to validate this in args.c,
	// since the suffix handling when decoding is weird now.
	if (opt_format == FORMAT_RAW && custom_suffix == NULL) {
		message_error(_("%s: With --format=raw, "
				"--suffix=.SUF is required unless "
				"writing to stdout"), src_name);
		return NULL;
	}

	const char *suffix = custom_suffix != NULL
			? custom_suffix : suffixes[0];
	size_t suffix_len = strlen(suffix);

#ifdef __DJGPP__
	if (!_use_lfn(src_name)) {
		// Long filename (LFN) support isn't available and we are
		// limited to 8.3 short filenames (SFN).
		//
		// Look for suffix separator from the filename, and make sure
		// that it is in the filename, not in a directory name.
		const char *sufsep = strrchr(src_name, '.');
		if (sufsep == NULL || sufsep[1] == '\0'
				|| has_dir_sep(sufsep)) {
			// src_name has no filename extension.
			//
			// Examples:
			// xz foo         -> foo.xz
			// xz -F lzma foo -> foo.lzm
			// xz -S x foo    -> foox
			// xz -S x foo.   -> foo.x
			// xz -S x.y foo  -> foox.y
			// xz -S .x foo   -> foo.x
			// xz -S .x foo.  -> foo.x
			//
			// Avoid double dots:
			if (sufsep != NULL && sufsep[1] == '\0'
					&& suffix[0] == '.')
				--src_len;

		} else if (custom_suffix == NULL
				&& strcasecmp(sufsep, ".tar") == 0) {
			// ".tar" is handled specially.
			//
			// Examples:
			// xz foo.tar          -> foo.txz
			// xz -F lzma foo.tar  -> foo.tlz
			static const char *const tar_suffixes[] = {
				".txz",
				".tlz",
				// ".tgz",
			};
			suffix = tar_suffixes[format];
			suffix_len = 4;
			src_len -= 4;

		} else {
			if (custom_suffix == NULL && opt_format == FORMAT_XZ) {
				// Instead of the .xz suffix, use a single
				// character at the end of the filename
				// extension. This is to minimize name
				// conflicts when compressing multiple files
				// with the same basename. E.g. foo.txt and
				// foo.exe become foo.tx- and foo.ex-. Dash
				// is rare as the last character of the
				// filename extension, so it seems to be
				// quite safe choice and it stands out better
				// in directory listings than e.g. x. For
				// comparison, gzip uses z.
				suffix = "-";
				suffix_len = 1;
			}

			if (suffix[0] == '.') {
				// The first character of the suffix is a dot.
				// Throw away the original filename extension
				// and replace it with the new suffix.
				//
				// Examples:
				// xz -F lzma foo.txt  -> foo.lzm
				// xz -S .x  foo.txt   -> foo.x
				src_len = sufsep - src_name;

			} else {
				// The first character of the suffix is not
				// a dot. Preserve the first 0-2 characters
				// of the original filename extension.
				//
				// Examples:
				// xz foo.txt         -> foo.tx-
				// xz -S x  foo.c     -> foo.cx
				// xz -S ab foo.c     -> foo.cab
				// xz -S ab foo.txt   -> foo.tab
				// xz -S abc foo.txt  -> foo.abc
				//
				// Truncate the suffix to three chars:
				if (suffix_len > 3)
					suffix_len = 3;

				// If needed, overwrite 1-3 characters.
				if (strlen(sufsep) > 4 - suffix_len)
					src_len = sufsep - src_name
							+ 4 - suffix_len;
			}
		}
	}
#endif

	char *dest_name = xmalloc(src_len + suffix_len + 1);

	memcpy(dest_name, src_name, src_len);
	memcpy(dest_name + src_len, suffix, suffix_len);
	dest_name[src_len + suffix_len] = '\0';

	return dest_name;
}


extern char *
suffix_get_dest_name(const char *src_name)
{
	assert(src_name != NULL);

	// Length of the name is needed in all cases to locate the end of
	// the string to compare the suffix, so calculate the length here.
	const size_t src_len = strlen(src_name);

	return opt_mode == MODE_COMPRESS
			? compressed_name(src_name, src_len)
			: uncompressed_name(src_name, src_len);
}


extern void
suffix_set(const char *suffix)
{
	// Empty suffix and suffixes having a directory separator are
	// rejected. Such suffixes would break things later.
	if (suffix[0] == '\0' || has_dir_sep(suffix))
		message_fatal(_("%s: Invalid filename suffix"), suffix);

	// Replace the old custom_suffix (if any) with the new suffix.
	free(custom_suffix);
	custom_suffix = xstrdup(suffix);
	return;
}
