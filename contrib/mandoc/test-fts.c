#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <stdio.h>
#include <string.h>

#ifdef FTS_COMPARE_CONST
int fts_compare(const FTSENT *const *, const FTSENT *const *);
#else
int fts_compare(const FTSENT **, const FTSENT **);
#endif

int
main(void)
{
	const char	*argv[2];
	FTS		*ftsp;
	FTSENT		*entry;

	argv[0] = ".";
	argv[1] = (char *)NULL;

	ftsp = fts_open((char * const *)argv,
	    FTS_PHYSICAL | FTS_NOCHDIR, fts_compare);

	if (ftsp == NULL) {
		perror("fts_open");
		return 1;
	}

	entry = fts_read(ftsp);

	if (entry == NULL) {
		perror("fts_read");
		return 1;
	}

	if (fts_set(ftsp, entry, FTS_SKIP) != 0) {
		perror("fts_set");
		return 1;
	}

	if (fts_close(ftsp) != 0) {
		perror("fts_close");
		return 1;
	}

	return 0;
}

int
#ifdef FTS_COMPARE_CONST
fts_compare(const FTSENT *const *a, const FTSENT *const *b)
#else
fts_compare(const FTSENT **a, const FTSENT **b)
#endif
{
	return strcmp((*a)->fts_name, (*b)->fts_name);
}
