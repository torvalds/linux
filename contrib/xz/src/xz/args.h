///////////////////////////////////////////////////////////////////////////////
//
/// \file       args.h
/// \brief      Argument parsing
//
//  Author:     Lasse Collin
//
//  This file has been put into the public domain.
//  You can do whatever you want with this file.
//
///////////////////////////////////////////////////////////////////////////////

typedef struct {
	/// Filenames from command line
	char **arg_names;

	/// Number of filenames from command line
	unsigned int arg_count;

	/// Name of the file from which to read filenames. This is NULL
	/// if --files or --files0 was not used.
	char *files_name;

	/// File opened for reading from which filenames are read. This is
	/// non-NULL only if files_name is non-NULL.
	FILE *files_file;

	/// Delimiter for filenames read from files_file
	char files_delim;

} args_info;


extern bool opt_stdout;
extern bool opt_force;
extern bool opt_keep_original;
// extern bool opt_recursive;
extern bool opt_robot;
extern bool opt_ignore_check;

extern const char stdin_filename[];

extern void args_parse(args_info *args, int argc, char **argv);
extern void args_free(void);
