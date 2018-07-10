/* vi: set sw=4 ts=4: */
/*
 * makemime: create MIME-encoded message
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config MAKEMIME
//config:	bool "makemime (5.9 kb)"
//config:	default y
//config:	help
//config:	Create MIME-formatted messages.

//applet:IF_MAKEMIME(APPLET(makemime, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_MAKEMIME) += makemime.o mail.o

#include "libbb.h"
#include "mail.h"

#if 0
# define dbg_error_msg(...) bb_error_msg(__VA_ARGS__)
#else
# define dbg_error_msg(...) ((void)0)
#endif

/*
  makemime -c type [-o file] [-e encoding] [-C charset] [-N name] \
                   [-a "Header: Contents"] file
           -m [ type ] [-o file] [-e encoding] [-a "Header: Contents"] file
           -j [-o file] file1 file2
           @file

   file:  filename    - read or write from filename
          -           - read or write from stdin or stdout
          &n          - read or write from file descriptor n
          \( opts \)  - read from child process, that generates [ opts ]

Options:
  -c type         - create a new MIME section from "file" with this
                    Content-Type: (default is application/octet-stream).
  -C charset      - MIME charset of a new text/plain section.
  -N name         - MIME content name of the new mime section.
  -m [ type ]     - create a multipart mime section from "file" of this
                    Content-Type: (default is multipart/mixed).
  -e encoding     - use the given encoding (7bit, 8bit, quoted-printable,
                    or base64), instead of guessing.  Omit "-e" and use
                    -c auto to set Content-Type: to text/plain or
                    application/octet-stream based on picked encoding.
  -j file1 file2  - join mime section file2 to multipart section file1.
  -o file         - write the result to file, instead of stdout (not
                    allowed in child processes).
  -a header       - prepend an additional header to the output.

  @file - read all of the above options from file, one option or
          value on each line.
  {which version of makemime is this? What do we support?}
*/
/* man makemime:

 * -c TYPE: create a (non-multipart) MIME section with Content-Type: TYPE
 * makemime -c TYPE [-e ENCODING] [-o OUTFILE] [-C CHARSET] [-N NAME] [-a HEADER...] FILE
 * The -C option sets the MIME charset attribute for text/plain content.
 * The -N option sets the name attribute for Content-Type:
 * Encoding must be one of the following: 7bit, 8bit, quoted-printable, or base64.

 * -m multipart/TYPE: create a multipart MIME collection with Content-Type: multipart/TYPE
 * makemime -m multipart/TYPE [-e ENCODING] [-o OUTFILE] [-a HEADER...] FILE
 * Type must be either "multipart/mixed", "multipart/alternative", or some other MIME multipart content type.
 * Additionally, encoding can only be "7bit" or "8bit", and will default to "8bit" if not specified.
 * Finally, filename must be a MIME-formatted section, NOT a regular file.
 * The -m option creates an initial multipart MIME collection, that contains only one MIME section, taken from filename.
 * The collection is written to standard output, or the pipe or to outputfile.

 * -j FILE1: add a section to a multipart MIME collection
 * makemime -j FILE1 [-o OUTFILE] FILE2
 * FILE1 must be a MIME collection that was previously created by the -m option.
 * FILE2 must be a MIME section that was previously created by the -c option.
 * The -j options adds the MIME section in FILE2 to the MIME collection in FILE1.
 */


/* In busybox 1.15.0.svn, makemime generates output like this
 * (empty lines are shown exactly!):
{headers added with -a HDR}
Mime-Version: 1.0
Content-Type: multipart/mixed; boundary="24269534-2145583448-1655890676"

--24269534-2145583448-1655890676
Content-Type: {set by -c, e.g. text/plain}; charset={set by -C, e.g. us-ascii}
Content-Disposition: inline; filename="A"
Content-Transfer-Encoding: base64

...file A contents...
--24269534-2145583448-1655890676
Content-Type: {set by -c, e.g. text/plain}; charset={set by -C, e.g. us-ascii}
Content-Disposition: inline; filename="B"
Content-Transfer-Encoding: base64

...file B contents...
--24269534-2145583448-1655890676--

 *
 * For reference: here is an example email to LKML which has
 * 1st unnamed part (so it serves as an email body)
 * and one attached file:
...other headers...
Content-Type: multipart/mixed; boundary="=-tOfTf3byOS0vZgxEWcX+"
...other headers...
Mime-Version: 1.0
...other headers...


--=-tOfTf3byOS0vZgxEWcX+
Content-Type: text/plain
Content-Transfer-Encoding: 7bit

...email text...
...email text...


--=-tOfTf3byOS0vZgxEWcX+
Content-Disposition: attachment; filename="xyz"
Content-Type: text/plain; name="xyz"; charset="UTF-8"
Content-Transfer-Encoding: 7bit

...file contents...
...file contents...

--=-tOfTf3byOS0vZgxEWcX+--

...random junk added by mailing list robots and such...
*/

