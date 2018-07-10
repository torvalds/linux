/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "bb_archive.h"

void FAST_FUNC header_verbose_list(const file_header_t *file_header)
{
	struct tm tm_time;
	struct tm *ptm = &tm_time; //localtime(&file_header->mtime);

#if ENABLE_FEATURE_TAR_UNAME_GNAME
	char uid[sizeof(int)*3 + 2];
	/*char gid[sizeof(int)*3 + 2];*/
	char *user;
	char *group;

	localtime_r(&file_header->mtime, ptm);

	user = file_header->tar__uname;
	if (user == NULL) {
		sprintf(uid, "%u", (unsigned)file_header->uid);
		user = uid;
	}
	group = file_header->tar__gname;
	if (group == NULL) {
		/*sprintf(gid, "%u", (unsigned)file_header->gid);*/
		group = utoa(file_header->gid);
	}
	printf("%s %s/%s %9"OFF_FMT"u %4u-%02u-%02u %02u:%02u:%02u %s",
		bb_mode_string(file_header->mode),
		user,
		group,
		file_header->size,
		1900 + ptm->tm_year,
		1 + ptm->tm_mon,
		ptm->tm_mday,
		ptm->tm_hour,
		ptm->tm_min,
		ptm->tm_sec,
		file_header->name);

#else /* !FEATURE_TAR_UNAME_GNAME */

	localtime_r(&file_header->mtime, ptm);

	printf("%s %u/%u %9"OFF_FMT"u %4u-%02u-%02u %02u:%02u:%02u %s",
		bb_mode_string(file_header->mode),
		(unsigned)file_header->uid,
		(unsigned)file_header->gid,
		file_header->size,
		1900 + ptm->tm_year,
		1 + ptm->tm_mon,
		ptm->tm_mday,
		ptm->tm_hour,
		ptm->tm_min,
		ptm->tm_sec,
		file_header->name);

#endif /* FEATURE_TAR_UNAME_GNAME */

	/* NB: GNU tar shows "->" for symlinks and "link to" for hardlinks */
	if (file_header->link_target) {
		printf(" -> %s", file_header->link_target);
	}
	bb_putchar('\n');
}
