///////////////////////////////////////////////////////////////////////////////
//
/// \file       options.c
/// \brief      Parser for filter-specific options
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

#include "private.h"


///////////////////
// Generic stuff //
///////////////////

typedef struct {
	const char *name;
	uint64_t id;
} name_id_map;


typedef struct {
	const char *name;
	const name_id_map *map;
	uint64_t min;
	uint64_t max;
} option_map;


/// Parses option=value pairs that are separated with commas:
/// opt=val,opt=val,opt=val
///
/// Each option is a string, that is converted to an integer using the
/// index where the option string is in the array.
///
/// Value can be
///  - a string-id map mapping a list of possible string values to integers
///    (opts[i].map != NULL, opts[i].min and opts[i].max are ignored);
///  - a number with minimum and maximum value limit
///    (opts[i].map == NULL && opts[i].min != UINT64_MAX);
///  - a string that will be parsed by the filter-specific code
///    (opts[i].map == NULL && opts[i].min == UINT64_MAX, opts[i].max ignored)
///
/// When parsing both option and value succeed, a filter-specific function
/// is called, which should update the given value to filter-specific
/// options structure.
///
/// \param      str     String containing the options from the command line
/// \param      opts    Filter-specific option map
/// \param      set     Filter-specific function to update filter_options
/// \param      filter_options  Pointer to filter-specific options structure
///
/// \return     Returns only if no errors occur.
///
static void
parse_options(const char *str, const option_map *opts,
		void (*set)(void *filter_options,
			unsigned key, uint64_t value, const char *valuestr),
		void *filter_options)
{
	if (str == NULL || str[0] == '\0')
		return;

	char *s = xstrdup(str);
	char *name = s;

	while (*name != '\0') {
		if (*name == ',') {
			++name;
			continue;
		}

		char *split = strchr(name, ',');
		if (split != NULL)
			*split = '\0';

		char *value = strchr(name, '=');
		if (value != NULL)
			*value++ = '\0';

		if (value == NULL || value[0] == '\0')
			message_fatal(_("%s: Options must be `name=value' "
					"pairs separated with commas"), str);

		// Look for the option name from the option map.
		unsigned i = 0;
		while (true) {
			if (opts[i].name == NULL)
				message_fatal(_("%s: Invalid option name"),
						name);

			if (strcmp(name, opts[i].name) == 0)
				break;

			++i;
		}

		// Option was found from the map. See how we should handle it.
		if (opts[i].map != NULL) {
			// value is a string which we should map
			// to an integer.
			unsigned j;
			for (j = 0; opts[i].map[j].name != NULL; ++j) {
				if (strcmp(opts[i].map[j].name, value) == 0)
					break;
			}

			if (opts[i].map[j].name == NULL)
				message_fatal(_("%s: Invalid option value"),
						value);

			set(filter_options, i, opts[i].map[j].id, value);

		} else if (opts[i].min == UINT64_MAX) {
			// value is a special string that will be
			// parsed by set().
			set(filter_options, i, 0, value);

		} else {
			// value is an integer.
			const uint64_t v = str_to_uint64(name, value,
					opts[i].min, opts[i].max);
			set(filter_options, i, v, value);
		}

		// Check if it was the last option.
		if (split == NULL)
			break;

		name = split + 1;
	}

	free(s);
	return;
}


///////////
// Delta //
///////////

enum {
	OPT_DIST,
};


static void
set_delta(void *options, unsigned key, uint64_t value,
		const char *valuestr lzma_attribute((__unused__)))
{
	lzma_options_delta *opt = options;
	switch (key) {
	case OPT_DIST:
		opt->dist = value;
		break;
	}
}


extern lzma_options_delta *
options_delta(const char *str)
{
	static const option_map opts[] = {
		{ "dist",     NULL,  LZMA_DELTA_DIST_MIN,
		                     LZMA_DELTA_DIST_MAX },
		{ NULL,       NULL,  0, 0 }
	};

	lzma_options_delta *options = xmalloc(sizeof(lzma_options_delta));
	*options = (lzma_options_delta){
		// It's hard to give a useful default for this.
		.type = LZMA_DELTA_TYPE_BYTE,
		.dist = LZMA_DELTA_DIST_MIN,
	};

	parse_options(str, opts, &set_delta, options);

	return options;
}


/////////
// BCJ //
/////////

enum {
	OPT_START_OFFSET,
};