//usage:#define makemime_trivial_usage
//usage:       "[OPTIONS] [FILE]..."
//usage:#define makemime_full_usage "\n\n"
//usage:       "Create multipart MIME-encoded message from FILEs\n"
/* //usage:    "Transfer encoding is base64, disposition is inline (not attachment)\n" */
//usage:     "\n	-o FILE	Output. Default: stdout"
//usage:     "\n	-a HDR	Add header(s). Examples:"
//usage:     "\n		\"From: user@host.org\", \"Date: `date -R`\""
//usage:     "\n	-c CT	Content type. Default: application/octet-stream"
//usage:     "\n	-C CS	Charset. Default: " CONFIG_FEATURE_MIME_CHARSET
/* //usage:  "\n	-e ENC	Transfer encoding. Ignored. base64 is assumed" */
//usage:     "\n"
//usage:     "\nOther options are silently ignored"

/*
 * -c [Content-Type] should create just one MIME section
 * with "Content-Type:", "Content-Transfer-Encoding:", and HDRs from "-a HDR".
 * NB: without "Content-Disposition:" auto-added, unlike we do now
 * NB2: -c has *optional* param which nevertheless _can_ be specified after a space :(
 *
 * -m [multipart/mixed] should create multipart MIME section
 * with "Content-Type:", "Content-Transfer-Encoding:", and HDRs from "-a HDR",
 * and add FILE to it _verbatim_:
 *  HEADERS
 *
 *  --=_1_1321709112_1605
 *  FILE_CONTENTS
 *  --=_1_1321709112_1605
 * without any encoding of FILE_CONTENTS. (Basically, it expects that FILE
 * is the result of "makemime -c").
 *
 * -j MULTIPART_FILE1 SINGLE_FILE2 should output MULTIPART_FILE1 + SINGLE_FILE2
 *
 * Our current behavior is a mutant "-m + -c + -j" one: we create multipart MIME
 * and we put "-c" encoded FILEs into many multipart sections.
 */

int makemime_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int makemime_main(int argc UNUSED_PARAM, char **argv)
{
	llist_t *opt_headers = NULL, *l;
	const char *opt_output;
	const char *content_type = "application/octet-stream";
#define boundary opt_output
	enum {
		OPT_c = 1 << 0,         // create (non-multipart) section
		OPT_e = 1 << 1,         // Content-Transfer-Encoding. Ignored. Assumed base64
		OPT_o = 1 << 2,         // output to
		OPT_C = 1 << 3,         // charset
		OPT_N = 1 << 4,         // COMPAT
		OPT_a = 1 << 5,         // additional headers
		//OPT_m = 1 << 6,         // create mutipart section
		//OPT_j = 1 << 7,         // join section to multipart section
	};

	INIT_G();

	// parse options
	opts = getopt32(argv,
		"c:e:o:C:N:a:*", // "m:j:",
		&content_type, NULL, &opt_output, &G.opt_charset, NULL, &opt_headers //, NULL, NULL
	);
	//argc -= optind;
	argv += optind;

	// respect -o output
	if (opts & OPT_o)
		freopen(opt_output, "w", stdout);

	// no files given on command line? -> use stdin
	if (!*argv)
		*--argv = (char *)"-";

	// put additional headers
	for (l = opt_headers; l; l = l->link)
		puts(l->data);

	// make a random string -- it will delimit message parts
	srand(monotonic_us());
	boundary = xasprintf("%u-%u-%u",
			(unsigned)rand(), (unsigned)rand(), (unsigned)rand());

	// put multipart header
	printf(
		"Mime-Version: 1.0\n"
		"Content-Type: multipart/mixed; boundary=\"%s\"\n"
		, boundary
	);

	// put attachments
	while (*argv) {
		printf(
			"\n--%s\n"
			"Content-Type: %s; charset=%s\n"
			"Content-Disposition: inline; filename=\"%s\"\n"
			"Content-Transfer-Encoding: base64\n"
			, boundary
			, content_type
			, G.opt_charset
			, bb_get_last_path_component_strip(*argv)
		);
		encode_base64(*argv++, (const char *)stdin, "");
	}

	// put multipart footer
	printf("\n--%s--\n" "\n", boundary);

	return EXIT_SUCCESS;
#undef boundary
}
