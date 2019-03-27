#include <config.h>
#include <rc_cmdlength.h>

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

// XXX: Move to header.
size_t remoteconfig_cmdlength( const char *, const char *);

/* Bug 2853 */
/* evaluate the length of the command sequence. This breaks at the first
 * char that is not >= SPACE and <= 127 after trimming from the right.
 */
size_t
remoteconfig_cmdlength(
	const char *src_buf,
	const char *src_end
	)
{
	const char *scan;
	unsigned char ch;

	/* trim whitespace & garbage from the right */
	while (src_end != src_buf) {
		ch = src_end[-1];
		if (ch > ' ' && ch < 128)
			break;
		--src_end;
	}
	/* now do a forward scan */
	for (scan = src_buf; scan != src_end; ++scan) {
		ch = scan[0];
		if ((ch < ' ' || ch >= 128) && ch != '\t')
			break;
	}
	return (size_t)(scan - src_buf);
}