static void
set_bcj(void *options, unsigned key, uint64_t value,
		const char *valuestr lzma_attribute((__unused__)))
{
	lzma_options_bcj *opt = options;
	switch (key) {
	case OPT_START_OFFSET:
		opt->start_offset = value;
		break;
	}
}


extern lzma_options_bcj *
options_bcj(const char *str)
{
	static const option_map opts[] = {
		{ "start",    NULL,  0, UINT32_MAX },
		{ NULL,       NULL,  0, 0 }
	};

	lzma_options_bcj *options = xmalloc(sizeof(lzma_options_bcj));
	*options = (lzma_options_bcj){
		.start_offset = 0,
	};

	parse_options(str, opts, &set_bcj, options);

	return options;
}


//////////
// LZMA //
//////////

enum {
	OPT_PRESET,
	OPT_DICT,
	OPT_LC,
	OPT_LP,
	OPT_PB,
	OPT_MODE,
	OPT_NICE,
	OPT_MF,
	OPT_DEPTH,
};


static void lzma_attribute((__noreturn__))
error_lzma_preset(const char *valuestr)
{
	message_fatal(_("Unsupported LZMA1/LZMA2 preset: %s"), valuestr);
}


static void
set_lzma(void *options, unsigned key, uint64_t value, const char *valuestr)
{
	lzma_options_lzma *opt = options;

	switch (key) {
	case OPT_PRESET: {
		if (valuestr[0] < '0' || valuestr[0] > '9')
			error_lzma_preset(valuestr);

		uint32_t preset = valuestr[0] - '0';

		// Currently only "e" is supported as a modifier,
		// so keep this simple for now.
		if (valuestr[1] != '\0') {
			if (valuestr[1] == 'e')
				preset |= LZMA_PRESET_EXTREME;
			else
				error_lzma_preset(valuestr);

			if (valuestr[2] != '\0')
				error_lzma_preset(valuestr);
		}

		if (lzma_lzma_preset(options, preset))
			error_lzma_preset(valuestr);

		break;
	}

	case OPT_DICT:
		opt->dict_size = value;
		break;

	case OPT_LC:
		opt->lc = value;
		break;

	case OPT_LP:
		opt->lp = value;
		break;

	case OPT_PB:
		opt->pb = value;
		break;

	case OPT_MODE:
		opt->mode = value;
		break;

	case OPT_NICE:
		opt->nice_len = value;
		break;

	case OPT_MF:
		opt->mf = value;
		break;

	case OPT_DEPTH:
		opt->depth = value;
		break;
	}
}


extern lzma_options_lzma *
options_lzma(const char *str)
{
	static const name_id_map modes[] = {
		{ "fast",   LZMA_MODE_FAST },
		{ "normal", LZMA_MODE_NORMAL },
		{ NULL,     0 }
	};

	static const name_id_map mfs[] = {
		{ "hc3", LZMA_MF_HC3 },
		{ "hc4", LZMA_MF_HC4 },
		{ "bt2", LZMA_MF_BT2 },
		{ "bt3", LZMA_MF_BT3 },
		{ "bt4", LZMA_MF_BT4 },
		{ NULL,  0 }
	};

	static const option_map opts[] = {
		{ "preset", NULL,   UINT64_MAX, 0 },
		{ "dict",   NULL,   LZMA_DICT_SIZE_MIN,
				(UINT32_C(1) << 30) + (UINT32_C(1) << 29) },
		{ "lc",     NULL,   LZMA_LCLP_MIN, LZMA_LCLP_MAX },
		{ "lp",     NULL,   LZMA_LCLP_MIN, LZMA_LCLP_MAX },
		{ "pb",     NULL,   LZMA_PB_MIN, LZMA_PB_MAX },
		{ "mode",   modes,  0, 0 },
		{ "nice",   NULL,   2, 273 },
		{ "mf",     mfs,    0, 0 },
		{ "depth",  NULL,   0, UINT32_MAX },
		{ NULL,     NULL,   0, 0 }
	};

	lzma_options_lzma *options = xmalloc(sizeof(lzma_options_lzma));
	if (lzma_lzma_preset(options, LZMA_PRESET_DEFAULT))
		message_bug();

	parse_options(str, opts, &set_lzma, options);

	if (options->lc + options->lp > LZMA_LCLP_MAX)
		message_fatal(_("The sum of lc and lp must not exceed 4"));

	const uint32_t nice_len_min = options->mf & 0x0F;
	if (options->nice_len < nice_len_min)
		message_fatal(_("The selected match finder requires at "
				"least nice=%" PRIu32), nice_len_min);

	return options;
}
